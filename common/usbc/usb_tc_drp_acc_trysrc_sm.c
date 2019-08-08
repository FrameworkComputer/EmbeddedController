/* Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "charge_manager.h"
#include "charge_state.h"
#include "common.h"
#include "console.h"
#include "hooks.h"
#include "system.h"
#include "task.h"
#include "tcpm.h"
#include "usb_common.h"
#include "usb_mux.h"
#include "usb_pd.h"
#include "usb_tc_drp_acc_trysrc_sm.h"
#include "usb_tc_sm.h"
#include "usbc_ppc.h"

/*
 * USB Type-C DRP with Accessory and Try.SRC module
 *   See Figure 4-16 in Release 1.4 of USB Type-C Spec.
 */

#ifdef CONFIG_COMMON_RUNTIME
#define CPRINTF(format, args...) cprintf(CC_HOOK, format, ## args)
#define CPRINTS(format, args...) cprints(CC_HOOK, format, ## args)
#else /* CONFIG_COMMON_RUNTIME */
#define CPRINTF(format, args...)
#define CPRINTS(format, args...)
#endif

/* Type-C Layer Flags */
#define TC_FLAGS_VCONN_ON                 BIT(0)
#define TC_FLAGS_TS_DTS_PARTNER           BIT(1)
#define TC_FLAGS_VBUS_NEVER_LOW           BIT(2)
#define TC_FLAGS_LPM_TRANSITION           BIT(3)
#define TC_FLAGS_LPM_ENGAGED              BIT(4)
#define TC_FLAGS_LPM_REQUESTED            BIT(5)


struct type_c tc[CONFIG_USB_PD_PORT_COUNT];

/* Port dual-role state */
enum pd_dual_role_states drp_state[CONFIG_USB_PD_PORT_COUNT] = {
	[0 ... (CONFIG_USB_PD_PORT_COUNT - 1)] =
		CONFIG_USB_PD_INITIAL_DRP_STATE};

/*
 * 4 entry rw_hash table of type-C devices that AP has firmware updates for.
 */
#ifdef CONFIG_COMMON_RUNTIME
#define RW_HASH_ENTRIES 4
static struct ec_params_usb_pd_rw_hash_entry rw_hash_table[RW_HASH_ENTRIES];
#endif

static void tc_set_data_role(int port, int role);

#ifdef CONFIG_USB_PD_TRY_SRC
/* Enable variable for Try.SRC states */
static uint8_t pd_try_src_enable;
static void pd_update_try_source(void);
#endif

/*
 * Type-C State Hierarchy (Sub-States are listed inside the boxes)
 *
 * |TC_CC_RD --------------|	|TC_CC_RP ------------------------|
 * |			   |	|				  |
 * |	TC_UNATTACHED_SNK  |	|	TC_UNATTACHED_SRC         |
 * |	TC_ATTACH_WAIT_SNK |	|	TC_ATTACH_WAIT_SRC        |
 * |	TC_TRY_WAIT_SNK    |	|	TC_TRY_SRC                |
 * |	TC_DBG_ACC_SNK     |	|	TC_UNORIENTED_DBG_ACC_SRC |
 * |-----------------------|	|---------------------------------|
 *
 * |TC_CC_OPEN -----------|
 * |                      |
 * |	TC_DISABLED       |
 * |	TC_ERROR_RECOVERY |
 * |----------------------|
 *
 * TC_ATTACHED_SNK		TC_ATTACHED_SRC
 *
 */

/*
 * Type-C states
 */
DECLARE_STATE(tc, disabled, WITH_RUN, WITH_EXIT);
DECLARE_STATE(tc, error_recovery, WITH_RUN, NOOP);
DECLARE_STATE(tc, unattached_snk, WITH_RUN, NOOP);
DECLARE_STATE(tc, attach_wait_snk, WITH_RUN, NOOP);
DECLARE_STATE(tc, attached_snk, WITH_RUN, WITH_EXIT);
DECLARE_STATE(tc, dbg_acc_snk, WITH_RUN, NOOP);
DECLARE_STATE(tc, unattached_src, WITH_RUN, NOOP);
DECLARE_STATE(tc, attach_wait_src, WITH_RUN, NOOP);
DECLARE_STATE(tc, attached_src, WITH_RUN, WITH_EXIT);
DECLARE_STATE(tc, unoriented_dbg_acc_src, WITH_RUN, NOOP);

#ifdef CONFIG_USB_PD_TRY_SRC
DECLARE_STATE(tc, try_src, WITH_RUN, NOOP);
DECLARE_STATE(tc, try_wait_snk, WITH_RUN, NOOP);
#endif

/* Super States */
DECLARE_STATE(tc, cc_rd, NOOP, NOOP);
DECLARE_STATE(tc, cc_rp, NOOP, NOOP);
DECLARE_STATE(tc, cc_open, NOOP, NOOP);

/*
 * Public Functions
 *
 * NOTE: Functions prefixed with pd_ are defined in usb_pd.h
 *       Functions prefixed with tc_ are defined int usb_tc_sm.h
 */

#ifndef CONFIG_USB_PRL_SM

/*
 * These pd_ functions are implemented in common/usb_prl_sm.c
 */

void pd_transmit_complete(int port, int status)
{
	/* DO NOTHING */
}

void pd_execute_hard_reset(int port)
{
	/* DO NOTHING */
}

void pd_set_vbus_discharge(int port, int enable)
{
	/* DO NOTHING */
}

uint16_t pd_get_identity_vid(int port)
{
	/* DO NOTHING */
	return 0;
}

#endif /* !defined(CONFIG_USB_PRL_SM) */

void pd_update_contract(int port)
{
	/* DO NOTHING */
}

void pd_set_new_power_request(int port)
{
	/* DO NOTHING */
}

void pd_request_power_swap(int port)
{
	/* DO NOTHING */
}

void pd_set_suspend(int port, int enable)
{
	sm_state state;

	if (pd_is_port_enabled(port) == !enable)
		return;

	if (enable)
		state = tc_disabled;
	else
		state = (TC_DEFAULT_STATE(port) == TC_UNATTACHED_SRC) ?
				tc_unattached_src : tc_unattached_snk;

	sm_set_state(port, TC_OBJ(port), state);
}

int pd_is_port_enabled(int port)
{
	return !(tc[port].state_id == TC_DISABLED);
}

int pd_fetch_acc_log_entry(int port)
{
	return EC_RES_SUCCESS;
}

int pd_get_polarity(int port)
{
	return tc[port].polarity;
}

int pd_get_role(int port)
{
	return tc[port].data_role;
}

int pd_is_vbus_present(int port)
{
	if (IS_ENABLED(CONFIG_USB_PD_VBUS_DETECT_TCPC))
		return tcpm_get_vbus_level(port);
	else
		return pd_snk_is_vbus_provided(port);
}

