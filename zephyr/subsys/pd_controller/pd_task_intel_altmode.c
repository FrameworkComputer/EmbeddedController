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
#include <drivers/intel_altmode.h>
#include <usbc/pd_task_intel_altmode.h>

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

struct intel_altmode_data {
	/* Driver event object to receive events posted. */
	struct k_event evt;
	/* Callback for the AP power events */
	struct ap_power_ev_callback cb;
	/* Cache the dta status register */
	union data_status_reg data_status[CONFIG_USB_PD_PORT_MAX_COUNT];
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
	LOG_DBG("suspend event: 0x%x", data.event);

	if (data.event == AP_POWER_RESUME) {
		/*
		 * Set event to forcefully get new PD data.
		 * This ensures EC doesn't miss the interrupt if the interrupt
		 * pull-ups are on A-rail.
		 */
		intel_altmode_post_event(INTEL_ALTMODE_EVENT_FORCE);
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
			      INTEL_ALTMODE_EVENT_MASK, false, Z_FOREVER);

	/* Clear all events posted */
	k_event_clear(&intel_altmode_task_data.evt, events);

	return events & INTEL_ALTMODE_EVENT_MASK;
}

static void process_altmode_pd_data(int port)
{
	int rv;
	union data_status_reg status;
	mux_state_t mux = USB_PD_MUX_NONE;
	union data_status_reg *prev_status =
		&intel_altmode_task_data.data_status[port];
	union data_control_reg control = { .i2c_int_ack = 1 };
#ifdef CONFIG_PLATFORM_EC_USB_PD_DP_MODE
	bool prv_hpd_lvl;
#endif

	LOG_INF("Process p%d data", port);

	/* Clear the interrupt */
	rv = pd_altmode_write_control(pd_config_array[port], &control);
	if (rv) {
		LOG_ERR("P%d write Err=%d", port, rv);
		return;
	}

	/* Read the status register */
	rv = pd_altmode_read_status(pd_config_array[port], &status);
	if (rv) {
		LOG_ERR("P%d read Err=%d", port, rv);
		return;
	}

#ifdef CONFIG_PLATFORM_EC_USB_PD_DP_MODE
	/* Store previous HPD tatus */
	prv_hpd_lvl = prev_status->hpd_lvl;
#endif

	/* Nothing to do if the data in the status register has not changed */
	if (!memcmp(&status.raw_value[0], prev_status,
		    sizeof(union data_status_reg)))
		return;

	/* Update the new data */
	memcpy(prev_status, &status, sizeof(union data_status_reg));

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

	LOG_INF("Set p%d mux=0x%x", port, mux);

	usb_mux_set(port, mux,
		    mux == USB_PD_MUX_NONE ? USB_SWITCH_DISCONNECT :
					     USB_SWITCH_CONNECT,
		    status.conn_ori);

#ifdef CONFIG_PLATFORM_EC_USB_PD_DP_MODE
	/* Update the change in HPD level */
	if (prv_hpd_lvl != status.hpd_lvl)
		usb_mux_hpd_update(port,
				   status.hpd_lvl ? USB_PD_MUX_HPD_LVL : 0);
#endif
}

static void intel_altmode_thread(void *unused1, void *unused2, void *unused3)
{
	int i;
	uint32_t events;

	/* Initialize events */
	k_event_init(&intel_altmode_task_data.evt);

	/* Add callbacks for suspend hooks */
	ap_power_ev_init_callback(&intel_altmode_task_data.cb,
				  intel_altmode_suspend_handler,
				  AP_POWER_RESUME);
	ap_power_ev_add_callback(&intel_altmode_task_data.cb);

	/* Register PD interrupt callback */
	for (i = 0; i < CONFIG_USB_PD_PORT_MAX_COUNT; i++)
		pd_altmode_set_result_cb(pd_config_array[i],
					 intel_altmode_event_cb);

	LOG_INF("Intel Altmode thread start");

	while (1) {
		events = intel_altmode_wait_event();

		LOG_DBG("Altmode events=0x%x", events);

		if (events & BIT(INTEL_ALTMODE_EVENT_INTERRUPT)) {
			/* Process data of interrupted port */
			for (i = 0; i < CONFIG_USB_PD_PORT_MAX_COUNT; i++) {
				if (pd_altmode_is_interrupted(
					    pd_config_array[i]))
					process_altmode_pd_data(i);
			}
		} else if (events & BIT(INTEL_ALTMODE_EVENT_FORCE)) {
			/* Process data for any wake events on all ports */
			for (i = 0; i < CONFIG_USB_PD_PORT_MAX_COUNT; i++)
				process_altmode_pd_data(i);
		}
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

	shell_fprintf(sh, SHELL_INFO, "RD_VAL: ");
	for (i = 0; i < INTEL_ALTMODE_DATA_STATUS_REG_LEN; i++)
		shell_fprintf(sh, SHELL_INFO, "[%d]0x%x, ", i,
			      status.raw_value[i]);

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

/*
 * TODO: For all the below functions; need to enable PD to EC power path
 * interface and gather the information.
 */
enum tcpc_cc_polarity pd_get_polarity(int port)
{
	return intel_altmode_task_data.data_status[port].conn_ori;
}

enum pd_data_role pd_get_data_role(int port)
{
	return !intel_altmode_task_data.data_status[port].data_role;
}

int pd_is_connected(int port)
{
	return intel_altmode_task_data.data_status[port].data_conn;
}

#ifdef CONFIG_PLATFORM_EC_USB_PD_DP_MODE
__override uint8_t get_dp_pin_mode(int port)
{
	return intel_altmode_task_data.data_status[port].dp_pin << 2;
}
#endif

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

/*
 * To suppress the compilation error, below functions are added with tested
 * data.
 */
void pd_request_data_swap(int port)
{
}

enum pd_power_role pd_get_power_role(int port)
{
	return !intel_altmode_task_data.data_status[port].dp_src_snk;
}

uint8_t pd_get_task_state(int port)
{
	return 0;
}

int pd_comm_is_enabled(int port)
{
	return 1;
}

bool pd_get_vconn_state(int port)
{
	return true;
}

bool pd_get_partner_dual_role_power(int port)
{
	return false;
}

bool pd_get_partner_data_swap_capable(int port)
{
	return false;
}

bool pd_get_partner_usb_comm_capable(int port)
{
	return false;
}

bool pd_get_partner_unconstr_power(int port)
{
	return false;
}

const char *pd_get_task_state_name(int port)
{
	return "";
}

enum pd_cc_states pd_get_task_cc_state(int port)
{
	return PD_CC_UFP_ATTACHED;
}

bool pd_capable(int port)
{
	return true;
}
