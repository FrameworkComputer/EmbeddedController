/* Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * TODO(b/272518464): Work around coreboot GCC preprocessor bug.
 * #line marks the *next* line, so it is off by one.
 */
#line 11

#include "builtin/assert.h"
#include "compile_time_macros.h"
#include "console.h"
#include "hooks.h"
#include "timer.h"
#include "usb_common.h"
#include "usb_mux.h"
#include "usb_tc_sm.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef CONFIG_COMMON_RUNTIME
#define CPRINTS(format, args...) cprints(CC_USBPD, format, ##args)
#define CPRINTF(format, args...) cprintf(CC_USBPD, format, ##args)
#else
#define CPRINTS(format, args...)
#define CPRINTF(format, args...)
#endif

/*
 * Update retimer firmware of no device attached (NDA) ports
 *
 * https://docs.kernel.org/admin-guide/thunderbolt.html#
 * upgrading-on-board-retimer-nvm-when-there-is-no-cable-connected
 *
 * On EC side:
 * Retimer firmware update is initiated by AP.
 * The operations requested by AP are:
 * 0 - USB_RETIMER_FW_UPDATE_QUERY_PORT
 * 1 - USB_RETIMER_FW_UPDATE_SUSPEND_PD
 * 2 - USB_RETIMER_FW_UPDATE_RESUME_PD
 * 3 - USB_RETIMER_FW_UPDATE_GET_MUX
 * 4 - USB_RETIMER_FW_UPDATE_SET_USB
 * 5 - USB_RETIMER_FW_UPDATE_SET_SAFE
 * 6 - USB_RETIMER_FW_UPDATE_SET_TBT
 * 7 - USB_RETIMER_FW_UPDATE_DISCONNECT
 *
 * Operation 0 is processed immediately.
 * Operations 1 to 7 are deferred and processed inside tc_run().
 * Operations 1/2/3 can be processed any time; while 4/5/6/7 have
 * to be processed when PD task is suspended.
 * Two TC flags are created for this situation.
 * If Op 1/2/3 is received, TC_FLAGS_USB_RETIMER_FW_UPDATE_RUN
 * is set, PD task will be waken up and process it.
 * If 4/5/6/7 is received, TC_FLAGS_USB_RETIMER_FW_UPDATE_LTD_RUN is
 * set, PD task should be in suspended mode and process it.
 *
 * On host side:
 * 1. Put NDA ports into offline mode.
 *    This forces retimer to power on, and requests EC to suspend
 *    PD port, set USB mux to USB, Safe then TBT.
 * 2. Scan for retimers
 * 3. Update retimer NVM firmware.
 * 4. Authenticate.
 * 5. Wait 5 or more seconds for retimer to come back.
 * 6. Put NDA ports into online mode -- the functional state.
 *    This requestes EC to disconnect(set USB mux to 0), resume PD port.
 *
 * Error recovery:
 * As mentioned above, to put port online, host sends two requests to EC
 * 1. Disconnect USB MUX: USB_RETIMER_FW_UPDATE_DISCONNECT
 * if step 1 is successful, then
 * 2. Resume PD port: USB_RETIMER_FW_UPDATE_RESUME_PD
 *
 * If step 1 fails, host will not send step 2. This means no
 * resume request from host. PD port stays in suspended state.
 * EC needs an error recovery to resume PD port by itself.
 *
 * Below is how error recovery works:
 * PD port state is set to RETIMER_ONLINE_REQUESTED when receives
 * "Disconnect USB MUX"; a deferred call is set up too. When EC resumes
 * port upon host's request, port state will be set to RETIMER_ONLINE;
 * or port state stays RETIMER_ONLINE_REQUESTED if host doesn't request.
 * By the time the deferrred call is fired, it will check if any port is
 * still in RETIMER_ONLINE_REQUESTED state. If true, EC will put the
 * port online by itself. That is, retry disconnect and unconditionally
 * resume the port.
 */

#define SUSPEND 1
#define RESUME 0

enum retimer_port_state {
	RETIMER_ONLINE,
	RETIMER_OFFLINE,
	RETIMER_ONLINE_REQUESTED
};

/*
 * Two seconds buffer is added on top of required 5 seconds;
 * to cover the time to disconnect and resume.
 */
#define RETIMTER_ONLINE_DELAY (7 * SECOND)

/* Track current port AP requested to update retimer firmware */
static int cur_port;
static int last_op; /* Operation received from AP via ACPI_WRITE */
/* Operation result returned to ACPI_READ */
static int last_result;
/* Track port state: SUSPEND or RESUME */
static enum retimer_port_state port_state[CONFIG_USB_PD_PORT_MAX_COUNT];