void pd_vbus_low(int port)
{
	TC_CLR_FLAG(port, TC_FLAGS_VBUS_NEVER_LOW);
}

int pd_is_connected(int port)
{
	return (tc[port].state_id == TC_ATTACHED_SNK) ||
				(tc[port].state_id == TC_ATTACHED_SRC);
}

#ifdef CONFIG_USB_PD_ALT_MODE_DFP
/*
 * TODO(b/137493121): Move this function to a separate file that's shared
 * between the this and the original stack.
 */
void pd_prepare_sysjump(void)
{
	/*
	 * We can't be in an alternate mode since PD comm is disabled, so
	 * no need to send the event
	 */
}
#endif

void tc_src_power_off(int port)
{
	if (tc[port].state_id == TC_ATTACHED_SRC) {
		/* Remove VBUS */
		pd_power_supply_reset(port);

		if (IS_ENABLED(CONFIG_CHARGE_MANAGER)) {
			charge_manager_set_ceil(port, CEIL_REQUESTOR_PD,
							CHARGE_CEIL_NONE);
		}
	}
}

void tc_start_error_recovery(int port)
{
	/*
	 * Async. function call:
	 *   The port should transition to the ErrorRecovery state
	 *   from any other state when directed.
	 */
	sm_set_state(port, TC_OBJ(port), tc_error_recovery);
}

void tc_state_init(int port, enum typec_state_id start_state)
{
	int res = 0;
	sm_state this_state;

	res = tc_restart_tcpc(port);
	if (res)
		this_state = tc_disabled;
	else
		this_state = (start_state == TC_UNATTACHED_SRC) ?
				tc_unattached_src : tc_unattached_snk;

	CPRINTS("TCPC p%d init %s", port, res ? "failed" : "ready");

	sm_init_state(port, TC_OBJ(port), this_state);

	if (IS_ENABLED(CONFIG_USBC_SS_MUX))
		/* Initialize USB mux to its default state */
		usb_mux_init(port);

	tcpm_select_rp_value(port, CONFIG_USB_PD_PULLUP);

	if (IS_ENABLED(CONFIG_CHARGE_MANAGER)) {
		/* Initialize PD and type-C supplier current limits to 0 */
		pd_set_input_current_limit(port, 0, 0);
		typec_set_input_current_limit(port, 0, 0);
		charge_manager_update_dualrole(port, CAP_UNKNOWN);
	}

	tc[port].flags = 0;
	tc[port].evt_timeout = 5*MSEC;
}

/*
 * Private Functions
 */

void tc_event_check(int port, int evt)
{
	/* NO EVENTS TO CHECK */
}

/*
 * CC values for regular sources and Debug sources (aka DTS)
 *
 * Source type  Mode of Operation   CC1    CC2
 * ---------------------------------------------
 * Regular      Default USB Power   RpUSB  Open
 * Regular      USB-C @ 1.5 A       Rp1A5  Open
 * Regular      USB-C @ 3 A         Rp3A0  Open
 * DTS          Default USB Power   Rp3A0  Rp1A5
 * DTS          USB-C @ 1.5 A       Rp1A5  RpUSB
 * DTS          USB-C @ 3 A         Rp3A0  RpUSB
 */

static void tc_set_data_role(int port, int role)
{
	tc[port].data_role = role;

	if (IS_ENABLED(CONFIG_USBC_SS_MUX))
		set_usb_mux_with_current_data_role(port);

	/* Notify TCPC of role update */
	tcpm_set_msg_header(port, tc[port].power_role, tc[port].data_role);
}

static void sink_stop_drawing_current(int port)
{
	pd_set_input_current_limit(port, 0, 0);

	if (IS_ENABLED(CONFIG_CHARGE_MANAGER)) {
		typec_set_input_current_limit(port, 0, 0);
		charge_manager_set_ceil(port,
				CEIL_REQUESTOR_PD, CHARGE_CEIL_NONE);
	}
}

#ifdef CONFIG_USB_PD_TRY_SRC
/*
 * TODO(b/137493121): Move this function to a separate file that's shared
 * between the this and the original stack.
 */
static void pd_update_try_source(void)
{
	int i;
	int try_src = 0;

	int batt_soc = usb_get_battery_soc();

	try_src = 0;
	for (i = 0; i < CONFIG_USB_PD_PORT_COUNT; i++)
		try_src |= drp_state[i] == PD_DRP_TOGGLE_ON;

	/*
	 * Enable try source when dual-role toggling AND battery is present
	 * and at some minimum percentage.
	 */
	pd_try_src_enable = try_src &&
			    batt_soc >= CONFIG_USB_PD_TRY_SRC_MIN_BATT_SOC;

#ifdef CONFIG_BATTERY_REVIVE_DISCONNECT
	/*
	 * Don't attempt Try.Src if the battery is in the disconnect state.  The
	 * discharge FET may not be enabled and so attempting Try.Src may cut
	 * off our only power source at the time.
	 */
	pd_try_src_enable &= (battery_get_disconnect_state() ==
			BATTERY_NOT_DISCONNECTED);
#elif defined(CONFIG_BATTERY_PRESENT_CUSTOM) || \
			defined(CONFIG_BATTERY_PRESENT_GPIO)
	/*
	 * When battery is cutoff in ship mode it may not be reliable to
	 * check if battery is present with its state of charge.
	 * Also check if battery is initialized and ready to provide power.
	 */
	pd_try_src_enable &= (battery_is_present() == BP_YES);
#endif /* CONFIG_BATTERY_PRESENT_[CUSTOM|GPIO] */

}
DECLARE_HOOK(HOOK_BATTERY_SOC_CHANGE, pd_update_try_source, HOOK_PRIO_DEFAULT);
#endif /* CONFIG_USB_PD_TRY_SRC */

#ifdef CONFIG_CMD_PD_DEV_DUMP_INFO
static inline void pd_dev_dump_info(uint16_t dev_id, uint8_t *hash)
{
	int j;

	ccprintf("DevId:%d.%d Hash:", HW_DEV_ID_MAJ(dev_id),
		 HW_DEV_ID_MIN(dev_id));
	for (j = 0; j < PD_RW_HASH_SIZE; j += 4) {
		ccprintf(" 0x%02x%02x%02x%02x", hash[j + 3], hash[j + 2],
			 hash[j + 1], hash[j]);
	}
	ccprintf("\n");
}
#endif /* CONFIG_CMD_PD_DEV_DUMP_INFO */

static void set_vconn(int port, int enable)
{
	if (enable == TC_CHK_FLAG(port, TC_FLAGS_VCONN_ON))
		return;

	if (enable)
		TC_SET_FLAG(port, TC_FLAGS_VCONN_ON);
	else
		TC_CLR_FLAG(port, TC_FLAGS_VCONN_ON);

	/*
	 * We always need to tell the TCPC to enable Vconn first, otherwise some
	 * TCPCs get confused and think the CC line is in over voltage mode and
	 * immediately disconnects. If there is a PPC, both devices will
	 * potentially source Vconn, but that should be okay since Vconn has
	 * "make before break" electrical requirements when swapping anyway.
	 */
	tcpm_set_vconn(port, enable);

	if (IS_ENABLED(CONFIG_USBC_PPC_VCONN))
		ppc_set_vconn(port, enable);
}

