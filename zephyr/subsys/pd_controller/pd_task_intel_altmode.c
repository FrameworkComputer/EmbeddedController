/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * Source file for PD task to configure USB-C Alternate modes on Intel SoC.
 */

#include "i2c.h"
#include "i2c/i2c.h"
#include "usb_mux.h"
#include "usb_pd.h"
#include "usbc/utils.h"

#include <stdlib.h>

#include <zephyr/logging/log.h>
#include <zephyr/shell/shell.h>

#include <ap_power/ap_power.h>
#include <ap_power/ap_power_interface.h>
#include <drivers/intel_altmode.h>
#include <usbc/pd_task_intel_altmode.h>
#include <usbc/pdc_power_mgmt.h>

LOG_MODULE_DECLARE(usbpd_altmode, CONFIG_USB_PD_ALTMODE_LOG_LEVEL);

#define INTEL_ALTMODE_COMPAT_PD intel_pd_altmode

#define PD_CHIP_ENTRY(usbc_id, pd_id, config_fn) \
	[USBC_PORT_NEW(usbc_id)] = config_fn(pd_id),

#define CHECK_COMPAT(compat, usbc_id, pd_id, config_fn) \
	COND_CODE_1(DT_NODE_HAS_COMPAT(pd_id, compat),  \
		    (PD_CHIP_ENTRY(usbc_id, pd_id, config_fn)), ())

#define PD_CHIP_FIND(usbc_id, pd_id) \
	CHECK_COMPAT(INTEL_ALTMODE_COMPAT_PD, usbc_id, pd_id, DEVICE_DT_GET)

#define PD_CHIP(usbc_id)                                                    \
	COND_CODE_1(DT_NODE_HAS_PROP(usbc_id, alt_mode),                    \
		    (PD_CHIP_FIND(usbc_id, DT_PHANDLE(usbc_id, alt_mode))), \
		    ())

#define INTEL_ALTMODE_EVENT_MASK GENMASK(INTEL_ALTMODE_EVENT_COUNT - 1, 0)

enum intel_altmode_event {
	INTEL_ALTMODE_EVENT_FORCE,
	INTEL_ALTMODE_EVENT_INTERRUPT,
	INTEL_ALTMODE_EVENT_COUNT
};

struct usb_mux_info_t {
	mux_state_t mux_mode;
	enum usb_switch usb_mode;
	int polarity;
};

struct intel_altmode_data {
	/* Driver event object to receive events posted. */
	struct k_event evt;
	/* Callback for the AP power events */
	struct ap_power_ev_callback cb;
	/* Cache the dta status register */
	union data_status_reg data_status[CONFIG_USB_PD_PORT_MAX_COUNT];
	/*
	 * Used in polling mode to synchronize mux_state with PDC attached
	 * state
	 */
	struct usb_mux_info_t mux_pending[CONFIG_USB_PD_PORT_MAX_COUNT];
};

/* Generate device tree for available PDs */
static const struct device *pd_config_array[] = { DT_FOREACH_STATUS_OKAY(
	named_usbc_port, PD_CHIP) };

BUILD_ASSERT(ARRAY_SIZE(pd_config_array) == CONFIG_USB_PD_PORT_MAX_COUNT);

/* Store the task data */
static struct intel_altmode_data intel_altmode_task_data;

/*
 *Store pd_intel_altmode_task thread state- suspended(false) or resumed(true)
 */
static bool thread_state;

static void intel_altmode_post_event(enum intel_altmode_event event)
{
	k_event_post(&intel_altmode_task_data.evt, BIT(event));
}

static void intel_altmode_suspend_handler(struct ap_power_ev_callback *cb,
					  struct ap_power_ev_data data)
{
	LOG_DBG("ALTMODE: suspend event: %d, 0x%x", __builtin_ctz(data.event),
		data.event);