int usb_retimer_fw_update_get_result(void)
{
	int result = 0;

	switch (last_op) {
	case USB_RETIMER_FW_UPDATE_SUSPEND_PD:
		if (last_result == USB_RETIMER_FW_UPDATE_ERR) {
			result = last_result;
			break;
		}
		__fallthrough;
	case USB_RETIMER_FW_UPDATE_RESUME_PD:
		result = pd_is_port_enabled(cur_port);
		break;
	case USB_RETIMER_FW_UPDATE_QUERY_PORT:
		result = usb_mux_retimer_fw_update_port_info();
		break;
	case USB_RETIMER_FW_UPDATE_GET_MUX:
	case USB_RETIMER_FW_UPDATE_SET_USB:
	case USB_RETIMER_FW_UPDATE_SET_SAFE:
	case USB_RETIMER_FW_UPDATE_SET_TBT:
	case USB_RETIMER_FW_UPDATE_DISCONNECT:
		result = last_result;
		break;
	default:
		break;
	}

	return result;
}

static void retimer_fw_update_set_port_state(int port,
					     enum retimer_port_state state)
{
	port_state[port] = state;
}

static enum retimer_port_state retimer_fw_update_get_port_state(int port)
{
	return port_state[port];
}

/**
 * @brief Suspend or resume PD task and update the state of the port.
 *
 * @param port PD port
 * @param suspend
 * SUSPEND: suspend PD task; set state to RETIMER_OFFLINE
 * RESUME: resume PD task; set state to RETIMER_ONLINE.
 *
 */
static void retimer_fw_update_port_handler(int port, bool suspend)
{
	pd_set_suspend(port, suspend);
	retimer_fw_update_set_port_state(
		port, suspend == SUSPEND ? RETIMER_OFFLINE : RETIMER_ONLINE);
}

static void deferred_pd_suspend(void)
{
	retimer_fw_update_port_handler(cur_port, SUSPEND);
}
DECLARE_DEFERRED(deferred_pd_suspend);

static inline mux_state_t retimer_fw_update_usb_mux_get(int port)
{
	return usb_mux_get(port) & USB_RETIMER_FW_UPDATE_MUX_MASK;
}

/*
 * Host will wait maximum 300ms for result; otherwise it's error.
 * so the polling takes 300ms too.
 */
#define POLLING_CYCLE 15
#define POLLING_TIME_MS 20

static bool query_usb_mux_set_completed_timeout(int port)
{
	int i;

	for (i = 0; i < POLLING_CYCLE; i++) {
		if (!usb_mux_set_completed(port))
			crec_msleep(POLLING_TIME_MS);
		else
			return false;
	}

	return true;
}

static void retry_online(int port)
{
	usb_mux_set(port, USB_PD_MUX_NONE, USB_SWITCH_DISCONNECT,
		    pd_get_polarity(port));
	/* Wait maximum 300 ms for USB mux to be set */
	query_usb_mux_set_completed_timeout(port);
	/* Resume the port unconditionally */
	retimer_fw_update_port_handler(port, RESUME);
}

/*
 * After NVM update, if AP skips step 5, not wait 5+ seconds for retimer
 * to come back; then do step 6 immediately, requesting EC to put
 * retimer online. Step 6 will fail; port is still offline afterwards.
 *
 * This deferred function monitors if any port has this problem and retry
 * online one more time.
 */
static void retimer_check_online(void)
{
	int i;

	for (i = 0; i < CONFIG_USB_PD_PORT_MAX_COUNT; i++) {
		if (retimer_fw_update_get_port_state(i) ==
		    RETIMER_ONLINE_REQUESTED) {
			/*
			 * Now the time has passed RETIMTER_ONLINE_DELAY;
			 * retry online.
			 * The port is suspended; if the port is not
			 * suspended, DISCONNECT request won't go through,
			 * we couldn't be here.
			 */
			retry_online(i);
			/* PD port is resumed */
		}
	}
}
DECLARE_DEFERRED(retimer_check_online);

/* Allow mux results to be filled in during HOOKS if needed */
static void last_result_mux_get(void);
DECLARE_DEFERRED(last_result_mux_get);

static void last_result_mux_get(void)
{
	if (query_usb_mux_set_completed_timeout(cur_port)) {
		last_result = USB_RETIMER_FW_UPDATE_ERR;
		return;
	}

	last_result = retimer_fw_update_usb_mux_get(cur_port);
}