#ifdef CONFIG_USB_PD_TCPM_TCPCI
static uint32_t pd_ports_to_resume;
static void resume_pd_port(void)
{
	uint32_t port;
	uint32_t suspended_ports = atomic_read_clear(&pd_ports_to_resume);

	while (suspended_ports) {
		port = __builtin_ctz(suspended_ports);
		suspended_ports &= ~(1 << port);
		pd_set_suspend(port, 0);
	}
}
DECLARE_DEFERRED(resume_pd_port);

void pd_deferred_resume(int port)
{
	atomic_or(&pd_ports_to_resume, 1 << port);
	hook_call_deferred(&resume_pd_port_data, SECOND);
}
#endif  /* CONFIG_USB_PD_DEFERRED_RESUME */

/*
 * HOST COMMANDS
 */
static int hc_pd_ports(struct host_cmd_handler_args *args)
{
	struct ec_response_usb_pd_ports *r = args->response;

	r->num_ports = CONFIG_USB_PD_PORT_COUNT;
	args->response_size = sizeof(*r);

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_USB_PD_PORTS,
			hc_pd_ports,
			EC_VER_MASK(0));

static int hc_remote_rw_hash_entry(struct host_cmd_handler_args *args)
{
	int i;
	int idx = 0;
	int found = 0;
	const struct ec_params_usb_pd_rw_hash_entry *p = args->params;
	static int rw_hash_next_idx;

	if (!p->dev_id)
		return EC_RES_INVALID_PARAM;

	for (i = 0; i < RW_HASH_ENTRIES; i++) {
		if (p->dev_id == rw_hash_table[i].dev_id) {
			idx = i;
			found = 1;
			break;
		}
	}

	if (!found) {
		idx = rw_hash_next_idx;
		rw_hash_next_idx = rw_hash_next_idx + 1;
		if (rw_hash_next_idx == RW_HASH_ENTRIES)
			rw_hash_next_idx = 0;
	}

	memcpy(&rw_hash_table[idx], p, sizeof(*p));

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_USB_PD_RW_HASH_ENTRY,
			hc_remote_rw_hash_entry,
			EC_VER_MASK(0));

static int hc_remote_pd_dev_info(struct host_cmd_handler_args *args)
{
	const uint8_t *port = args->params;
	struct ec_params_usb_pd_rw_hash_entry *r = args->response;

	if (*port >= CONFIG_USB_PD_PORT_COUNT)
		return EC_RES_INVALID_PARAM;

	r->dev_id = tc[*port].dev_id;

	if (r->dev_id)
		memcpy(r->dev_rw_hash, tc[*port].dev_rw_hash, PD_RW_HASH_SIZE);

	r->current_image = tc[*port].current_image;

	args->response_size = sizeof(*r);
	return EC_RES_SUCCESS;
}

DECLARE_HOST_COMMAND(EC_CMD_USB_PD_DEV_INFO,
			hc_remote_pd_dev_info,
			EC_VER_MASK(0));

#ifdef CONFIG_USBC_PPC
static void pd_send_hard_reset(int port)
{
	task_set_event(PD_PORT_TO_TASK_ID(port), PD_EVENT_SEND_HARD_RESET, 0);
}

static uint32_t port_oc_reset_req;

static void re_enable_ports(void)
{
	uint32_t ports = atomic_read_clear(&port_oc_reset_req);

	while (ports) {
		int port = __fls(ports);

		ports &= ~BIT(port);

		/*
		 * Let the board know that the overcurrent is
		 * over since we're going to attempt re-enabling
		 * the port.
		 */
		board_overcurrent_event(port, 0);

		pd_send_hard_reset(port);
		/*
		 * TODO(b/117854867): PD3.0 to send an alert message
		 * indicating OCP after explicit contract.
		 */
	}
}
DECLARE_DEFERRED(re_enable_ports);

void pd_handle_overcurrent(int port)
{
	/* Keep track of the overcurrent events. */
	CPRINTS("C%d: overcurrent!", port);

	if (IS_ENABLED(CONFIG_USB_PD_LOGGING))
		pd_log_event(PD_EVENT_PS_FAULT, PD_LOG_PORT_SIZE(port, 0),
			PS_FAULT_OCP, NULL);

	ppc_add_oc_event(port);
	/* Let the board specific code know about the OC event. */
	board_overcurrent_event(port, 1);

	/* Wait 1s before trying to re-enable the port. */
	atomic_or(&port_oc_reset_req, BIT(port));
	hook_call_deferred(&re_enable_ports_data, SECOND);
}
#endif /* defined(CONFIG_USBC_PPC) */

#ifdef CONFIG_USB_PD_TCPC_LOW_POWER
/* 10 ms is enough time for any TCPC transaction to complete. */
#define PD_LPM_DEBOUNCE_US (10 * MSEC)

/* This is only called from the PD tasks that owns the port. */
static void handle_device_access(int port)
{
	/* This should only be called from the PD task */
	assert(port == TASK_ID_TO_PD_PORT(task_get_current()));

	tc[port].low_power_time = get_time().val + PD_LPM_DEBOUNCE_US;
	if (TC_CHK_FLAG(port, TC_FLAGS_LPM_ENGAGED)) {
		CPRINTS("TCPC p%d Exit Low Power Mode", port);
		TC_CLR_FLAG(port, TC_FLAGS_LPM_ENGAGED |
						TC_FLAGS_LPM_REQUESTED);
		/*
		 * Wake to ensure we make another pass through the main task
		 * loop after clearing the flags.
		 */
		task_wake(PD_PORT_TO_TASK_ID(port));
	}
}

static int pd_device_in_low_power(int port)
{
	/*
	 * If we are actively waking the device up in the PD task, do not
	 * let TCPC operation wait or retry because we are in low power mode.
	 */
	if (port == TASK_ID_TO_PD_PORT(task_get_current()) &&
				TC_CHK_FLAG(port, TC_FLAGS_LPM_TRANSITION))
		return 0;

	return TC_CHK_FLAG(port, TC_FLAGS_LPM_ENGAGED);
}

/*
 * TODO(b/137493121): Move this function to a separate file that's shared
 * between the this and the original stack.
 */