	/* The retimer is only powered in in S3 and above.
	 * Disable the altmode thread while in S5 or below.
	 */
	if (data.event == AP_POWER_STARTUP) {
		resume_pd_intel_altmode_task();
	} else if (data.event == AP_POWER_SHUTDOWN) {
		suspend_pd_intel_altmode_task();
	} else {
		LOG_ERR("Invalid suspend event");
	}
}

static void intel_altmode_event_cb(void)
{
	intel_altmode_post_event(INTEL_ALTMODE_EVENT_INTERRUPT);
}

static uint32_t intel_altmode_wait_event(void)
{
	uint32_t events;

	events = k_event_wait(&intel_altmode_task_data.evt,
			      INTEL_ALTMODE_EVENT_MASK, false, K_FOREVER);

	/* Clear all events posted */
	k_event_clear(&intel_altmode_task_data.evt, events);

	return events & INTEL_ALTMODE_EVENT_MASK;
}

static void intel_altmode_set_mux(int port, mux_state_t mux,
				  enum usb_switch usb_mode, int polarity)
{
	LOG_INF("Set p%d mux=0x%x", port, mux);
	usb_mux_set(port, mux, usb_mode, polarity);
}

static bool process_altmode_pd_data(int port)
{
	int rv;
	union data_status_reg status;
	mux_state_t mux = USB_PD_MUX_NONE;
	union data_status_reg *prev_status =
		&intel_altmode_task_data.data_status[port];
	union data_control_reg control = { .i2c_int_ack = 1 };
	struct usb_mux_info_t *mux_pend =
		&intel_altmode_task_data.mux_pending[port];
	enum usb_switch usb_mode;
#ifdef CONFIG_PLATFORM_EC_USB_PD_DP_MODE
	bool prv_hpd_lvl;
#endif

/* TODO(b/325624686): The polling mechanism is temporary and will be removed
 * after the bug is resolved */
#if !CONFIG_USBPD_POLL_PDC
	LOG_INF("Process p%d data", port);
#endif
	/* Clear the interrupt */
	rv = pd_altmode_write_control(pd_config_array[port], &control);
	if (rv) {
		LOG_ERR("P%d write Err=%d", port, rv);
		return false;
	}

	/* Read the status register */
	rv = pd_altmode_read_status(pd_config_array[port], &status);
	if (rv) {
		LOG_ERR("P%d read Err=%d", port, rv);
		return false;
	}

#ifdef CONFIG_PLATFORM_EC_USB_PD_DP_MODE
	/* Store previous HPD tatus */
	prv_hpd_lvl = prev_status->hpd_lvl;
#endif

	/* Nothing to do if the data in the status register has not changed */
	if (!memcmp(&status.raw_value[0], prev_status,
		    sizeof(union data_status_reg))) {
		/* Nothing to do if mux isn't pending */
		if (mux_pend->mux_mode == USB_PD_MUX_NONE) {
			return false;
		}

		/* Mux is pending. Make sure a connection is established */
		if (pdc_power_mgmt_is_connected(port) ||
		    /* Retimer Firmware update NDA case */
		    (!pdc_power_mgmt_is_connected(port) &&
		     mux_pend->mux_mode == USB_PD_MUX_TBT_COMPAT_ENABLED)) {
			intel_altmode_set_mux(port, mux_pend->mux_mode,
					      mux_pend->usb_mode,
					      mux_pend->polarity);
			/* Clear mux state so it's no longer pending */
			mux_pend->mux_mode = USB_PD_MUX_NONE;
			return false;
		}

		/* Mux is pending but a connection hasn't been established */
		return true;
	}

	/* Update the new data */
	memcpy(prev_status, &status, sizeof(union data_status_reg));

	/* Log changes to aid in debugging.  MSB printed first. */
	LOG_INF("P%d DATA_STATUS: %02x %02x %02x %02x %02x", port,
		status.raw_value[4], status.raw_value[3], status.raw_value[2],
		status.raw_value[1], status.raw_value[0]);

	/* Process MUX events */

	/* Orientation */
	if (status.conn_ori)
		mux |= USB_PD_MUX_POLARITY_INVERTED;

	/* USB status */
	if (status.usb2 || status.usb3_2)
		mux |= USB_PD_MUX_USB_ENABLED;

#ifdef CONFIG_PLATFORM_EC_USB_PD_DP_MODE
	/* DP status */
	if (status.dp)
		mux |= USB_PD_MUX_DP_ENABLED;

	if (status.hpd_lvl)
		mux |= USB_PD_MUX_HPD_LVL;

	if (status.dp_irq)
		mux |= USB_PD_MUX_HPD_IRQ;
#endif

#ifdef CONFIG_PLATFORM_EC_USB_PD_TBT_COMPAT_MODE
	/* TBT status */
	if (status.tbt)
		mux |= USB_PD_MUX_TBT_COMPAT_ENABLED;
#endif

#ifdef CONFIG_PLATFORM_EC_USB_PD_USB4
	if (status.usb4)
		mux |= USB_PD_MUX_USB4_ENABLED;
#endif

	if (mux == USB_PD_MUX_NONE || mux == USB_PD_MUX_POLARITY_INVERTED) {
		usb_mode = USB_SWITCH_DISCONNECT;
		mux = USB_PD_MUX_NONE;
	} else {
		usb_mode = USB_SWITCH_CONNECT;
	}

	/*
	 * If the new desired mux state is USB_PD_MUX_NONE, then there is no
	 * current connection and this setting can be applied
	 * immediately. However, other mux states imply the port is
	 * connected. usb_set_mux() will notify the AP and the AP will then
	 * query the port status via HC 0x0101. Setting the mux needs to be
	 * delayed until the PDC API has had time to query connector_status and
	 * updated its connected/disconnected status.
	 *
	 * TODO(b/325090383): Reevaluate if this synchronization is required
	 * once type-c events are being reported to the AP.
	 */
	if (usb_mode == USB_SWITCH_DISCONNECT) {
		intel_altmode_set_mux(port, mux, usb_mode, status.conn_ori);
	}
	/*
	 * Save the desired mux state always. If the desired mux state is NONE,
	 * then updating these values is a don't care. Otherwise, the mux set
	 * needs to happen in conjunction with the PDC connected state which is
	 * checked above.
	 */
	mux_pend->mux_mode = mux;
	mux_pend->usb_mode = usb_mode;
	mux_pend->polarity = status.conn_ori;

#ifdef CONFIG_PLATFORM_EC_USB_PD_DP_MODE
	/* Update the change in HPD level */
	if (prv_hpd_lvl != status.hpd_lvl)
		usb_mux_hpd_update(port,
				   status.hpd_lvl ? USB_PD_MUX_HPD_LVL : 0);
#endif
	return true;
}