void usb_retimer_fw_update_process_op_cb(int port)
{
	bool result_mux_get = false;

	if (port != cur_port) {
		CPRINTS("Unexpected FW op: port %d, cur %d", port, cur_port);
		return;
	}

	switch (last_op) {
	case USB_RETIMER_FW_UPDATE_SUSPEND_PD:
		last_result = 0;
		/*
		 * Do not perform retimer firmware update process
		 * if battery is not present, or battery level is low.
		 */
		if (!pd_firmware_upgrade_check_power_readiness(port)) {
			last_result = USB_RETIMER_FW_UPDATE_ERR;
			break;
		}

		/*
		 * If the port has entered low power mode, the PD task
		 * is paused and will not complete processing of
		 * pd_set_suspend(). Move pd_set_suspend() into a deferred
		 * call so that it runs from the HOOKS task and can generate
		 * a wake event to the PD task and enter suspended mode.
		 */
		hook_call_deferred(&deferred_pd_suspend_data, 0);
		break;
	case USB_RETIMER_FW_UPDATE_RESUME_PD:
		retimer_fw_update_port_handler(port, RESUME);
		break;
	case USB_RETIMER_FW_UPDATE_GET_MUX:
		result_mux_get = true;
		break;
	case USB_RETIMER_FW_UPDATE_SET_USB:
		usb_mux_set(port, USB_PD_MUX_USB_ENABLED, USB_SWITCH_CONNECT,
			    pd_get_polarity(port));
		result_mux_get = true;
		break;
	case USB_RETIMER_FW_UPDATE_SET_SAFE:
		usb_mux_set_safe_mode(port);
		result_mux_get = true;
		break;
	case USB_RETIMER_FW_UPDATE_SET_TBT:
		usb_mux_set(port, USB_PD_MUX_TBT_COMPAT_ENABLED,
			    USB_SWITCH_CONNECT, pd_get_polarity(port));
		result_mux_get = true;
		break;
	case USB_RETIMER_FW_UPDATE_DISCONNECT:
		usb_mux_set(port, USB_PD_MUX_NONE, USB_SWITCH_DISCONNECT,
			    pd_get_polarity(port));
		result_mux_get = true;
		/*
		 * Host decides to put retimer online; now disconnects USB MUX
		 * and sets port state to "RETIMER_ONLINE_REQUESTED".
		 */
		retimer_fw_update_set_port_state(port,
						 RETIMER_ONLINE_REQUESTED);
		hook_call_deferred(&retimer_check_online_data,
				   RETIMTER_ONLINE_DELAY);
		break;
	default:
		break;
	}

	/*
	 * Fill in our mux result if available, or set up a deferred retrieval
	 * if the set is still pending.
	 */
	if (result_mux_get)
		last_result_mux_get();
}

void usb_retimer_fw_update_process_op(int port, int op)
{
	ASSERT(port >= 0 && port < CONFIG_USB_PD_PORT_MAX_COUNT);

	/*
	 * The order of requests from host are:
	 *
	 * Port 0 offline
	 * Port 0 rescan retimers
	 * Port 1 offline
	 * Port 1 rescan retimers
	 * ...
	 * Port 0 online
	 * Port 1 online
	 * ...
	 */
	last_op = op;
	cur_port = port;

	/*
	 * Host has requested to put this port back online, and haven't
	 * finished online process. During this period, don't accept any
	 * requests, except USB_RETIMER_FW_UPDATE_RESUME_PD.
	 */
	if (port_state[port] == RETIMER_ONLINE_REQUESTED) {
		if (op != USB_RETIMER_FW_UPDATE_RESUME_PD) {
			last_result = USB_RETIMER_FW_UPDATE_ERR;
			return;
		}
	}

	switch (op) {
	case USB_RETIMER_FW_UPDATE_QUERY_PORT:
		break;
	/* Operations can't be processed in ISR, defer to later */
	case USB_RETIMER_FW_UPDATE_GET_MUX:
		last_result = USB_RETIMER_FW_UPDATE_INVALID_MUX;
		tc_usb_firmware_fw_update_run(port);
		break;
	case USB_RETIMER_FW_UPDATE_SUSPEND_PD:
	case USB_RETIMER_FW_UPDATE_RESUME_PD:
		tc_usb_firmware_fw_update_run(port);
		break;
	case USB_RETIMER_FW_UPDATE_SET_USB:
	case USB_RETIMER_FW_UPDATE_SET_SAFE:
	case USB_RETIMER_FW_UPDATE_SET_TBT:
	case USB_RETIMER_FW_UPDATE_DISCONNECT:
		if (pd_is_port_enabled(port)) {
			last_result = USB_RETIMER_FW_UPDATE_ERR;
		} else {
			last_result = USB_RETIMER_FW_UPDATE_INVALID_MUX;
			tc_usb_firmware_fw_update_limited_run(port);
		}
		break;
	default:
		break;
	}
}

/*
 * If due to any reason system shuts down during firmware update, resume
 * the PD port; otherwise, PD port is suspended even system powers up again.
 * In normal case, system should not allow shutdown during firmware update.
 */
static void restore_port(void)
{
	int port;

	for (port = 0; port < CONFIG_USB_PD_PORT_MAX_COUNT; port++) {
		if (retimer_fw_update_get_port_state(port))
			retimer_fw_update_port_handler(port, RESUME);
	}
}
DECLARE_HOOK(HOOK_CHIPSET_SHUTDOWN, restore_port, HOOK_PRIO_DEFAULT);
DECLARE_HOOK(HOOK_CHIPSET_RESET, restore_port, HOOK_PRIO_DEFAULT);