static int reset_device_and_notify(int port)
{
	int rv;
	int task, waiting_tasks;

	/* This should only be called from the PD task */
	assert(port == TASK_ID_TO_PD_PORT(task_get_current()));

	TC_SET_FLAG(port, TC_FLAGS_LPM_TRANSITION);
	rv = tcpm_init(port);
	TC_CLR_FLAG(port, TC_FLAGS_LPM_TRANSITION);

	if (rv == EC_SUCCESS)
		CPRINTS("TCPC p%d init ready", port);
	else
		CPRINTS("TCPC p%d init failed!", port);

	/*
	 * Before getting the other tasks that are waiting, clear the reset
	 * event from this PD task to prevent multiple reset/init events
	 * occurring.
	 *
	 * The double reset event happens when the higher priority PD interrupt
	 * task gets an interrupt during the above tcpm_init function. When that
	 * occurs, the higher priority task waits correctly for us to finish
	 * waking the TCPC, but it has also set PD_EVENT_TCPC_RESET again, which
	 * would result in a second, unnecessary init.
	 */
	atomic_clear(task_get_event_bitmap(task_get_current()),
		PD_EVENT_TCPC_RESET);

	waiting_tasks = atomic_read_clear(&tc[port].tasks_waiting_on_reset);

	/*
	 * Now that we are done waking up the device, handle device access
	 * manually because we ignored it while waking up device.
	 */
	handle_device_access(port);

	/* Clear SW LPM state; the state machine will set it again if needed */
	TC_CLR_FLAG(port, TC_FLAGS_LPM_REQUESTED);

	/* Wake up all waiting tasks. */
	while (waiting_tasks) {
		task = __fls(waiting_tasks);
		waiting_tasks &= ~BIT(task);
		task_set_event(task, TASK_EVENT_PD_AWAKE, 0);
	}

	return rv;
}

/*
 * TODO(b/137493121): Move this function to a separate file that's shared
 * between the this and the original stack.
 */
static void pd_wait_for_wakeup(int port)
{
	if (port == TASK_ID_TO_PD_PORT(task_get_current())) {
		/* If we are in the PD task, we can directly reset */
		reset_device_and_notify(port);
	} else {
		/* Otherwise, we need to wait for the TCPC reset to complete */
		atomic_or(&tc[port].tasks_waiting_on_reset,
			1 << task_get_current());
		/*
		 * NOTE: We could be sending the PD task the reset event while
		 * it is already processing the reset event. If that occurs,
		 * then we will reset the TCPC multiple times, which is
		 * undesirable but most likely benign. Empirically, this doesn't
		 * happen much, but it if starts occurring, we can add a guard
		 * to prevent/reduce it.
		 */
		task_set_event(PD_PORT_TO_TASK_ID(port),
			       PD_EVENT_TCPC_RESET, 0);
		task_wait_event_mask(TASK_EVENT_PD_AWAKE, -1);
	}
}

/*
 * TODO(b/137493121): Move this function to a separate file that's shared
 * between the this and the original stack.
 */
void pd_wait_exit_low_power(int port)
{
	if (pd_device_in_low_power(port))
		pd_wait_for_wakeup(port);
}

/*
 * TODO(b/137493121): Move this function to a separate file that's shared
 * between the this and the original stack.
 */
/*
 * This can be called from any task. If we are in the PD task, we can handle
 * immediately. Otherwise, we need to notify the PD task via event.
 */
void pd_device_accessed(int port)
{
	if (port == TASK_ID_TO_PD_PORT(task_get_current())) {
		/* Ignore any access to device while it is waking up */
		if (TC_CHK_FLAG(port, TC_FLAGS_LPM_TRANSITION))
			return;

		handle_device_access(port);
	} else {
		task_set_event(PD_PORT_TO_TASK_ID(port),
			PD_EVENT_DEVICE_ACCESSED, 0);
	}
}

/*
 * TODO(b/137493121): Move this function to a separate file that's shared
 * between the this and the original stack.
 */
void pd_prevent_low_power_mode(int port, int prevent)
{
	const int current_task_mask = (1 << task_get_current());

	if (prevent)
		atomic_or(&tc[port].tasks_preventing_lpm, current_task_mask);
	else
		atomic_clear(&tc[port].tasks_preventing_lpm, current_task_mask);
}

#endif /* CONFIG_USB_PD_TCPC_LOW_POWER */

static void sink_power_sub_states(int port)
{
	enum tcpc_cc_voltage_status cc1, cc2, cc;
	enum tcpc_cc_voltage_status new_cc_voltage;

	tcpm_get_cc(port, &cc1, &cc2);

	cc = tc[port].polarity ? cc2 : cc1;

	if (cc == TYPEC_CC_VOLT_RP_DEF)
		new_cc_voltage = TYPEC_CC_VOLT_RP_DEF;
	else if (cc == TYPEC_CC_VOLT_RP_1_5)
		new_cc_voltage = TYPEC_CC_VOLT_RP_1_5;
	else if (cc == TYPEC_CC_VOLT_RP_3_0)
		new_cc_voltage = TYPEC_CC_VOLT_RP_3_0;
	else
		new_cc_voltage = TYPEC_CC_VOLT_OPEN;

	/* Debounce the cc state */
	if (new_cc_voltage != tc[port].cc_voltage) {
		tc[port].cc_voltage = new_cc_voltage;
		tc[port].cc_debounce =
				get_time().val + PD_T_RP_VALUE_CHANGE;
		return;
	}

	if (tc[port].cc_debounce == 0 ||
				get_time().val < tc[port].cc_debounce)
		return;

	tc[port].cc_debounce = 0;

	if (IS_ENABLED(CONFIG_CHARGE_MANAGER)) {
		tc[port].typec_curr = usb_get_typec_current_limit(
			tc[port].polarity, cc1, cc2);

		typec_set_input_current_limit(port,
			tc[port].typec_curr, TYPE_C_VOLTAGE);
		charge_manager_update_dualrole(port, CAP_DEDICATED);
	}
}


/*
 * TYPE-C State Implementations
 */

/**
 * Disabled
 *
 * Super State Entry Actions:
 *   Remove the terminations from CC
 *   Set's VBUS and VCONN off
 */
static int tc_disabled(int port, enum sm_signal sig)
{
	int ret = 0;

	ret = (*tc_disabled_sig[sig])(port);
	return SM_SUPER(ret, sig, tc_cc_open);
}

static int tc_disabled_entry(int port)
{
	tc[port].state_id = TC_DISABLED;
	CPRINTS("C%d: %s", port, tc_state_names[tc[port].state_id]);

	return 0;
}

static int tc_disabled_run(int port)
{
	task_wait_event(-1);
	return SM_RUN_SUPER;
}

static int tc_disabled_exit(int port)
{
	if (!IS_ENABLED(CONFIG_USB_PD_TCPC)) {
		if (tc_restart_tcpc(port) != 0) {
			CPRINTS("TCPC p%d restart failed!", port);
			return 0;
		}
	}

	CPRINTS("TCPC p%d resumed!", port);

	return 0;
}

/**
 * ErrorRecovery
 *
 * Super State Entry Actions:
 *   Remove the terminations from CC
 *   Set's VBUS and VCONN off
 */