static void intel_altmode_thread(void *unused1, void *unused2, void *unused3)
{
	int i;
	uint32_t events;

	LOG_INF("Intel Altmode thread init");

	/* Initialize events */
	k_event_init(&intel_altmode_task_data.evt);

	/* Add callbacks for suspend hooks */
	ap_power_ev_init_callback(&intel_altmode_task_data.cb,
				  intel_altmode_suspend_handler,
				  AP_POWER_STARTUP | AP_POWER_SHUTDOWN);
	ap_power_ev_add_callback(&intel_altmode_task_data.cb);

	/* Register PD interrupt callback */
	for (i = 0; i < CONFIG_USB_PD_PORT_MAX_COUNT; i++)
		pd_altmode_set_result_cb(pd_config_array[i],
					 intel_altmode_event_cb);

	/* If the AP is off, wait until it's powered up before entering the
	 * processing loop.
	 */
	if (ap_power_in_state(AP_POWER_STATE_ANY_OFF)) {
		LOG_INF("Intel Altmode: wait for AP power up");
		events = k_event_wait(&intel_altmode_task_data.evt,
				      BIT(INTEL_ALTMODE_EVENT_FORCE), false,
				      K_FOREVER);

		/* Clear all events posted */
		k_event_clear(&intel_altmode_task_data.evt, events);
	} else {
		/*
		 * AP already powered up.  We probably just did a sysjump.
		 * Trigger an update to the muxconfig.
		 */
		events = BIT(INTEL_ALTMODE_EVENT_FORCE);
	}

	LOG_INF("Intel Altmode thread start");

	while (1) {
		/*
		 * Process the forced event first so that they are not
		 * overlooked in the if-else conditions.
		 */
		if (events & BIT(INTEL_ALTMODE_EVENT_FORCE)) {
			/* Process data for any wake events on all ports */
			for (i = 0; i < CONFIG_USB_PD_PORT_MAX_COUNT; i++) {
				while (process_altmode_pd_data(i)) {
					k_msleep(25);
				}
			}
		} else if (events & BIT(INTEL_ALTMODE_EVENT_INTERRUPT)) {
			/* Process data of interrupted port */
			for (i = 0; i < CONFIG_USB_PD_PORT_MAX_COUNT; i++) {
				if (pd_altmode_is_interrupted(
					    pd_config_array[i])) {
					while (process_altmode_pd_data(i)) {
						k_msleep(25);
					}
				}
			}
		}
#if CONFIG_USBPD_POLL_PDC
		k_msleep(50);
		events = BIT(INTEL_ALTMODE_EVENT_FORCE);
#else
		events = intel_altmode_wait_event();
		LOG_DBG("Altmode events=0x%x", events);
#endif
	}
}

