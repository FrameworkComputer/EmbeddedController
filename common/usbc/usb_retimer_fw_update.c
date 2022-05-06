/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <stdbool.h>
#include <stdint.h>
#include "compile_time_macros.h"
#include "console.h"
#include "hooks.h"
#include "timer.h"
#include "usb_common.h"
#include "usb_mux.h"
#include "usb_tc_sm.h"

#ifdef CONFIG_COMMON_RUNTIME
#define CPRINTS(format, args...) cprints(CC_USBPD, format, ## args)
#define CPRINTF(format, args...) cprintf(CC_USBPD, format, ## args)
#else
#define CPRINTS(format, args...)
#define CPRINTF(format, args...)
#endif

/*
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
 */

#define SUSPEND 1
#define RESUME  0

/* Track current port AP requested to update retimer firmware */
static int cur_port;
static int last_op; /* Operation received from AP via ACPI_WRITE */
/* Operation result returned to ACPI_READ */
static int last_result;
/* Track port state: SUSPEND or RESUME */
static int port_state[CONFIG_USB_PD_PORT_MAX_COUNT];

int usb_retimer_fw_update_get_result(void)
{
	int result = 0;

	switch (last_op) {
	case USB_RETIMER_FW_UPDATE_SUSPEND_PD:
		if (last_result == USB_RETIMER_FW_UPDATE_ERR) {
			result = last_result;
			break;
		}
		/* fall through */
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

static void retimer_fw_update_set_port_state(int port, int state)
{
	port_state[port] = state;
}

static int retimer_fw_update_get_port_state(int port)
{
	return port_state[port];
}

/**
 * @brief Suspend or resume PD task and update the state of the port.
 *
 * @param port PD port
 * @param state
 * SUSPEND: suspend PD task for firmware update; and set state to SUSPEND
 * RESUME: resume PD task after firmware update is done; and set state
 * to RESUME.
 *
 */
static void retimer_fw_update_port_handler(int port, int state)
{
	pd_set_suspend(port, state);
	retimer_fw_update_set_port_state(port, state);
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

/* Allow mux results to be filled in during HOOKS if needed */
static void last_result_mux_get(void);
DECLARE_DEFERRED(last_result_mux_get);

static void last_result_mux_get(void)
{
	if (!usb_mux_set_completed(cur_port)) {
		hook_call_deferred(&last_result_mux_get_data, 20 * MSEC);
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
		usb_mux_set(port, USB_PD_MUX_USB_ENABLED,
			USB_SWITCH_CONNECT, pd_get_polarity(port));
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
		usb_mux_set(port, USB_PD_MUX_NONE,
			USB_SWITCH_DISCONNECT, pd_get_polarity(port));
		result_mux_get = true;
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
	 * TODO(b/179220036): check not overlapping requests;
	 * not change cur_port if retimer scan is in progress
	 */
	last_op = op;
	cur_port = port;

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

	for  (port = 0; port < CONFIG_USB_PD_PORT_MAX_COUNT; port++) {
		if (retimer_fw_update_get_port_state(port))
			retimer_fw_update_port_handler(port, RESUME);
	}
}
DECLARE_HOOK(HOOK_CHIPSET_SHUTDOWN, restore_port, HOOK_PRIO_DEFAULT);