static int tc_error_recovery(int port, enum sm_signal sig)
{
	int ret = 0;

	ret = (*tc_error_recovery_sig[sig])(port);
	return SM_SUPER(ret, sig, tc_cc_open);
}

static int tc_error_recovery_entry(int port)
{
	tc[port].state_id = TC_ERROR_RECOVERY;
	CPRINTS("C%d: %s", port, tc_state_names[tc[port].state_id]);

	tc[port].timeout = get_time().val + PD_T_ERROR_RECOVERY;
	return 0;
}

static int tc_error_recovery_run(int port)
{
	if (tc[port].timeout > 0 && get_time().val > tc[port].timeout) {
		tc[port].timeout = 0;
		tc_state_init(port, TC_UNATTACHED_SRC);
	}

	return 0;
}

/**
 * Unattached.SNK
 *
 * Super State Entry Actions:
 *   Vconn Off
 *   Place Rd on CC
 *   Set power role to SINK
 */
static int tc_unattached_snk(int port, enum sm_signal sig)
{
	int ret;

	ret = (*tc_unattached_snk_sig[sig])(port);
	return SM_SUPER(ret, sig, tc_cc_rd);
}

static int tc_unattached_snk_entry(int port)
{
	tc[port].state_id = TC_UNATTACHED_SNK;
	if (tc[port].obj.last_state != tc_unattached_src)
		CPRINTS("C%d: %s", port, tc_state_names[tc[port].state_id]);

	if (IS_ENABLED(CONFIG_CHARGE_MANAGER))
		charge_manager_update_dualrole(port, CAP_UNKNOWN);

	/*
	 * Indicate that the port is disconnected so the board
	 * can restore state from any previous data swap.
	 */
	pd_execute_data_swap(port, PD_ROLE_DISCONNECTED);
	tc[port].next_role_swap = get_time().val + PD_T_DRP_SNK;

	return 0;
}

static int tc_unattached_snk_run(int port)
{
	enum tcpc_cc_voltage_status cc1, cc2;

	/*
	 * TODO(b/137498392): Add wait before sampling the CC
	 * status after role changes
	 */

	/* Check for connection */
	tcpm_get_cc(port, &cc1, &cc2);

	/*
	 * The port shall transition to AttachWait.SNK when a Source
	 * connection is detected, as indicated by the SNK.Rp state
	 * on at least one of its CC pins.
	 *
	 * A DRP shall transition to Unattached.SRC within tDRPTransition
	 * after the state of both CC pins is SNK.Open for
	 * tDRP − dcSRC.DRP ∙ tDRP.
	 */
	if (cc_is_rp(cc1) || cc_is_rp(cc2)) {
		/* Connection Detected */
		sm_set_state(port, TC_OBJ(port), tc_attach_wait_snk);
	} else if (get_time().val > tc[port].next_role_swap) {
		/* DRP Toggle */
		sm_set_state(port, TC_OBJ(port), tc_unattached_src);
	}

	return 0;
}

/**
 * AttachWait.SNK
 *
 * Super State Entry Actions:
 *   Vconn Off
 *   Place Rd on CC
 *   Set power role to SINK
 */
static int tc_attach_wait_snk(int port, enum sm_signal sig)
{
	int ret;

	ret = (*tc_attach_wait_snk_sig[sig])(port);
	return SM_SUPER(ret, sig, tc_cc_rd);
}

static int tc_attach_wait_snk_entry(int port)
{
	tc[port].state_id = TC_ATTACH_WAIT_SNK;
	CPRINTS("C%d: %s", port, tc_state_names[tc[port].state_id]);

	tc[port].cc_state = PD_CC_UNSET;
	return 0;
}

static int tc_attach_wait_snk_run(int port)
{
	enum tcpc_cc_voltage_status cc1, cc2;
	enum pd_cc_states new_cc_state;

	/* Check for connection */
	tcpm_get_cc(port, &cc1, &cc2);

	if (cc_is_rp(cc1) && cc_is_rp(cc2))
		new_cc_state = PD_CC_DEBUG_ACC;
	else if (cc_is_rp(cc1) || cc_is_rp(cc2))
		new_cc_state = PD_CC_DFP_ATTACHED;
	else
		new_cc_state = PD_CC_NONE;

	/* Debounce the cc state */
	if (new_cc_state != tc[port].cc_state) {
		tc[port].cc_debounce = get_time().val + PD_T_CC_DEBOUNCE;
		tc[port].pd_debounce = get_time().val + PD_T_PD_DEBOUNCE;
		tc[port].cc_state = new_cc_state;
		return 0;
	}

	/*
	 * A DRP shall transition to Unattached.SNK when the state of both
	 * the CC1 and CC2 pins is SNK.Open for at least tPDDebounce.
	 */
	if (new_cc_state == PD_CC_NONE &&
				get_time().val > tc[port].pd_debounce) {
		/* We are detached */
		return sm_set_state(port, TC_OBJ(port), tc_unattached_src);
	}

	/* Wait for CC debounce */
	if (get_time().val < tc[port].cc_debounce)
		return 0;

	/*
	 * The port shall transition to Attached.SNK after the state of only one
	 * of the CC1 or CC2 pins is SNK.Rp for at least tCCDebounce and VBUS is
	 * detected.
	 *
	 * A DRP that strongly prefers the Source role may optionally transition
	 * to Try.SRC instead of Attached.SNK when the state of only one CC pin
	 * has been SNK.Rp for at least tCCDebounce and VBUS is detected.
	 *
	 * If the port supports Debug Accessory Mode, the port shall transition
	 * to DebugAccessory.SNK if the state of both the CC1 and CC2 pins is
	 * SNK.Rp for at least tCCDebounce and VBUS is detected.
	 */
	if (pd_is_vbus_present(port)) {
		if (new_cc_state == PD_CC_DFP_ATTACHED) {
#ifdef CONFIG_USB_PD_TRY_SRC
			if (pd_try_src_enable)
				sm_set_state(port, TC_OBJ(port), tc_try_src);
			else
#endif
				sm_set_state(port, TC_OBJ(port),
							tc_attached_snk);
		} else {
			/* new_cc_state is PD_CC_DEBUG_ACC */
			TC_SET_FLAG(port, TC_FLAGS_TS_DTS_PARTNER);
			sm_set_state(port, TC_OBJ(port), tc_dbg_acc_snk);
		}
	}

	return SM_RUN_SUPER;
}

/**
 * Attached.SNK
 */
static int tc_attached_snk(int port, enum sm_signal sig)
{
	int ret;

	ret = (*tc_attached_snk_sig[sig])(port);
	return SM_SUPER(ret, sig, 0);
}