K_THREAD_DEFINE(intel_altmode_tid, CONFIG_TASK_PD_ALTMODE_INTEL_STACK_SIZE,
		intel_altmode_thread, NULL, NULL, NULL,
		CONFIG_USBPD_ALTMODE_INTEL_THREAD_PRIORITY, 0, SYS_FOREVER_MS);

void intel_altmode_task_start(void)
{
	k_thread_start(intel_altmode_tid);

	/* Resume thread state */
	thread_state = true;
}

void suspend_pd_intel_altmode_task(void)
{
	k_thread_suspend(intel_altmode_tid);

	/* Suspend thread state */
	thread_state = false;
}

void resume_pd_intel_altmode_task(void)
{
	k_thread_resume(intel_altmode_tid);

	/* Resume thread state */
	thread_state = true;

	/*
	 * Suspended PD altmode task can miss the altmode events.
	 * Therefore, explicitly post event so PD altmode task updates
	 * the mux status after resuming.
	 */
	intel_altmode_post_event(INTEL_ALTMODE_EVENT_FORCE);
}

bool is_pd_intel_altmode_task_suspended(void)
{
	return !thread_state;
}

#ifdef CONFIG_CONSOLE_CMD_USBPD_INTEL_ALTMODE
static int cmd_get_pd_port(const struct shell *sh, char *arg_val, uint8_t *port)
{
	char *e;

	*port = strtoul(arg_val, &e, 0);
	if (*e || *port >= CONFIG_USB_PD_PORT_MAX_COUNT) {
		shell_error(sh, "Invalid port");
		return -EINVAL;
	}

	return 0;
}

static int cmd_altmode_read(const struct shell *sh, size_t argc, char **argv)
{
	int rv, i;
	uint8_t port;
	union data_status_reg status;

	/* Get PD port number */
	rv = cmd_get_pd_port(sh, argv[1], &port);
	if (rv)
		return rv;

	/* Read from status register */
	rv = pd_altmode_read_status(pd_config_array[port], &status);
	if (rv) {
		shell_error(sh, "Read failed, rv=%d", rv);
		return rv;
	}

	shell_fprintf(sh, SHELL_INFO, "DATA_STATUS (msb-lsb): ");
	for (i = INTEL_ALTMODE_DATA_STATUS_REG_LEN - 1; i >= 0; i--)
		shell_fprintf(sh, SHELL_INFO, "%02x ", status.raw_value[i]);

	shell_info(sh, "");

	return 0;
}