static int tc_attached_snk_entry(int port)
{
	enum tcpc_cc_voltage_status cc1, cc2;

	tc[port].state_id = TC_ATTACHED_SNK;
	CPRINTS("C%d: %s", port, tc_state_names[tc[port].state_id]);

	/* Get connector orientation */
	tcpm_get_cc(port, &cc1, &cc2);
	tc[port].polarity = get_snk_polarity(cc1, cc2);
	set_polarity(port, tc[port].polarity);

	/*
	 * Initial data role for sink is UFP
	 * This also sets the usb mux
	 */
	tc_set_data_role(port, PD_ROLE_UFP);

	if (IS_ENABLED(CONFIG_CHARGE_MANAGER)) {
		tc[port].typec_curr = usb_get_typec_current_limit(
			tc[port].polarity, cc1, cc2);
		typec_set_input_current_limit(port, tc[port].typec_curr,
						TYPE_C_VOLTAGE);
		charge_manager_update_dualrole(port, CAP_DEDICATED);
		tc[port].cc_state = (tc[port].polarity) ? cc2 : cc1;
	}

	/* Apply Rd */
	tcpm_set_cc(port, TYPEC_CC_RD);

	tc[port].cc_debounce = 0;
	return 0;
}

static int tc_attached_snk_run(int port)
{
	/* Detach detection */
	if (!pd_is_vbus_present(port))
		return sm_set_state(port, TC_OBJ(port), tc_unattached_snk);

	/* Run Sink Power Sub-State */
	sink_power_sub_states(port);

	return 0;
}

static int tc_attached_snk_exit(int port)
{
	/* Stop drawing power */
	sink_stop_drawing_current(port);

	return 0;
}

/**
 * UnorientedDebugAccessory.SRC
 *
 * Super State Entry Actions:
 *  Vconn Off
 *  Place Rp on CC
 *  Set power role to SOURCE
 */
static int tc_unoriented_dbg_acc_src(int port, enum sm_signal sig)
{
	int ret;

	ret = (*tc_unoriented_dbg_acc_src_sig[sig])(port);
	return SM_SUPER(ret, sig, tc_cc_rp);
}

static int tc_unoriented_dbg_acc_src_entry(int port)
{
	tc[port].state_id = TC_UNORIENTED_DEBUG_ACCESSORY_SRC;
	CPRINTS("C%d: %s", port, tc_state_names[tc[port].state_id]);

	/* Enable VBUS */
	pd_set_power_supply_ready(port);

	/* Any board specific unoriented debug setup should be added below */

	return 0;
}

static int tc_unoriented_dbg_acc_src_run(int port)
{
	enum tcpc_cc_voltage_status cc1, cc2;

	/* Check for connection */
	tcpm_get_cc(port, &cc1, &cc2);

	/*
	 * A DRP, the port shall transition to Unattached.SNK when the
	 * SRC.Open state is detected on either the CC1 or CC2 pin.
	 */
	if (cc1 == TYPEC_CC_VOLT_OPEN || cc2 == TYPEC_CC_VOLT_OPEN) {
		/* Remove VBUS */
		pd_power_supply_reset(port);
		charge_manager_set_ceil(port, CEIL_REQUESTOR_PD,
							CHARGE_CEIL_NONE);

		sm_set_state(port, TC_OBJ(port), tc_unattached_snk);
	}

	return 0;
}

/**
 * Debug Accessory.SNK
 *
 * Super State Entry Actions:
 *   Vconn Off
 *   Place Rd on CC
 *   Set power role to SINK
 */
static int tc_dbg_acc_snk(int port, enum sm_signal sig)
{
	int ret;

	ret = (*tc_dbg_acc_snk_sig[sig])(port);
	return SM_SUPER(ret, sig, tc_cc_rd);
}

static int tc_dbg_acc_snk_entry(int port)
{
	tc[port].state_id = TC_DEBUG_ACCESSORY_SNK;
	CPRINTS("C%d: %s", port, tc_state_names[tc[port].state_id]);

	/*
	 * TODO(b/137759869): Board specific debug accessory setup should
	 * be add here.
	 */

	return 0;
}

static int tc_dbg_acc_snk_run(int port)
{
	if (!pd_is_vbus_present(port))
		sm_set_state(port, TC_OBJ(port), tc_unattached_snk);

	return 0;
}

/**
 * Unattached.SRC
 *
 * Super State Entry Actions:
 *   Vconn Off
 *   Place Rp on CC
 *   Set power role to SOURCE
 */
static int tc_unattached_src(int port, enum sm_signal sig)
{
	int ret;

	ret = (*tc_unattached_src_sig[sig])(port);
	return SM_SUPER(ret, sig, tc_cc_rp);
}

static int tc_unattached_src_entry(int port)
{
	tc[port].state_id = TC_UNATTACHED_SRC;
	if (tc[port].obj.last_state != tc_unattached_snk)
		CPRINTS("C%d: %s", port, tc_state_names[tc[port].state_id]);

	if (IS_ENABLED(CONFIG_USBC_PPC)) {
		/* There is no sink connected. */
		ppc_sink_is_connected(port, 0);

		/*
		 * Clear the overcurrent event counter
		 * since we've detected a disconnect.
		 */
		ppc_clear_oc_event_counter(port);
	}

	if (IS_ENABLED(CONFIG_CHARGE_MANAGER))
		charge_manager_update_dualrole(port, CAP_UNKNOWN);

	tc_set_data_role(port, PD_ROLE_DFP);

	/*
	 * Indicate that the port is disconnected so the board
	 * can restore state from any previous data swap.
	 */
	pd_execute_data_swap(port, PD_ROLE_DISCONNECTED);

	tc[port].next_role_swap = get_time().val + PD_T_DRP_SRC;

	return 0;
}

static int tc_unattached_src_run(int port)
{
	enum tcpc_cc_voltage_status cc1, cc2;

	/* Check for connection */
	tcpm_get_cc(port, &cc1, &cc2);

	/*
	 * Transition to AttachWait.SRC when VBUS is vSafe0V and:
	 *   1) The SRC.Rd state is detected on either CC1 or CC2 pin or
	 *   2) The SRC.Ra state is detected on both the CC1 and CC2 pins.
	 *
	 * A DRP shall transition to Unattached.SNK within tDRPTransition
	 * after dcSRC.DRP ∙ tDRP
	 */
	if (cc_is_at_least_one_rd(cc1, cc2) || cc_is_audio_acc(cc1, cc2))
		sm_set_state(port, TC_OBJ(port), tc_attach_wait_src);
	else if (get_time().val > tc[port].next_role_swap)
		sm_set_state(port, TC_OBJ(port), tc_unattached_snk);

	return SM_RUN_SUPER;
}

/**
 * AttachWait.SRC
 *
 * Super State Entry Actions:
 *   Vconn Off
 *   Place Rp on CC
 *   Set power role to SOURCE
 */
static int tc_attach_wait_src(int port, enum sm_signal sig)
{
	int ret;

	ret = (*tc_attach_wait_src_sig[sig])(port);
	return SM_SUPER(ret, sig, tc_cc_rp);
}

static int tc_attach_wait_src_entry(int port)
{
	tc[port].state_id = TC_ATTACH_WAIT_SRC;
	CPRINTS("C%d: %s", port, tc_state_names[tc[port].state_id]);

	tc[port].cc_state = PD_CC_UNSET;

	return 0;
}

static int tc_attach_wait_src_run(int port)
{
	enum tcpc_cc_voltage_status cc1, cc2;
	enum pd_cc_states new_cc_state;

	/* Check for connection */
	tcpm_get_cc(port, &cc1, &cc2);

	/* Debug accessory */
	if (cc_is_snk_dbg_acc(cc1, cc2)) {
		/* Debug accessory */
		new_cc_state = PD_CC_DEBUG_ACC;
	} else if (cc_is_at_least_one_rd(cc1, cc2)) {
		/* UFP attached */
		new_cc_state = PD_CC_UFP_ATTACHED;
	} else if (cc_is_audio_acc(cc1, cc2)) {
		/* AUDIO Accessory not supported. Just ignore */
		new_cc_state = PD_CC_AUDIO_ACC;
	} else {
		/* No UFP */
		return sm_set_state(port, TC_OBJ(port), tc_unattached_snk);
	}

	/* Debounce the cc state */
	if (new_cc_state != tc[port].cc_state) {
		tc[port].cc_debounce = get_time().val + PD_T_CC_DEBOUNCE;
		tc[port].cc_state = new_cc_state;
		return 0;
	}

	/* Wait for CC debounce */
	if (get_time().val < tc[port].cc_debounce)
		return 0;

	/*
	 * The port shall transition to Attached.SRC when VBUS is at vSafe0V
	 * and the SRC.Rd state is detected on exactly one of the CC1 or CC2
	 * pins for at least tCCDebounce.
	 *
	 * If the port supports Debug Accessory Mode, it shall transition to
	 * UnorientedDebugAccessory.SRC when VBUS is at vSafe0V and the SRC.Rd
	 * state is detected on both the CC1 and CC2 pins for at least
	 * tCCDebounce.
	 */
	if (!pd_is_vbus_present(port)) {
		if (new_cc_state == PD_CC_UFP_ATTACHED)
			return sm_set_state(port, TC_OBJ(port),
							tc_attached_src);
		else if (new_cc_state == PD_CC_DEBUG_ACC)
			return sm_set_state(port, TC_OBJ(port),
						tc_unoriented_dbg_acc_src);
	}

	return 0;
}

/**
 * Attached.SRC
 */
static int tc_attached_src(int port, enum sm_signal sig)
{
	int ret;

	ret = (*tc_attached_src_sig[sig])(port);
	return SM_SUPER(ret, sig, 0);
}

static int tc_attached_src_entry(int port)
{
	enum tcpc_cc_voltage_status cc1, cc2;

	tc[port].state_id = TC_ATTACHED_SRC;
	CPRINTS("C%d: %s", port, tc_state_names[tc[port].state_id]);

	/* Get connector orientation */
	tcpm_get_cc(port, &cc1, &cc2);
	tc[port].polarity = (cc1 != TYPEC_CC_VOLT_RD);
	set_polarity(port, tc[port].polarity);

	/*
	 * Initial data role for sink is DFP
	 * This also sets the usb mux
	 */
	tc_set_data_role(port, PD_ROLE_DFP);

	/*
	 * Start sourcing Vconn before Vbus to ensure
	 * we are within USB Type-C Spec 1.4 tVconnON
	 */
	if (IS_ENABLED(CONFIG_USBC_VCONN))
		set_vconn(port, 1);

	/* Enable VBUS */
	if (pd_set_power_supply_ready(port)) {
		/* Stop sourcing Vconn if Vbus failed */
		if (IS_ENABLED(CONFIG_USBC_VCONN))
			set_vconn(port, 0);

		if (IS_ENABLED(CONFIG_USBC_SS_MUX))
			usb_mux_set(port, TYPEC_MUX_NONE,
			USB_SWITCH_DISCONNECT, tc[port].polarity);
	}

	/* Apply Rp */
	tcpm_set_cc(port, TYPEC_CC_RP);

	tc[port].cc_debounce = 0;
	tc[port].cc_state = PD_CC_NONE;

	/* Inform PPC that a sink is connected. */
	if (IS_ENABLED(CONFIG_USBC_PPC))
		ppc_sink_is_connected(port, 1);

	return 0;
}

static int tc_attached_src_run(int port)
{
	enum tcpc_cc_voltage_status cc1, cc2;
	enum pd_cc_states new_cc_state;

	/* Check for connection */
	tcpm_get_cc(port, &cc1, &cc2);

	if (tc[port].polarity)
		cc1 = cc2;

	if (cc1 == TYPEC_CC_VOLT_OPEN)
		new_cc_state = PD_CC_NO_UFP;
	else
		new_cc_state = PD_CC_NONE;

	/* Debounce the cc state */
	if (new_cc_state != tc[port].cc_state) {
		tc[port].cc_state = new_cc_state;
		tc[port].cc_debounce = get_time().val + PD_T_SRC_DISCONNECT;
	}

	if (get_time().val < tc[port].cc_debounce)
		return 0;

	/*
	 * When the SRC.Open state is detected on the monitored CC pin, a DRP
	 * shall transition to Unattached.SNK unless it strongly prefers the
	 * Source role. In that case, it shall transition to TryWait.SNK.
	 * This transition to TryWait.SNK is needed so that two devices that
	 * both prefer the Source role do not loop endlessly between Source
	 * and Sink. In other words, a DRP that would enter Try.SRC from
	 * AttachWait.SNK shall enter TryWait.SNK for a Sink detach from
	 * Attached.SRC.
	 */
	if (tc[port].cc_state == PD_CC_NO_UFP) {
		if (IS_ENABLED(CONFIG_USB_PD_TRY_SRC))
			return sm_set_state(port, TC_OBJ(port),
							tc_try_wait_snk);
		else
			return sm_set_state(port, TC_OBJ(port),
							tc_unattached_snk);
	}

	return 0;
}

static int tc_attached_src_exit(int port)
{
	/*
	 * A port shall cease to supply VBUS within tVBUSOFF of exiting
	 * Attached.SRC.
	 */
	tc_src_power_off(port);

	return 0;
}

/**
 * Try.SRC
 *
 * Super State Entry Actions:
 *   Vconn Off
 *   Place Rp on CC
 *   Set power role to SOURCE
 */
#ifdef CONFIG_USB_PD_TRY_SRC
static int tc_try_src(int port, enum sm_signal sig)
{
	int ret;

	ret = (*tc_try_src_sig[sig])(port);
	return SM_SUPER(ret, sig, tc_cc_rp);
}