static int cmd_altmode_write(const struct shell *sh, size_t argc, char **argv)
{
	char *e;
	int rv, i;
	uint8_t port;
	uint16_t val;
	union data_control_reg control = { 0 };

	/* Get PD port number */
	rv = cmd_get_pd_port(sh, argv[1], &port);
	if (rv)
		return rv;

	/* Fill the control register with data */
	for (i = 2; i < argc; i++) {
		val = strtoul(argv[i], &e, 0);
		if (*e || val > UINT8_MAX) {
			shell_error(sh, "Invalid data, %s", argv[i]);
			return -EINVAL;
		}
		control.raw_value[i - 2] = (uint8_t)val;
	}

	/* Write to control register */
	rv = pd_altmode_write_control(pd_config_array[port], &control);
	if (rv)
		shell_error(sh, "Write failed, rv=%d", rv);

	return rv;
}

SHELL_STATIC_SUBCMD_SET_CREATE(
	sub_altmode_cmds,
	SHELL_CMD_ARG(read, NULL,
		      "Read status register\n"
		      "Usage: altmode read <port>",
		      cmd_altmode_read, 2, 1),
	SHELL_CMD_ARG(write, NULL,
		      "Write control register\n"
		      "Usage: altmode write <port> [<byte0>, ...]",
		      cmd_altmode_write, 3,
		      INTEL_ALTMODE_DATA_CONTROL_REG_LEN - 1),
	SHELL_SUBCMD_SET_END);

SHELL_CMD_REGISTER(altmode, &sub_altmode_cmds, "PD Altmode commands", NULL);

#endif /* CONFIG_CONSOLE_CMD_USBPD_INTEL_ALTMODE */

#ifdef CONFIG_PLATFORM_EC_USB_PD_TBT_COMPAT_MODE
enum tbt_compat_cable_speed get_tbt_cable_speed(int port)
{
	return intel_altmode_task_data.data_status[port].cable_speed;
}

enum tbt_compat_rounded_support get_tbt_rounded_support(int port)
{
	return intel_altmode_task_data.data_status[port].cable_gen;
}
#endif

#ifdef CONFIG_COMMON_RUNTIME
/*
 * Combines the following information into a single byte
 * Bit 0: Active/Passive cable
 * Bit 1: Optical/Non-optical cable
 * Bit 2: Legacy Thunderbolt adapter
 * Bit 3: Active Link Uni-Direction/Bi-Direction
 * Bit 4: Retimer/Rediriver cable
 */
uint8_t get_pd_control_flags(int port)
{
	uint8_t control_flags = 0;

	control_flags |=
		intel_altmode_task_data.data_status[port].active_passive ==
				TBT_CABLE_ACTIVE ?
			USB_PD_CTRL_ACTIVE_CABLE :
			0;
	control_flags |= intel_altmode_task_data.data_status[port].cable_type ==
					 TBT_CABLE_OPTICAL ?
				 USB_PD_CTRL_OPTICAL_CABLE :
				 0;
	control_flags |= intel_altmode_task_data.data_status[port].tbt_type ==
					 TBT_ADAPTER_TBT2_LEGACY ?
				 USB_PD_CTRL_TBT_LEGACY_ADAPTER :
				 0;
	control_flags |= intel_altmode_task_data.data_status[port].usb4_tbt_lt ?
				 USB_PD_CTRL_ACTIVE_LINK_UNIDIR :
				 0;
	control_flags |= intel_altmode_task_data.data_status[port].ret_redrv ==
					 USB_RETIMER ?
				 USB_PD_CTRL_RETIMER_CABLE :
				 0;

	return control_flags;
}
#endif