static int tc_try_src_entry(int port)
{
	tc[port].state_id = TC_TRY_SRC;
	CPRINTS("C%d: %s", port, tc_state_names[tc[port].state_id]);

	tc[port].cc_state = PD_CC_UNSET;
	tc[port].try_wait_debounce = get_time().val + PD_T_DRP_TRY;
	tc[port].timeout = get_time().val + PD_T_TRY_TIMEOUT;
	return 0;
}

static int tc_try_src_run(int port)
{
	enum tcpc_cc_voltage_status cc1, cc2;
	enum pd_cc_states new_cc_state;

	/* Check for connection */
	tcpm_get_cc(port, &cc1, &cc2);

	if ((cc1 == TYPEC_CC_VOLT_RD && cc2 != TYPEC_CC_VOLT_RD) ||
	     (cc1 != TYPEC_CC_VOLT_RD && cc2 == TYPEC_CC_VOLT_RD))
		new_cc_state = PD_CC_UFP_ATTACHED;
	else
		new_cc_state = PD_CC_NONE;

	/* Debounce the cc state */
	if (new_cc_state != tc[port].cc_state) {
		tc[port].cc_state = new_cc_state;
		tc[port].cc_debounce = get_time().val + PD_T_CC_DEBOUNCE;
	}

	/*
	 * The port shall transition to Attached.SRC when the SRC.Rd state is
	 * detected on exactly one of the CC1 or CC2 pins for at least
	 * tTryCCDebounce.
	 */
	if (get_time().val > tc[port].cc_debounce) {
		if (new_cc_state == PD_CC_UFP_ATTACHED)
			sm_set_state(port, TC_OBJ(port), tc_attached_src);
	}

	/*
	 * The port shall transition to TryWait.SNK after tDRPTry and the
	 * SRC.Rd state has not been detected and VBUS is within vSafe0V,
	 * or after tTryTimeout and the SRC.Rd state has not been detected.
	 */
	if (new_cc_state == PD_CC_NONE) {
		if ((get_time().val > tc[port].try_wait_debounce &&
					!pd_is_vbus_present(port)) ||
					get_time().val > tc[port].timeout) {
			sm_set_state(port, TC_OBJ(port), tc_try_wait_snk);
		}
	}

	return 0;
}

/**
 * TryWait.SNK
 *
 * Super State Entry Actions:
 *   Vconn Off
 *   Place Rd on CC
 *   Set power role to SINK
 */
static int tc_try_wait_snk(int port, enum sm_signal sig)
{
	int ret;

	ret = (*tc_try_wait_snk_sig[sig])(port);
	return SM_SUPER(ret, sig, tc_cc_rd);
}

static int tc_try_wait_snk_entry(int port)
{
	tc[port].state_id = TC_TRY_WAIT_SNK;
	CPRINTS("C%d: %s", port, tc_state_names[tc[port].state_id]);

	tc[port].cc_state = PD_CC_UNSET;
	tc[port].try_wait_debounce = get_time().val + PD_T_CC_DEBOUNCE;

	return 0;
}

static int tc_try_wait_snk_run(int port)
{
	enum tcpc_cc_voltage_status cc1, cc2;
	enum pd_cc_states new_cc_state;

	/* Check for connection */
	tcpm_get_cc(port, &cc1, &cc2);

	/* We only care about CCs being open */
	if (cc1 == TYPEC_CC_VOLT_OPEN && cc2 == TYPEC_CC_VOLT_OPEN)
		new_cc_state = PD_CC_NONE;
	else
		new_cc_state = PD_CC_UNSET;

	/* Debounce the cc state */
	if (new_cc_state != tc[port].cc_state) {
		tc[port].cc_state = new_cc_state;
		tc[port].pd_debounce = get_time().val + PD_T_PD_DEBOUNCE;
	}

	/*
	 * The port shall transition to Unattached.SNK when the state of both
	 * of the CC1 and CC2 pins is SNK.Open for at least tPDDebounce.
	 */
	if ((get_time().val > tc[port].pd_debounce) &&
						(new_cc_state == PD_CC_NONE)) {
		return sm_set_state(port, TC_OBJ(port), tc_unattached_snk);
	}

	/*
	 * The port shall transition to Attached.SNK after tCCDebounce if or
	 * when VBUS is detected.
	 */
	if (get_time().val > tc[port].try_wait_debounce &&
						pd_is_vbus_present(port))
		sm_set_state(port, TC_OBJ(port), tc_attached_snk);

	return 0;
}

#endif

/**
 * Super State CC_RD
 */
static int tc_cc_rd(int port, enum sm_signal sig)
{
	int ret;

	ret = (*tc_cc_rd_sig[sig])(port);
	return SM_SUPER(ret, sig, 0);
}

static int tc_cc_rd_entry(int port)
{
	/* Disable VCONN */
	if (IS_ENABLED(CONFIG_USBC_VCONN))
		set_vconn(port, 0);

	/*
	 * Both CC1 and CC2 pins shall be independently terminated to
	 * ground through Rd.
	 */
	tcpm_set_cc(port, TYPEC_CC_RD);

	/* Set power role to sink */
	tc_set_power_role(port, PD_ROLE_SINK);
	tcpm_set_msg_header(port, tc[port].power_role, tc[port].data_role);

	return 0;
}

/**
 * Super State CC_RP
 */
static int tc_cc_rp(int port, enum sm_signal sig)
{
	int ret;

	ret = (*tc_cc_rp_sig[sig])(port);
	return SM_SUPER(ret, sig, 0);
}

static int tc_cc_rp_entry(int port)
{
	/* Disable VCONN */
	if (IS_ENABLED(CONFIG_USBC_VCONN))
		set_vconn(port, 0);

	/* Set power role to source */
	tc_set_power_role(port, PD_ROLE_SOURCE);
	tcpm_set_msg_header(port, tc[port].power_role, tc[port].data_role);

	/*
	 * Both CC1 and CC2 pins shall be independently pulled
	 * up through Rp.
	 */
	tcpm_select_rp_value(port, CONFIG_USB_PD_PULLUP);
	tcpm_set_cc(port, TYPEC_CC_RP);

	return 0;
}

/**
 * Super State CC_OPEN
 */
static int tc_cc_open(int port, enum sm_signal sig)
{
	int ret;

	ret = (*tc_cc_open_sig[sig])(port);
	return SM_SUPER(ret, sig, 0);
}

static int tc_cc_open_entry(int port)
{
	/* Disable VBUS */
	pd_power_supply_reset(port);

	/* Disable VCONN */
	if (IS_ENABLED(CONFIG_USBC_VCONN))
		set_vconn(port, 0);

	/* Remove terminations from CC */
	tcpm_set_cc(port, TYPEC_CC_OPEN);

	if (IS_ENABLED(CONFIG_USBC_PPC)) {
		/* There is no sink connected. */
		ppc_sink_is_connected(port, 0);

		/*
		 * Clear the overcurrent event counter
		 * since we've detected a disconnect.
		 */
		ppc_clear_oc_event_counter(port);
	}

	return 0;
}
