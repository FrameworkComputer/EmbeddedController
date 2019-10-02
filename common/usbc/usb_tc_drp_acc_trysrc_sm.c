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
#include "usb_pe_sm.h"
#include "usb_prl_sm.h"
#include "usb_sm.h"
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
#define TC_FLAGS_CTVPD_DETECTED           BIT(6)
#define TC_FLAGS_REQUEST_VC_SWAP_ON       BIT(7)
#define TC_FLAGS_REQUEST_VC_SWAP_OFF      BIT(8)
#define TC_FLAGS_REJECT_VCONN_SWAP        BIT(9)
#define TC_FLAGS_REQUEST_PR_SWAP          BIT(10)
#define TC_FLAGS_REQUEST_DR_SWAP          BIT(11)
#define TC_FLAGS_POWER_OFF_SNK            BIT(12)
#define TC_FLAGS_PARTNER_EXTPOWER         BIT(13)
#define TC_FLAGS_PARTNER_DR_DATA          BIT(14)
#define TC_FLAGS_PARTNER_DR_POWER         BIT(15)
#define TC_FLAGS_PARTNER_PD_CAPABLE       BIT(16)
#define TC_FLAGS_HARD_RESET               BIT(17)
#define TC_FLAGS_PARTNER_USB_COMM         BIT(18)
#define TC_FLAGS_PR_SWAP_IN_PROGRESS      BIT(19)
#define TC_FLAGS_DO_PR_SWAP               BIT(20)
#define TC_FLAGS_DISC_IDENT_IN_PROGRESS   BIT(21)

enum ps_reset_sequence {
	PS_STATE0,
	PS_STATE1,
	PS_STATE2,
	PS_STATE3
};

/* List of all TypeC-level states */
enum usb_tc_state {
	/* Normal States */
	TC_DISABLED,
	TC_ERROR_RECOVERY,
	TC_UNATTACHED_SNK,
	TC_ATTACH_WAIT_SNK,
	TC_ATTACHED_SNK,
	TC_UNORIENTED_DBG_ACC_SRC,
	TC_DBG_ACC_SNK,
	TC_UNATTACHED_SRC,
	TC_ATTACH_WAIT_SRC,
	TC_ATTACHED_SRC,
	TC_TRY_SRC,
	TC_TRY_WAIT_SNK,
#ifdef CONFIG_USB_PE_SM
	TC_CT_UNATTACHED_SNK,
	TC_CT_ATTACHED_SNK,
#endif
	/* Super States */
	TC_CC_OPEN,
	TC_CC_RD,
	TC_CC_RP,
	TC_UNATTACHED,
};
/* Forward declare the full list of states. This is indexed by usb_tc_state */
static const struct usb_state tc_states[];

#ifdef CONFIG_COMMON_RUNTIME
/* List of human readable state names for console debugging */
static const char * const tc_state_names[] = {
	[TC_DISABLED] = "Disabled",
	[TC_ERROR_RECOVERY] = "ErrorRecovery",
	[TC_UNATTACHED_SNK] = "Unattached.SNK",
	[TC_ATTACH_WAIT_SNK] = "AttachWait.SNK",
	[TC_ATTACHED_SNK] = "Attached.SNK",
	[TC_UNORIENTED_DBG_ACC_SRC] = "UnorientedDebugAccessory.SRC",
	[TC_DBG_ACC_SNK] = "DebugAccessory.SNK",
	[TC_UNATTACHED_SRC] = "Unattached.SRC",
	[TC_ATTACH_WAIT_SRC] = "AttachWait.SRC",
	[TC_ATTACHED_SRC] = "Attached.SRC",
	[TC_TRY_SRC] = "Try.SRC",
	[TC_TRY_WAIT_SNK] = "TryWait.SNK",
};
#endif

/* Generate a compiler error if invalid states are referenced */
#ifndef CONFIG_USB_PD_TRY_SRC
extern int TC_TRY_SRC_UNDEFINED;
#define TC_TRY_SRC	TC_TRY_SRC_UNDEFINED
extern int TC_TRY_WAIT_SNK_UNDEFINED;
#define TC_TRY_WAIT_SNK	TC_TRY_WAIT_SNK_UNDEFINED
#endif

static struct type_c {
	/* state machine context */
	struct sm_ctx ctx;
	/* current port power role (SOURCE or SINK) */
	enum pd_power_role power_role;
	/* current port data role (DFP or UFP) */
	enum pd_data_role data_role;
	/* Higher-level power deliver state machines are enabled if true. */
	uint8_t pd_enable;
#ifdef CONFIG_USB_PE_SM
	/* Power supply reset sequence during a hard reset */
	enum ps_reset_sequence ps_reset_state;
#endif
	/* Port polarity : 0 => CC1 is CC line, 1 => CC2 is CC line */
	uint8_t polarity;
	/* port flags, see TC_FLAGS_* */
	uint32_t flags;
	/* Role toggle timer */
	uint64_t next_role_swap;
	/* Generic timer */
	uint64_t timeout;
	/* Time to enter low power mode */
	uint64_t low_power_time;
	/* Tasks to notify after TCPC has been reset */
	int tasks_waiting_on_reset;
	/* Tasks preventing TCPC from entering low power mode */
	int tasks_preventing_lpm;
	/* The last time the cc1 or cc2 line changed. */
	uint64_t cc_last_change;
	/* Current voltage on CC pins */
	enum tcpc_cc_voltage_status cc1, cc2;
	/* Interpreted PD state of above cc1 and cc2 lines */
	enum pd_cc_states cc_state;
	/* Type-C current */
	typec_current_t typec_curr;
	/* Type-C current change */
	typec_current_t typec_curr_change;
	/* Attached ChromeOS device id, RW hash, and current RO / RW image */
	uint16_t dev_id;
	uint32_t dev_rw_hash[PD_RW_HASH_SIZE/4];
	enum ec_current_image current_image;
} tc[CONFIG_USB_PD_PORT_COUNT];

/* Port dual-role state */
static __maybe_unused
enum pd_dual_role_states drp_state[CONFIG_USB_PD_PORT_COUNT] = {
	[0 ... (CONFIG_USB_PD_PORT_COUNT - 1)] =
		CONFIG_USB_PD_INITIAL_DRP_STATE};

#ifdef CONFIG_USBC_VCONN
static void set_vconn(int port, int enable);
#endif

/* Forward declare common, private functions */
static void exit_low_power_mode(int port);
static void handle_device_access(int port);
static void handle_new_power_state(int port);
static int reset_device_and_notify(int port);
static void pd_update_dual_role_config(int port);
static int pd_device_in_low_power(int port);
static void pd_wait_for_wakeup(int port);

/* Tracker for which task is waiting on sysjump prep to finish */
static volatile task_id_t sysjump_task_waiting = TASK_ID_INVALID;

#ifdef CONFIG_USB_PE_SM
/*
 * 4 entry rw_hash table of type-C devices that AP has firmware updates for.
 */
#ifdef CONFIG_COMMON_RUNTIME
#define RW_HASH_ENTRIES 4
static struct ec_params_usb_pd_rw_hash_entry rw_hash_table[RW_HASH_ENTRIES];
#endif

#endif /* CONFIG_USB_PE_SM */

/* Forward declare common, private functions */
static void set_state_tc(const int port, const enum usb_tc_state new_state);
test_export_static enum usb_tc_state get_state_tc(const int port);

/* Enable variable for Try.SRC states */
STATIC_IF(CONFIG_USB_PD_TRY_SRC) uint8_t pd_try_src_enable;
STATIC_IF(CONFIG_USB_PD_TRY_SRC) void pd_update_try_source(void);

static void sink_stop_drawing_current(int port);

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

#endif /* !CONFIG_USB_PRL_SM */

void pd_update_contract(int port)
{
	if (IS_ENABLED(CONFIG_USB_PE_SM)) {
		/* Must be in Attached.SRC when this function is called */
		if (get_state_tc(port) == TC_ATTACHED_SRC)
			pe_dpm_request(port, DPM_REQUEST_SRC_CAP_CHANGE);
	}
}

void pd_request_source_voltage(int port, int mv)
{
	if (IS_ENABLED(CONFIG_USB_PE_SM)) {
		pd_set_max_voltage(mv);

		/* Must be in Attached.SNK when this function is called */
		if (get_state_tc(port) == TC_ATTACHED_SNK)
			pe_dpm_request(port, DPM_REQUEST_NEW_POWER_LEVEL);
		else
			TC_SET_FLAG(port, TC_FLAGS_REQUEST_PR_SWAP);

		task_wake(PD_PORT_TO_TASK_ID(port));
	}
}

void pd_set_external_voltage_limit(int port, int mv)
{
	if (IS_ENABLED(CONFIG_USB_PE_SM)) {
		pd_set_max_voltage(mv);

		/* Must be in Attached.SNK when this function is called */
		if (get_state_tc(port) == TC_ATTACHED_SNK)
			pe_dpm_request(port, DPM_REQUEST_NEW_POWER_LEVEL);

		task_wake(PD_PORT_TO_TASK_ID(port));
	}
}

void pd_set_new_power_request(int port)
{
	if (IS_ENABLED(CONFIG_USB_PE_SM)) {
		/* Must be in Attached.SNK when this function is called */
		if (get_state_tc(port) == TC_ATTACHED_SNK)
			pe_dpm_request(port, DPM_REQUEST_NEW_POWER_LEVEL);
	}
}

void pd_request_power_swap(int port)
{
	if (IS_ENABLED(CONFIG_USB_PE_SM)) {
		/*
		 * Must be in Attached.SRC or Attached.SNK when this function
		 * is called
		 */
		if (get_state_tc(port) == TC_ATTACHED_SRC ||
					get_state_tc(port) == TC_ATTACHED_SNK) {
			TC_SET_FLAG(port, TC_FLAGS_PR_SWAP_IN_PROGRESS);
		}
	}
}

#ifdef CONFIG_USB_PE_SM
void pd_set_dual_role(int port, enum pd_dual_role_states state)
{
	drp_state[port] = state;

	if (IS_ENABLED(CONFIG_USB_PD_TRY_SRC))
		pd_update_try_source();
}

int pd_get_partner_data_swap_capable(int port)
{
	/* return data swap capable status of port partner */
	return TC_CHK_FLAG(port, TC_FLAGS_PARTNER_DR_DATA);
}

int pd_comm_is_enabled(int port)
{
	return tc[port].pd_enable;
}

void pd_send_vdm(int port, uint32_t vid, int cmd, const uint32_t *data,
							int count)
{
	pe_send_vdm(port, vid, cmd, data, count);
}

void pd_request_data_swap(int port)
{
	/*
	 * Must be in Attached.SRC or Attached.SNK when this function
	 * is called
	 */
	if (get_state_tc(port) == TC_ATTACHED_SRC ||
				get_state_tc(port) == TC_ATTACHED_SNK) {
		TC_SET_FLAG(port, TC_FLAGS_REQUEST_DR_SWAP);
		task_set_event(PD_PORT_TO_TASK_ID(port), PD_EVENT_SM, 0);
	}
}

/*
 * Return true if partner port is a DTS or TS capable of entering debug
 * mode (eg. is presenting Rp/Rp or Rd/Rd).
 */
int pd_ts_dts_plugged(int port)
{
	return TC_CHK_FLAG(port, TC_FLAGS_TS_DTS_PARTNER);
}

/* Return true if partner port is known to be PD capable. */
int pd_capable(int port)
{
	return TC_CHK_FLAG(port, TC_FLAGS_PARTNER_PD_CAPABLE);
}

/*
 * Return true if partner port is capable of communication over USB data
 * lines.
 */
int pd_get_partner_usb_comm_capable(int port)
{
	return TC_CHK_FLAG(port, TC_FLAGS_PARTNER_USB_COMM);
}

enum pd_dual_role_states pd_get_dual_role(int port)
{
	return drp_state[port];
}

int pd_dev_store_rw_hash(int port, uint16_t dev_id, uint32_t *rw_hash,
					uint32_t current_image)
{
	int i;

	tc[port].dev_id = dev_id;
	memcpy(tc[port].dev_rw_hash, rw_hash, PD_RW_HASH_SIZE);
#ifdef CONFIG_CMD_PD_DEV_DUMP_INFO
	if (debug_level >= 2)
		pd_dev_dump_info(dev_id, (uint8_t *)rw_hash);
#endif
	tc[port].current_image = current_image;

	/* Search table for matching device / hash */
	for (i = 0; i < RW_HASH_ENTRIES; i++)
		if (dev_id == rw_hash_table[i].dev_id)
			return !memcmp(rw_hash,
					rw_hash_table[i].dev_rw_hash,
					PD_RW_HASH_SIZE);
	return 0;
}

void pd_got_frs_signal(int port)
{
	pe_got_frs_signal(port);
}

int tc_is_attached_src(int port)
{
	return get_state_tc(port) == TC_ATTACHED_SRC;
}

int tc_is_attached_snk(int port)
{
	return get_state_tc(port) == TC_ATTACHED_SNK;
}

void tc_partner_dr_power(int port, int en)
{
	if (en)
		TC_SET_FLAG(port, TC_FLAGS_PARTNER_DR_POWER);
	else
		TC_CLR_FLAG(port, TC_FLAGS_PARTNER_DR_POWER);
}

void tc_partner_extpower(int port, int en)
{
	if (en)
		TC_SET_FLAG(port, TC_FLAGS_PARTNER_EXTPOWER);
	else
		TC_CLR_FLAG(port, TC_FLAGS_PARTNER_EXTPOWER);
}

void tc_partner_usb_comm(int port, int en)
{
	if (en)
		TC_SET_FLAG(port, TC_FLAGS_PARTNER_USB_COMM);
	else
		TC_CLR_FLAG(port, TC_FLAGS_PARTNER_USB_COMM);
}

void tc_partner_dr_data(int port, int en)
{
	if (en)
		TC_SET_FLAG(port, TC_FLAGS_PARTNER_DR_DATA);
	else
		TC_CLR_FLAG(port, TC_FLAGS_PARTNER_DR_DATA);
}

void tc_pd_connection(int port, int en)
{
	if (en)
		TC_SET_FLAG(port, TC_FLAGS_PARTNER_PD_CAPABLE);
	else
		TC_CLR_FLAG(port, TC_FLAGS_PARTNER_PD_CAPABLE);
}

void tc_ctvpd_detected(int port)
{
	TC_SET_FLAG(port, TC_FLAGS_CTVPD_DETECTED);
}

void tc_vconn_on(int port)
{
	set_vconn(port, 1);
}

int tc_check_vconn_swap(int port)
{
#ifdef CONFIG_USBC_VCONN
	if (TC_CHK_FLAG(port, TC_FLAGS_REJECT_VCONN_SWAP))
		return 0;

	return pd_check_vconn_swap(port);
#else
	return 0;
#endif
}

void tc_pr_swap_complete(int port)
{
	TC_CLR_FLAG(port, TC_FLAGS_PR_SWAP_IN_PROGRESS);
}

void tc_prs_src_snk_assert_rd(int port)
{
	/* Must be in Attached.SRC when this function is called */
	if (get_state_tc(port) == TC_ATTACHED_SRC) {
		/* Transition to Attached.SNK to assert Rd */
		TC_SET_FLAG(port, TC_FLAGS_DO_PR_SWAP);
		task_set_event(PD_PORT_TO_TASK_ID(port), PD_EVENT_SM, 0);
	}
}

void tc_prs_snk_src_assert_rp(int port)
{
	/* Must be in Attached.SNK when this function is called */
	if (get_state_tc(port) == TC_ATTACHED_SNK) {
		/* Transition to Attached.SRC to assert Rp */
		TC_SET_FLAG(port, TC_FLAGS_DO_PR_SWAP);
		task_set_event(PD_PORT_TO_TASK_ID(port), PD_EVENT_SM, 0);
	}
}

void tc_hard_reset(int port)
{
	TC_SET_FLAG(port, TC_FLAGS_HARD_RESET);
	task_set_event(PD_PORT_TO_TASK_ID(port), PD_EVENT_SM, 0);
}

void tc_disc_ident_in_progress(int port)
{
	TC_SET_FLAG(port, TC_FLAGS_DISC_IDENT_IN_PROGRESS);
}

void tc_disc_ident_complete(int port)
{
	TC_CLR_FLAG(port, TC_FLAGS_DISC_IDENT_IN_PROGRESS);
}
#endif /* CONFIG_USB_PE_SM */

void tc_snk_power_off(int port)
{
	if (get_state_tc(port) == TC_ATTACHED_SNK) {
		TC_SET_FLAG(port, TC_FLAGS_POWER_OFF_SNK);
		sink_stop_drawing_current(port);
	}
}

int tc_src_power_on(int port)
{
	if (get_state_tc(port) == TC_ATTACHED_SRC)
		return pd_set_power_supply_ready(port);

	return 0;
}

void tc_src_power_off(int port)
{
	if (get_state_tc(port) == TC_ATTACHED_SRC) {
		/* Remove VBUS */
		pd_power_supply_reset(port);

		if (IS_ENABLED(CONFIG_CHARGE_MANAGER))
			charge_manager_set_ceil(port, CEIL_REQUESTOR_PD,
						CHARGE_CEIL_NONE);
	}
}

void pd_set_suspend(int port, int enable)
{
	if (pd_is_port_enabled(port) == !enable)
		return;

	set_state_tc(port,
		enable ? TC_DISABLED : TC_UNATTACHED_SNK);
}

int pd_is_port_enabled(int port)
{
	return get_state_tc(port) != TC_DISABLED;
}

int pd_fetch_acc_log_entry(int port)
{
	if (IS_ENABLED(CONFIG_USB_PE_SM))
		pd_send_vdm(port, USB_VID_GOOGLE, VDO_CMD_GET_LOG, NULL, 0);

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
	return (get_state_tc(port) == TC_ATTACHED_SNK) ||
				(get_state_tc(port) == TC_ATTACHED_SRC);
}

#ifdef CONFIG_USB_PD_ALT_MODE_DFP
/*
 * TODO(b/137493121): Move this function to a separate file that's shared
 * between the this and the original stack.
 */
void pd_prepare_sysjump(void)
{
	if (IS_ENABLED(CONFIG_USB_PE_SM)) {
		int i;

		/*
		 * Exit modes before sysjump so we can cleanly enter again
		 * later
		 */
		for (i = 0; i < CONFIG_USB_PD_PORT_COUNT; i++) {
			/*
			 * We can't be in an alternate mode if PD comm is
			 * disabled, so no need to send the event
			 */
			if (!pd_comm_is_enabled(i))
				continue;

			sysjump_task_waiting = task_get_current();
			task_set_event(PD_PORT_TO_TASK_ID(i),
							PD_EVENT_SYSJUMP, 0);
			task_wait_event_mask(TASK_EVENT_SYSJUMP_READY, -1);
			sysjump_task_waiting = TASK_ID_INVALID;
		}
	}
}
#endif

void tc_start_error_recovery(int port)
{
	/*
	 * Async. function call:
	 *   The port should transition to the ErrorRecovery state
	 *   from any other state when directed.
	 */
	set_state_tc(port, TC_ERROR_RECOVERY);
}

static void restart_tc_sm(int port, enum usb_tc_state start_state)
{
	int res = 0;

	res = tc_restart_tcpc(port);

	CPRINTS("TCPC p%d init %s", port, res ? "failed" : "ready");

	/* Disable if restart failed, otherwise start in default state. */
	set_state_tc(port, res ? TC_DISABLED : start_state);

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

#ifdef CONFIG_USB_PE_SM
	tc[port].pd_enable = 0;
	tc[port].ps_reset_state = PS_STATE0;
#endif
}

void tc_state_init(int port)
{
	/* Unattached.SNK is the default starting state. */
	restart_tc_sm(port, TC_UNATTACHED_SNK);
}

enum pd_power_role tc_get_power_role(int port)
{
	return tc[port].power_role;
}

enum pd_data_role tc_get_data_role(int port)
{
	return tc[port].data_role;
}

enum pd_cable_plug tc_get_cable_plug(int port)
{
	/*
	 * Messages sent by this state machine are always from a DFP/UFP,
	 * i.e. the chromebook.
	 */
	return PD_PLUG_FROM_DFP_UFP;
}

uint8_t tc_get_polarity(int port)
{
	return tc[port].polarity;
}

uint8_t tc_get_pd_enabled(int port)
{
	return tc[port].pd_enable;
}

void tc_set_power_role(int port, enum pd_power_role role)
{
	tc[port].power_role = role;
}

/*
 * Private Functions
 */

/* Set the TypeC state machine to a new state. */
static void set_state_tc(const int port, const enum usb_tc_state new_state)
{
	set_state(port, &tc[port].ctx, &tc_states[new_state]);
}

/* Get the current TypeC state. */
test_export_static enum usb_tc_state get_state_tc(const int port)
{
	return tc[port].ctx.current - &tc_states[0];
}

static void print_current_state(const int port)
{
	CPRINTS("C%d: %s", port, tc_state_names[get_state_tc(port)]);
}

/* This is only called from the PD tasks that owns the port. */
static void exit_low_power_mode(int port)
{
	if (!IS_ENABLED(CONFIG_USB_PE_SM) &&
	    !IS_ENABLED(CONFIG_USB_PD_TCPC_LOW_POWER))
		return;

	if (TC_CHK_FLAG(port, TC_FLAGS_LPM_ENGAGED))
		reset_device_and_notify(port);
	else
		TC_CLR_FLAG(port, TC_FLAGS_LPM_REQUESTED);
}

void tc_event_check(int port, int evt)
{
	/* Update the cc variables if there was a change */
	if (evt & PD_EVENT_CC) {
		enum tcpc_cc_voltage_status cc1, cc2;

		tcpm_get_cc(port, &cc1, &cc2);
		if (cc1 != tc[port].cc1 || cc2 != tc[port].cc2) {
			tc[port].cc_state = pd_get_cc_state(cc1, cc2);
			tc[port].cc1 = cc1;
			tc[port].cc2 = cc2;
			tc[port].cc_last_change = get_time().val;
		}
	}

	if (!IS_ENABLED(CONFIG_USB_PE_SM))
		return;

	if (IS_ENABLED(CONFIG_USB_PD_TCPC_LOW_POWER)) {
		if (evt & PD_EXIT_LOW_POWER_EVENT_MASK)
			exit_low_power_mode(port);

		if (evt & PD_EVENT_DEVICE_ACCESSED)
			handle_device_access(port);
	}

	if (IS_ENABLED(CONFIG_POWER_COMMON)) {
		if (evt & PD_EVENT_POWER_STATE_CHANGE)
			handle_new_power_state(port);
	}

	if (IS_ENABLED(CONFIG_USB_PD_ALT_MODE_DFP)) {
		if (evt & PD_EVENT_SYSJUMP) {
			pe_exit_dp_mode(port);
			notify_sysjump_ready(&sysjump_task_waiting);
		}
	}

	if (evt & PD_EVENT_UPDATE_DUAL_ROLE)
		pd_update_dual_role_config(port);
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

void tc_set_data_role(int port, enum pd_data_role role)
{
	tc[port].data_role = role;

	if (IS_ENABLED(CONFIG_USBC_SS_MUX))
		set_usb_mux_with_current_data_role(port);

	/*
	 * Run any board-specific code for role swap (e.g. setting OTG signals
	 * to SoC).
	 */
	pd_execute_data_swap(port, role);

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

/* This must only be called from the PD task */
static void pd_update_dual_role_config(int port)
{
	/*
	 * Change to sink if port is currently a source AND (new DRP
	 * state is force sink OR new DRP state is either toggle off
	 * or debug accessory toggle only and we are in the source
	 * disconnected state).
	 */
	if (!IS_ENABLED(CONFIG_USB_PE_SM))
		return;

	if (tc[port].power_role == PD_ROLE_SOURCE &&
			((drp_state[port] == PD_DRP_FORCE_SINK &&
			!pd_ts_dts_plugged(port)) ||
			(drp_state[port] == PD_DRP_TOGGLE_OFF &&
			get_state_tc(port) == TC_UNATTACHED_SRC))) {
		set_state_tc(port, TC_UNATTACHED_SNK);
	} else if (tc[port].power_role == PD_ROLE_SINK &&
			drp_state[port] == PD_DRP_FORCE_SOURCE) {
		/*
		 * Change to source if port is currently a sink and the
		 * new DRP state is force source.
		 */
		set_state_tc(port, TC_UNATTACHED_SRC);
	}
}

static void handle_new_power_state(int port)
{
	if (IS_ENABLED(CONFIG_POWER_COMMON) &&
	    IS_ENABLED(CONFIG_USB_PE_SM)) {
		if (chipset_in_or_transitioning_to_state(CHIPSET_STATE_ANY_OFF))
			/*
			 * The SoC will negotiated DP mode again when it
			 * boots up
			 */
			pe_exit_dp_mode(port);

		/* Ensure mux is set properly after chipset transition */
		set_usb_mux_with_current_data_role(port);
	}
}

#ifdef CONFIG_USB_PE_SM
/*
 * HOST COMMANDS
 */
#ifdef HAS_TASK_HOSTCMD
static enum ec_status hc_pd_ports(struct host_cmd_handler_args *args)
{
	struct ec_response_usb_pd_ports *r = args->response;

	r->num_ports = CONFIG_USB_PD_PORT_COUNT;
	args->response_size = sizeof(*r);

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_USB_PD_PORTS,
			hc_pd_ports,
			EC_VER_MASK(0));
static const enum pd_dual_role_states dual_role_map[USB_PD_CTRL_ROLE_COUNT] = {
	[USB_PD_CTRL_ROLE_TOGGLE_ON]    = PD_DRP_TOGGLE_ON,
	[USB_PD_CTRL_ROLE_TOGGLE_OFF]   = PD_DRP_TOGGLE_OFF,
	[USB_PD_CTRL_ROLE_FORCE_SINK]   = PD_DRP_FORCE_SINK,
	[USB_PD_CTRL_ROLE_FORCE_SOURCE] = PD_DRP_FORCE_SOURCE,
	[USB_PD_CTRL_ROLE_FREEZE]       = PD_DRP_FREEZE,
};

#ifdef CONFIG_USBC_SS_MUX
static const enum typec_mux typec_mux_map[USB_PD_CTRL_MUX_COUNT] = {
	[USB_PD_CTRL_MUX_NONE] = TYPEC_MUX_NONE,
	[USB_PD_CTRL_MUX_USB]  = TYPEC_MUX_USB,
	[USB_PD_CTRL_MUX_AUTO] = TYPEC_MUX_DP,
	[USB_PD_CTRL_MUX_DP]   = TYPEC_MUX_DP,
	[USB_PD_CTRL_MUX_DOCK] = TYPEC_MUX_DOCK,
};
#endif

__overridable uint8_t board_get_dp_pin_mode(int port)
{
	return 0;
}

/*
 * TODO(b/142911453): Move this function to a common/usb_common.c to avoid
 * duplicate code
 */
static enum ec_status hc_usb_pd_control(struct host_cmd_handler_args *args)
{
	const struct ec_params_usb_pd_control *p = args->params;
	struct ec_response_usb_pd_control_v2 *r_v2 = args->response;
	struct ec_response_usb_pd_control *r = args->response;

	if (p->port >= CONFIG_USB_PD_PORT_COUNT)
		return EC_RES_INVALID_PARAM;

	if (p->role >= USB_PD_CTRL_ROLE_COUNT ||
			p->mux >= USB_PD_CTRL_MUX_COUNT)
		return EC_RES_INVALID_PARAM;

	if (p->role != USB_PD_CTRL_ROLE_NO_CHANGE)
		pd_set_dual_role(p->port, dual_role_map[p->role]);

#ifdef CONFIG_USBC_SS_MUX
	if (p->mux != USB_PD_CTRL_MUX_NO_CHANGE)
		usb_mux_set(p->port, typec_mux_map[p->mux],
			typec_mux_map[p->mux] == TYPEC_MUX_NONE ?
			USB_SWITCH_DISCONNECT :
			USB_SWITCH_CONNECT,
			pd_get_polarity(p->port));
#endif /* CONFIG_USBC_SS_MUX */

	if (p->swap == USB_PD_CTRL_SWAP_DATA)
		pd_request_data_swap(p->port);
	else if (p->swap == USB_PD_CTRL_SWAP_POWER)
		pd_request_power_swap(p->port);
#ifdef CONFIG_USBC_VCONN_SWAP
	else if (p->swap == USB_PD_CTRL_SWAP_VCONN)
		pe_dpm_request(p->port, DPM_REQUEST_VCONN_SWAP);
#endif

	switch (args->version) {
	case 0:
		r->enabled = pd_comm_is_enabled(p->port);
		r->role = tc[p->port].power_role;
		r->polarity = tc[p->port].polarity;
		r->state = get_state_tc(p->port);
		args->response_size = sizeof(*r);
		break;
	case 1:
	case 2:
		if (sizeof(*r_v2) > args->response_max)
			return EC_RES_INVALID_PARAM;

		r_v2->enabled =
			(pd_comm_is_enabled(p->port) ?
			PD_CTRL_RESP_ENABLED_COMMS : 0) |
			(pd_is_connected(p->port) ?
				PD_CTRL_RESP_ENABLED_CONNECTED : 0) |
			(TC_CHK_FLAG(p->port, TC_FLAGS_PARTNER_PD_CAPABLE) ?
				PD_CTRL_RESP_ENABLED_PD_CAPABLE : 0);
		r_v2->role =
			(tc[p->port].power_role ? PD_CTRL_RESP_ROLE_POWER : 0) |
			(tc[p->port].data_role ? PD_CTRL_RESP_ROLE_DATA : 0) |
			(TC_CHK_FLAG(p->port, TC_FLAGS_VCONN_ON) ?
			PD_CTRL_RESP_ROLE_VCONN : 0) |
			(TC_CHK_FLAG(p->port, TC_FLAGS_PARTNER_DR_POWER) ?
				PD_CTRL_RESP_ROLE_DR_POWER : 0) |
			(TC_CHK_FLAG(p->port, TC_FLAGS_PARTNER_DR_DATA) ?
				PD_CTRL_RESP_ROLE_DR_DATA : 0) |
			(TC_CHK_FLAG(p->port, TC_FLAGS_PARTNER_USB_COMM) ?
				PD_CTRL_RESP_ROLE_USB_COMM : 0) |
			(TC_CHK_FLAG(p->port, TC_FLAGS_PARTNER_EXTPOWER) ?
				PD_CTRL_RESP_ROLE_EXT_POWERED : 0);
		r_v2->polarity = tc[p->port].polarity;
		r_v2->cc_state = tc[p->port].cc_state;
		r_v2->dp_mode = board_get_dp_pin_mode(p->port);
		r_v2->cable_type = get_usb_pd_mux_cable_type(p->port);

		strzcpy(r_v2->state, tc_state_names[get_state_tc(p->port)],
				sizeof(r_v2->state));
		if (args->version == 1) {
			/*
			 * ec_response_usb_pd_control_v2 (r_v2) is a
			 * strict superset of ec_response_usb_pd_control_v1
			 */
			args->response_size =
				sizeof(struct ec_response_usb_pd_control_v1);
		} else
			args->response_size = sizeof(*r_v2);
		break;
	default:
		return EC_RES_INVALID_PARAM;
	}
	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_USB_PD_CONTROL,
			hc_usb_pd_control,
			EC_VER_MASK(0) | EC_VER_MASK(1) | EC_VER_MASK(2));

static enum ec_status hc_remote_flash(struct host_cmd_handler_args *args)
{
	const struct ec_params_usb_pd_fw_update *p = args->params;
	int port = p->port;
	int rv = EC_RES_SUCCESS;
	const uint32_t *data = &(p->size) + 1;
	int i, size;

	if (port >= CONFIG_USB_PD_PORT_COUNT)
		return EC_RES_INVALID_PARAM;

	if (p->size + sizeof(*p) > args->params_size)
		return EC_RES_INVALID_PARAM;

#if defined(CONFIG_BATTERY_PRESENT_CUSTOM) ||   \
defined(CONFIG_BATTERY_PRESENT_GPIO)
	/*
	 * Do not allow PD firmware update if no battery and this port
	 * is sinking power, because we will lose power.
	 */
	if (battery_is_present() != BP_YES &&
			charge_manager_get_active_charge_port() == port)
		return EC_RES_UNAVAILABLE;
#endif

	switch (p->cmd) {
	case USB_PD_FW_REBOOT:
		pe_send_vdm(port, USB_VID_GOOGLE, VDO_CMD_REBOOT, NULL, 0);
		/*
		 * Return immediately to free pending i2c bus.  Host needs to
		 * manage this delay.
		 */
		return EC_RES_SUCCESS;

	case USB_PD_FW_FLASH_ERASE:
		pe_send_vdm(port, USB_VID_GOOGLE, VDO_CMD_FLASH_ERASE, NULL, 0);
		/*
		 * Return immediately.  Host needs to manage delays here which
		 * can be as long as 1.2 seconds on 64KB RW flash.
		 */
		return EC_RES_SUCCESS;

	case USB_PD_FW_ERASE_SIG:
		pe_send_vdm(port, USB_VID_GOOGLE, VDO_CMD_ERASE_SIG, NULL, 0);
		break;

	case USB_PD_FW_FLASH_WRITE:
		/* Data size must be a multiple of 4 */
		if (!p->size || p->size % 4)
			return EC_RES_INVALID_PARAM;

		size = p->size / 4;
		for (i = 0; i < size; i += VDO_MAX_SIZE - 1) {
			pe_send_vdm(port, USB_VID_GOOGLE, VDO_CMD_FLASH_WRITE,
				data + i, MIN(size - i, VDO_MAX_SIZE - 1));
		}
		return EC_RES_SUCCESS;

	default:
		return EC_RES_INVALID_PARAM;
	}

	return rv;
}
DECLARE_HOST_COMMAND(EC_CMD_USB_PD_FW_UPDATE,
			hc_remote_flash,
			EC_VER_MASK(0));

static enum ec_status
hc_remote_rw_hash_entry(struct host_cmd_handler_args *args)
{
	int i, idx = 0, found = 0;
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

static enum ec_status hc_remote_pd_dev_info(struct host_cmd_handler_args *args)
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

#ifndef CONFIG_USB_PD_TCPC
#ifdef CONFIG_EC_CMD_PD_CHIP_INFO
static enum ec_status hc_remote_pd_chip_info(struct host_cmd_handler_args *args)
{
	const struct ec_params_pd_chip_info *p = args->params;
	struct ec_response_pd_chip_info_v1 *info;

	if (p->port >= CONFIG_USB_PD_PORT_COUNT)
		return EC_RES_INVALID_PARAM;

	if (tcpm_get_chip_info(p->port, p->live, &info))
		return EC_RES_ERROR;

	/*
	 * Take advantage of the fact that v0 and v1 structs have the
	 * same layout for v0 data. (v1 just appends data)
	 */
	args->response_size =
		args->version ? sizeof(struct ec_response_pd_chip_info_v1)
				: sizeof(struct ec_response_pd_chip_info);

	memcpy(args->response, info, args->response_size);

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_PD_CHIP_INFO,
			hc_remote_pd_chip_info,
			EC_VER_MASK(0) | EC_VER_MASK(1));
#endif /* CONFIG_EC_CMD_PD_CHIP_INFO */
#endif /* !CONFIG_USB_PD_TCPC */

#ifdef CONFIG_HOSTCMD_EVENTS
void pd_notify_dp_alt_mode_entry(void)
{
	/*
	 * Note: EC_HOST_EVENT_PD_MCU may be a more appropriate host event to
	 * send, but we do not send that here because there are other cases
	 * where we send EC_HOST_EVENT_PD_MCU such as charger insertion or
	 * removal.  Currently, those do not wake the system up, but
	 * EC_HOST_EVENT_MODE_CHANGE does.  If we made the system wake up on
	 * EC_HOST_EVENT_PD_MCU, we would be turning the internal display on on
	 * every charger insertion/removal, which is not desired.
	 */
	CPRINTS("Notifying AP of DP Alt Mode Entry...");
	host_set_single_event(EC_HOST_EVENT_MODE_CHANGE);
}
#endif /* CONFIG_HOSTCMD_EVENTS */

#ifdef CONFIG_USB_PD_ALT_MODE_DFP
static enum ec_status hc_remote_pd_set_amode(struct host_cmd_handler_args *args)
{
	const struct ec_params_usb_pd_set_mode_request *p = args->params;

	if ((p->port >= CONFIG_USB_PD_PORT_COUNT) || (!p->svid) || (!p->opos))
		return EC_RES_INVALID_PARAM;

	switch (p->cmd) {
	case PD_EXIT_MODE:
		if (pd_dfp_exit_mode(p->port, p->svid, p->opos))
			pd_send_vdm(p->port, p->svid,
			CMD_EXIT_MODE | VDO_OPOS(p->opos), NULL, 0);
		else {
			CPRINTF("Failed exit mode\n");
			return EC_RES_ERROR;
		}
		break;
	case PD_ENTER_MODE:
		if (pd_dfp_enter_mode(p->port, p->svid, p->opos))
			pd_send_vdm(p->port, p->svid, CMD_ENTER_MODE |
				VDO_OPOS(p->opos), NULL, 0);
		break;
	default:
		return EC_RES_INVALID_PARAM;
	}
	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_USB_PD_SET_AMODE,
		hc_remote_pd_set_amode,
		EC_VER_MASK(0));
#endif /* CONFIG_USB_PD_ALT_MODE_DFP */
#endif /* HAS_TASK_HOSTCMD */

#if defined(CONFIG_USB_PD_ALT_MODE) && !defined(CONFIG_USB_PD_ALT_MODE_DFP)
void pd_send_hpd(int port, enum hpd_event hpd)
{
	uint32_t data[1];
	int opos = pd_alt_mode(port, USB_SID_DISPLAYPORT);

	if (!opos)
		return;

	data[0] =
		VDO_DP_STATUS((hpd == hpd_irq), /* IRQ_HPD */
		(hpd != hpd_low), /* HPD_HI|LOW */
		0, /* request exit DP */
		0, /* request exit USB */
		0, /* MF pref */
		1, /* enabled */
		0, /* power low */
		0x2);
		pd_send_vdm(port, USB_SID_DISPLAYPORT,
		VDO_OPOS(opos) | CMD_ATTENTION, data, 1);
}
#endif
#endif /* CONFIG_USB_PE_SM */

#ifdef CONFIG_USBC_VCONN_SWAP
void pd_request_vconn_swap_off(int port)
{
	if (get_state_tc(port) == TC_ATTACHED_SRC ||
			get_state_tc(port) == TC_ATTACHED_SNK) {
		TC_SET_FLAG(port, TC_FLAGS_REQUEST_VC_SWAP_OFF);
		task_wake(PD_PORT_TO_TASK_ID(port));
	}
}

void pd_request_vconn_swap_on(int port)
{
	if (get_state_tc(port) == TC_ATTACHED_SRC ||
			get_state_tc(port) == TC_ATTACHED_SNK) {
		TC_SET_FLAG(port, TC_FLAGS_REQUEST_VC_SWAP_ON);
		task_wake(PD_PORT_TO_TASK_ID(port));
	}
}
#endif

#ifdef CONFIG_USBC_VCONN
int tc_is_vconn_src(int port)
{
	if (get_state_tc(port) == TC_ATTACHED_SRC ||
			get_state_tc(port) == TC_ATTACHED_SNK)
		return TC_CHK_FLAG(port, TC_FLAGS_VCONN_ON);
	else
		return -1;
}
#endif

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

/* 10 ms is enough time for any TCPC transaction to complete. */
#define PD_LPM_DEBOUNCE_US (10 * MSEC)

/* This is only called from the PD tasks that owns the port. */
static void handle_device_access(int port)
{
	if (!IS_ENABLED(CONFIG_USB_PD_TCPC_LOW_POWER))
		return;

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
	if (!IS_ENABLED(CONFIG_USB_PD_TCPC_LOW_POWER))
		return 0;
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

	if (!IS_ENABLED(CONFIG_USB_PD_TCPC_LOW_POWER))
		return 0;

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
	if (!IS_ENABLED(CONFIG_USB_PD_TCPC_LOW_POWER))
		return;

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
	if (!IS_ENABLED(CONFIG_USB_PD_TCPC_LOW_POWER))
		return;

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

	if (!IS_ENABLED(CONFIG_USB_PD_TCPC_LOW_POWER))
		return;

	if (prevent)
		atomic_or(&tc[port].tasks_preventing_lpm, current_task_mask);
	else
		atomic_clear(&tc[port].tasks_preventing_lpm, current_task_mask);
}

static void sink_power_sub_states(int port)
{
	/* Debounce cc lines for at least Rp value change timer */
	if (get_time().val < tc[port].cc_last_change + PD_T_RP_VALUE_CHANGE)
		return;

	if (IS_ENABLED(CONFIG_CHARGE_MANAGER)) {
		tc[port].typec_curr = usb_get_typec_current_limit(
			tc[port].polarity, tc[port].cc1, tc[port].cc2);

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
static void tc_disabled_entry(const int port)
{
	print_current_state(port);
}

static void tc_disabled_run(const int port)
{
	task_wait_event(-1);
}

static void tc_disabled_exit(const int port)
{
	if (!IS_ENABLED(CONFIG_USB_PD_TCPC)) {
		if (tc_restart_tcpc(port) != 0) {
			CPRINTS("TCPC p%d restart failed!", port);
			return;
		}
	}

	CPRINTS("TCPC p%d resumed!", port);
}

/**
 * ErrorRecovery
 *
 * Super State Entry Actions:
 *   Remove the terminations from CC
 *   Set's VBUS and VCONN off
 */
static void tc_error_recovery_entry(const int port)
{
	print_current_state(port);

	tc[port].timeout = get_time().val + PD_T_ERROR_RECOVERY;
}

static void tc_error_recovery_run(const int port)
{
	if (tc[port].timeout > 0 && get_time().val > tc[port].timeout) {
		tc[port].timeout = 0;
		restart_tc_sm(port, TC_UNATTACHED_SRC);
	}
}

/* Set the CC resistors to Rd and update the TCPC power role header */
static void set_rd(const int port)
{
	/*
	 * Both CC1 and CC2 pins shall be independently terminated to
	 * ground through Rd. Reset last cc change time.
	 */
	tcpm_set_cc(port, TYPEC_CC_RD);
	tc[port].cc_last_change = get_time().val;

	/* Set power role to sink */
	tc_set_power_role(port, PD_ROLE_SINK);
	tcpm_set_msg_header(port, tc[port].power_role, tc[port].data_role);
}

/**
 * Unattached.SNK
 *
 * Super State is Unattached state
 */
static void tc_unattached_snk_entry(const int port)
{
	/* Set Rd since we are not in the Rd superstate */
	set_rd(port);

	if (IS_ENABLED(CONFIG_CHARGE_MANAGER))
		charge_manager_update_dualrole(port, CAP_UNKNOWN);

	tc[port].next_role_swap = get_time().val + PD_T_DRP_SNK;

	if (IS_ENABLED(CONFIG_USB_PE_SM)) {
		tc[port].flags = 0;
		tc[port].pd_enable = 0;
	}
}

static void tc_unattached_snk_run(const int port)
{
	/*
	 * TODO(b/137498392): Add wait before sampling the CC
	 * status after role changes
	 */

	if (IS_ENABLED(CONFIG_USB_PE_SM)) {
		if (TC_CHK_FLAG(port, TC_FLAGS_HARD_RESET)) {
			TC_CLR_FLAG(port, TC_FLAGS_HARD_RESET);
			tc_set_data_role(port, PD_ROLE_UFP);
			/* Inform Policy Engine that hard reset is complete */
			pe_ps_reset_complete(port);
		}
	}

	/*
	 * The port shall transition to AttachWait.SNK when a Source
	 * connection is detected, as indicated by the SNK.Rp state
	 * on at least one of its CC pins.
	 *
	 * A DRP shall transition to Unattached.SRC within tDRPTransition
	 * after the state of both CC pins is SNK.Open for
	 * tDRP − dcSRC.DRP ∙ tDRP.
	 */
	if (tc[port].cc_state == PD_CC_DFP_DEBUG_ACC ||
	    tc[port].cc_state == PD_CC_DFP_ATTACHED)
		/* Connection Detected */
		set_state_tc(port, TC_ATTACH_WAIT_SNK);
	else if (get_time().val > tc[port].next_role_swap)
		/* DRP Toggle */
		set_state_tc(port, TC_UNATTACHED_SRC);
}

/**
 * AttachWait.SNK
 *
 * Super State Entry Actions:
 *   Vconn Off
 *   Place Rd on CC
 *   Set power role to SINK
 */
static void tc_attach_wait_snk_entry(const int port)
{
	print_current_state(port);
}

static void tc_attach_wait_snk_run(const int port)
{
	/*
	 * A DRP shall transition to Unattached.SNK when the state of both
	 * the CC1 and CC2 pins is SNK.Open for at least tPDDebounce.
	 */
	if (get_time().val > tc[port].cc_last_change + PD_T_PD_DEBOUNCE &&
	    tc[port].cc_state == PD_CC_NONE) {
		if (IS_ENABLED(CONFIG_USB_PE_SM) &&
		    IS_ENABLED(CONFIG_USB_PD_ALT_MODE_DFP))
			pd_dfp_exit_mode(port, 0, 0);

		/* We are detached */
		set_state_tc(port, TC_UNATTACHED_SRC);
		return;
	}

	/* Wait for CC debounce */
	if (get_time().val < tc[port].cc_last_change + PD_T_CC_DEBOUNCE)
		return;

	/*
	 * The port shall transition to Attached.SNK after the state of only
	 * one of the CC1 or CC2 pins is SNK.Rp for at least tCCDebounce and
	 * VBUS is detected.
	 *
	 * A DRP that strongly prefers the Source role may optionally
	 * transition to Try.SRC instead of Attached.SNK when the state of only
	 * one CC pin has been SNK.Rp for at least tCCDebounce and VBUS is
	 * detected.
	 *
	 * If the port supports Debug Accessory Mode, the port shall transition
	 * to DebugAccessory.SNK if the state of both the CC1 and CC2 pins is
	 * SNK.Rp for at least tCCDebounce and VBUS is detected.
	 */
	if (pd_is_vbus_present(port)) {
		if (tc[port].cc_state == PD_CC_DFP_ATTACHED) {
			if (IS_ENABLED(CONFIG_USB_PD_TRY_SRC) &&
			    pd_try_src_enable)
				set_state_tc(port, TC_TRY_SRC);
			else
				set_state_tc(port, TC_ATTACHED_SNK);
		} else {
			/* cc_state is PD_CC_DFP_DEBUG_ACC */
			TC_SET_FLAG(port, TC_FLAGS_TS_DTS_PARTNER);
			set_state_tc(port, TC_DBG_ACC_SNK);
		}

		if (IS_ENABLED(CONFIG_USB_PE_SM) &&
		    IS_ENABLED(CONFIG_USB_PD_ALT_MODE_DFP)) {
			hook_call_deferred(
				&pd_usb_billboard_deferred_data, PD_T_AME);
		}
	}
}

/**
 * Attached.SNK
 */
static void tc_attached_snk_entry(const int port)
{
	print_current_state(port);

	if (!TC_CHK_FLAG(port, TC_FLAGS_PR_SWAP_IN_PROGRESS)) {
		/* Get connector orientation */
		tc[port].polarity = get_snk_polarity(
			tc[port].cc1, tc[port].cc2);
		set_polarity(port, tc[port].polarity);

		/*
		 * Initial data role for sink is UFP
		 * This also sets the usb mux
		 */
		tc_set_data_role(port, PD_ROLE_UFP);

		if (IS_ENABLED(CONFIG_CHARGE_MANAGER)) {
			tc[port].typec_curr =
			usb_get_typec_current_limit(
				tc[port].polarity, tc[port].cc1, tc[port].cc2);
			typec_set_input_current_limit(
				port, tc[port].typec_curr, TYPE_C_VOLTAGE);
			charge_manager_update_dualrole(port, CAP_DEDICATED);
		}
	}

	/* Enable PD */
	if (IS_ENABLED(CONFIG_USB_PE_SM))
		tc[port].pd_enable = 1;
}

static void tc_attached_snk_run(const int port)
{
#ifdef CONFIG_USB_PE_SM
	/*
	 * Perform Hard Reset
	 */
	if (TC_CHK_FLAG(port, TC_FLAGS_HARD_RESET)) {
		TC_CLR_FLAG(port, TC_FLAGS_HARD_RESET);

		tc_set_data_role(port, PD_ROLE_UFP);
		/* Clear the input current limit */
		sink_stop_drawing_current(port);

		/*
		 * When VCONN is supported, the Hard Reset Shall cause
		 * the Port with the Rd resistor asserted to turn off
		 * VCONN.
		 */
		if (TC_CHK_FLAG(port, TC_FLAGS_VCONN_ON))
			set_vconn(port, 0);

		/*
		 * Inform policy engine that power supply
		 * reset is complete
		 */
		pe_ps_reset_complete(port);
	}

	/*
	 * The sink will be powered off during a power role swap but we don't
	 * want to trigger a disconnect
	 */
	if (!TC_CHK_FLAG(port, TC_FLAGS_POWER_OFF_SNK) &&
	    !TC_CHK_FLAG(port, TC_FLAGS_PR_SWAP_IN_PROGRESS)) {
		/* Detach detection */
		if (!pd_is_vbus_present(port)) {
			if (IS_ENABLED(CONFIG_USB_PD_ALT_MODE_DFP))
				pd_dfp_exit_mode(port, 0, 0);

			set_state_tc(port, TC_UNATTACHED_SNK);
			return;
		}

		if (!pe_is_explicit_contract(port))
			sink_power_sub_states(port);
	}

	/*
	 * PD swap commands
	 */
	if (tc[port].pd_enable && prl_is_running(port)) {
		/*
		 * Power Role Swap
		 */
		if (TC_CHK_FLAG(port, TC_FLAGS_DO_PR_SWAP)) {
			/* Clear PR_SWAP flag in exit */
			set_state_tc(port, TC_ATTACHED_SRC);
			return;
		}

		/*
		 * Data Role Swap
		 */
		if (TC_CHK_FLAG(port, TC_FLAGS_REQUEST_DR_SWAP)) {
			TC_CLR_FLAG(port, TC_FLAGS_REQUEST_DR_SWAP);

			/* Perform Data Role Swap */
			tc_set_data_role(port,
				tc[port].data_role == PD_ROLE_UFP ?
					PD_ROLE_DFP : PD_ROLE_UFP);
		}

#ifdef CONFIG_USBC_VCONN
		/*
		 * VCONN Swap
		 */
		if (TC_CHK_FLAG(port, TC_FLAGS_REQUEST_VC_SWAP_ON)) {
			TC_CLR_FLAG(port, TC_FLAGS_REQUEST_VC_SWAP_ON);

			set_vconn(port, 1);
			/* Inform policy engine that vconn swap is complete */
			pe_vconn_swap_complete(port);
		} else if (TC_CHK_FLAG(port, TC_FLAGS_REQUEST_VC_SWAP_OFF)) {
			TC_CLR_FLAG(port, TC_FLAGS_REQUEST_VC_SWAP_OFF);

			set_vconn(port, 0);
			/* Inform policy engine that vconn swap is complete */
			pe_vconn_swap_complete(port);
		}
#endif
		/*
		 * If the port supports Charge-Through VCONN-Powered USB
		 * devices, and an explicit PD contract has failed to be
		 * negotiated, the port shall query the identity of the
		 * cable via USB PD on SOP’
		 */
		if (!pe_is_explicit_contract(port) &&
				TC_CHK_FLAG(port, TC_FLAGS_CTVPD_DETECTED)) {
			/*
			 * A port that via SOP’ has detected an attached
			 * Charge-Through VCONN-Powered USB device shall
			 * transition to Unattached.SRC if an explicit PD
			 * contract has failed to be negotiated.
			 */
			/* CTVPD detected */
			set_state_tc(port, TC_UNATTACHED_SRC);
		}
	}

#else /* CONFIG_USB_PE_SM */

	/* Detach detection */
	if (!pd_is_vbus_present(port)) {
		set_state_tc(port, TC_UNATTACHED_SNK);
		return;
	}

	/* Run Sink Power Sub-State */
	sink_power_sub_states(port);
#endif /* CONFIG_USB_PE_SM */
}

static void tc_attached_snk_exit(const int port)
{
	/*
	 * If supplying VCONN, the port shall cease to supply
	 * it within tVCONNOFF of exiting Attached.SNK if not PR swapping.
	 */
	if (TC_CHK_FLAG(port, TC_FLAGS_VCONN_ON) &&
	    !TC_CHK_FLAG(port, TC_FLAGS_DO_PR_SWAP))
		set_vconn(port, 0);

	/* Clear flags after checking Vconn status */
	TC_CLR_FLAG(port, TC_FLAGS_DO_PR_SWAP | TC_FLAGS_POWER_OFF_SNK);

	/* Stop drawing power */
	sink_stop_drawing_current(port);
}

/**
 * UnorientedDebugAccessory.SRC
 *
 * Super State Entry Actions:
 *  Vconn Off
 *  Place Rp on CC
 *  Set power role to SOURCE
 */
static void tc_unoriented_dbg_acc_src_entry(const int port)
{
	print_current_state(port);

	/* Enable VBUS */
	pd_set_power_supply_ready(port);

	/* Any board specific unoriented debug setup should be added below */
}

static void tc_unoriented_dbg_acc_src_run(const int port)
{
	/*
	 * A DRP, the port shall transition to Unattached.SNK when the
	 * SRC.Open state is detected on either the CC1 or CC2 pin.
	 */
	if (tc[port].cc1 == TYPEC_CC_VOLT_OPEN ||
	    tc[port].cc2 == TYPEC_CC_VOLT_OPEN) {
		/* Remove VBUS */
		pd_power_supply_reset(port);

		if (IS_ENABLED(CONFIG_CHARGE_MANAGER))
			charge_manager_set_ceil(port, CEIL_REQUESTOR_PD,
							CHARGE_CEIL_NONE);

		set_state_tc(port, TC_UNATTACHED_SNK);
	}
}

/**
 * Debug Accessory.SNK
 *
 * Super State Entry Actions:
 *   Vconn Off
 *   Place Rd on CC
 *   Set power role to SINK
 */
static void tc_dbg_acc_snk_entry(const int port)
{
	print_current_state(port);

	/*
	 * TODO(b/137759869): Board specific debug accessory setup should
	 * be add here.
	 */
}

static void tc_dbg_acc_snk_run(const int port)
{
	if (!pd_is_vbus_present(port)) {
		if (IS_ENABLED(CONFIG_USB_PE_SM) &&
		    IS_ENABLED(CONFIG_USB_PD_ALT_MODE_DFP)) {
			pd_dfp_exit_mode(port, 0, 0);
		}

		set_state_tc(port, TC_UNATTACHED_SNK);
	}
}

/* Set the CC resistors to Rp and update the TCPC power role header */
static void set_rp(const int port)
{
	/*
	 * Both CC1 and CC2 pins shall be independently pulled
	 * up through Rp. Reset last cc change time.
	 */
	tcpm_select_rp_value(port, CONFIG_USB_PD_PULLUP);
	tcpm_set_cc(port, TYPEC_CC_RP);
	tc[port].cc_last_change = get_time().val;

	/* Set power role to source */
	tc_set_power_role(port, PD_ROLE_SOURCE);
	tcpm_set_msg_header(port, tc[port].power_role, tc[port].data_role);
}

/**
 * Unattached.SRC
 *
 * Super State is Unattached state
 */
static void tc_unattached_src_entry(const int port)
{
	/* Set Rd since we are not in the Rd superstate */
	set_rp(port);

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

	if (IS_ENABLED(CONFIG_USB_PE_SM)) {
		tc[port].flags = 0;
		tc[port].pd_enable = 0;
	}

	tc[port].next_role_swap = get_time().val + PD_T_DRP_SRC;
}

static void tc_unattached_src_run(const int port)
{
	if (IS_ENABLED(CONFIG_USB_PE_SM)) {
		if (TC_CHK_FLAG(port, TC_FLAGS_HARD_RESET)) {
			TC_CLR_FLAG(port, TC_FLAGS_HARD_RESET);
			tc_set_data_role(port, PD_ROLE_DFP);
			/* Inform Policy Engine that hard reset is complete */
			pe_ps_reset_complete(port);
		}
	}

	if (IS_ENABLED(CONFIG_USBC_PPC)) {
		/*
		 * If the port is latched off, just continue to
		 * monitor for a detach.
		 */
		if (ppc_is_port_latched_off(port))
			return;
	}

	/*
	 * Transition to AttachWait.SRC when VBUS is vSafe0V and:
	 *   1) The SRC.Rd state is detected on either CC1 or CC2 pin or
	 *   2) The SRC.Ra state is detected on both the CC1 and CC2 pins.
	 *
	 * A DRP shall transition to Unattached.SNK within tDRPTransition
	 * after dcSRC.DRP ∙ tDRP
	 */
	if (cc_is_at_least_one_rd(tc[port].cc1, tc[port].cc2) ||
	    cc_is_audio_acc(tc[port].cc1, tc[port].cc2))
		set_state_tc(port, TC_ATTACH_WAIT_SRC);
	else if (get_time().val > tc[port].next_role_swap)
		set_state_tc(port, TC_UNATTACHED_SNK);
}

/**
 * AttachWait.SRC
 *
 * Super State Entry Actions:
 *   Vconn Off
 *   Place Rp on CC
 *   Set power role to SOURCE
 */
static void tc_attach_wait_src_entry(const int port)
{
	print_current_state(port);
}

static void tc_attach_wait_src_run(const int port)
{
	/* Immediate transition without debounce if we detect Vopen */
	if (tc[port].cc_state == PD_CC_NONE) {
		set_state_tc(port, TC_UNATTACHED_SNK);
		return;
	}

	/* Wait for CC debounce */
	if (get_time().val < tc[port].cc_last_change + PD_T_CC_DEBOUNCE)
		return;

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
		if (tc[port].cc_state == PD_CC_UFP_ATTACHED)
			set_state_tc(port, TC_ATTACHED_SRC);
		else if (tc[port].cc_state == PD_CC_UFP_DEBUG_ACC)
			set_state_tc(port, TC_UNORIENTED_DBG_ACC_SRC);
	}

}

/**
 * Attached.SRC
 */
static void tc_attached_src_entry(const int port)
{
	print_current_state(port);

	/* Run function relies on timeout being 0 or meaningful */
	tc[port].timeout = 0;

	if (TC_CHK_FLAG(port, TC_FLAGS_PR_SWAP_IN_PROGRESS)) {
		/* Enable VBUS */
		pd_set_power_supply_ready(port);

		/*
		 * Maintain VCONN supply state, whether ON or OFF, and its
		 * data role / usb mux connections.
		 */
	} else {
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

		tc[port].polarity = (tc[port].cc1 != TYPEC_CC_VOLT_RD);
		set_polarity(port, tc[port].polarity);

		/*
		 * Initial data role for sink is DFP
		 * This also sets the usb mux
		 */
		tc_set_data_role(port, PD_ROLE_DFP);

		tc[port].pd_enable = 0;
		tc[port].timeout = get_time().val +
			MAX(PD_POWER_SUPPLY_TURN_ON_DELAY, PD_T_VCONN_STABLE);
	}

	/* Inform PPC that a sink is connected. */
	if (IS_ENABLED(CONFIG_USBC_PPC))
		ppc_sink_is_connected(port, 1);
}

static void tc_attached_src_run(const int port)
{
#ifdef CONFIG_USB_PE_SM
	/* Enable PD communications after power supply has fully turned on */
	if (tc[port].pd_enable == 0 && get_time().val > tc[port].timeout) {
		tc[port].pd_enable = 1;
		tc[port].timeout = 0;
	}

	if (tc[port].pd_enable == 0)
		return;

	/*
	 * Handle Hard Reset from Policy Engine
	 */
	if (TC_CHK_FLAG(port, TC_FLAGS_HARD_RESET)) {
		if (get_time().val < tc[port].timeout)
			return;

		switch (tc[port].ps_reset_state) {
		case PS_STATE0:
			/* Remove VBUS */
			tc_src_power_off(port);

			/* Set role to DFP */
			tc_set_data_role(port, PD_ROLE_DFP);

			/* Turn off VCONN */
			if (TC_CHK_FLAG(port, TC_FLAGS_VCONN_ON))
				set_vconn(port, 0);

			/* Remove Rp */
			tcpm_set_cc(port, TYPEC_CC_OPEN);

			tc[port].ps_reset_state = PS_STATE1;
			tc[port].timeout = get_time().val + PD_T_SRC_RECOVER;
			return;
		case PS_STATE1:
			/* Turn VCONN on before Vbus to meet tVconnON */
			if (IS_ENABLED(CONFIG_USBC_VCONN))
				set_vconn(port, 1);

			tc[port].ps_reset_state = PS_STATE2;
			return;
		case PS_STATE2:
			/* Enable VBUS */
			pd_set_power_supply_ready(port);

			/* Apply Rp */
			tcpm_set_cc(port, TYPEC_CC_RP);

			tc[port].ps_reset_state = PS_STATE3;
			tc[port].timeout = get_time().val +
					PD_POWER_SUPPLY_TURN_ON_DELAY;
			return;
		case PS_STATE3:
			/* Tell Policy Engine Hard Reset is complete */
			pe_ps_reset_complete(port);

			TC_CLR_FLAG(port, TC_FLAGS_HARD_RESET);
			tc[port].ps_reset_state = PS_STATE0;
			return;
		}
	}
#endif

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
	if (tc[port].cc_state == PD_CC_NONE &&
			!TC_CHK_FLAG(port, TC_FLAGS_PR_SWAP_IN_PROGRESS) &&
			!TC_CHK_FLAG(port, TC_FLAGS_DISC_IDENT_IN_PROGRESS)) {

		if (IS_ENABLED(CONFIG_USB_PE_SM))
			if (IS_ENABLED(CONFIG_USB_PD_ALT_MODE_DFP))
				pd_dfp_exit_mode(port, 0, 0);

		tc[port].pd_enable = 0;
		set_state_tc(port, IS_ENABLED(CONFIG_USB_PD_TRY_SRC) ?
			TC_TRY_WAIT_SNK : TC_UNATTACHED_SNK);
		return;
	}

#ifdef CONFIG_USB_PE_SM
	/*
	 * PD swap commands
	 */
	if (tc[port].pd_enable && prl_is_running(port)) {
		/*
		 * Power Role Swap Request
		 */
		if (TC_CHK_FLAG(port, TC_FLAGS_DO_PR_SWAP)) {
			/* Clear PR_SWAP flag in exit */
			set_state_tc(port, TC_ATTACHED_SNK);
			return;
		}

		/*
		 * Data Role Swap Request
		 */
		if (TC_CHK_FLAG(port, TC_FLAGS_REQUEST_DR_SWAP)) {
			TC_CLR_FLAG(port, TC_FLAGS_REQUEST_DR_SWAP);

			/* Perform Data Role Swap */
			tc_set_data_role(port,
				tc[port].data_role == PD_ROLE_DFP ?
					PD_ROLE_UFP : PD_ROLE_DFP);
		}

		if (IS_ENABLED(CONFIG_USBC_VCONN)) {
			/*
			 * VCONN Swap Request
			 */
			if (TC_CHK_FLAG(port, TC_FLAGS_REQUEST_VC_SWAP_ON)) {
				TC_CLR_FLAG(port, TC_FLAGS_REQUEST_VC_SWAP_ON);
				set_vconn(port, 1);
				pe_vconn_swap_complete(port);
			} else if (TC_CHK_FLAG(port,
						TC_FLAGS_REQUEST_VC_SWAP_OFF)) {
				TC_CLR_FLAG(port, TC_FLAGS_REQUEST_VC_SWAP_OFF);
				set_vconn(port, 0);
				pe_vconn_swap_complete(port);
			}
		}

		/*
		 * A DRP that supports Charge-Through VCONN-Powered USB Devices
		 * shall transition to CTUnattached.SNK if the connected device
		 * identifies itself as a Charge-Through VCONN-Powered USB
		 * Device in its Discover Identity Command response.
		 */

		/*
		 * A DRP that supports Charge-Through VCONN-Powered USB Devices
		 * shall transition to CTUnattached.SNK if the connected device
		 * identifies itself as a Charge-Through VCONN-Powered USB
		 * Device in its Discover Identity Command response.
		 *
		 * If it detects that it is connected to a VCONN-Powered USB
		 * Device, the port may remove VBUS and discharge it to
		 * vSafe0V, while continuing to remain in this state with VCONN
		 * applied.
		 */
		if (TC_CHK_FLAG(port, TC_FLAGS_CTVPD_DETECTED)) {
			TC_CLR_FLAG(port, TC_FLAGS_CTVPD_DETECTED);

			/* Clear TC_FLAGS_DISC_IDENT_IN_PROGRESS */
			TC_CLR_FLAG(port, TC_FLAGS_DISC_IDENT_IN_PROGRESS);

			set_state_tc(port, TC_CT_UNATTACHED_SNK);
		}
	}
#endif
}

static void tc_attached_src_exit(const int port)
{
	/*
	 * A port shall cease to supply VBUS within tVBUSOFF of exiting
	 * Attached.SRC.
	 */
	tc_src_power_off(port);

	/* Disable VCONN if not power role swapping */
	if (TC_CHK_FLAG(port, TC_FLAGS_VCONN_ON) &&
	    !TC_CHK_FLAG(port, TC_FLAGS_DO_PR_SWAP))
		set_vconn(port, 0);

	/* Clear PR swap flag after checking for Vconn */
	TC_CLR_FLAG(port, TC_FLAGS_DO_PR_SWAP);
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
static void tc_try_src_entry(const int port)
{
	print_current_state(port);

	tc[port].timeout = get_time().val + PD_T_TRY_TIMEOUT;
}

static void tc_try_src_run(const int port)
{
	const uint64_t now = get_time().val;

	/*
	 * The port shall transition to Attached.SRC when the SRC.Rd state is
	 * detected on exactly one of the CC1 or CC2 pins for at least
	 * tTryCCDebounce.
	 */
	if (now > tc[port].cc_last_change + PD_T_TRY_CC_DEBOUNCE &&
	    tc[port].cc_state == PD_CC_UFP_ATTACHED)
		set_state_tc(port, TC_ATTACHED_SRC);
	/*
	 * The port shall transition to TryWait.SNK after tDRPTry and the
	 * SRC.Rd state has not been detected and VBUS is within vSafe0V,
	 * or after tTryTimeout and the SRC.Rd state has not been detected.
	 */
	else if (now > tc[port].cc_last_change + PD_T_DRP_TRY &&
	    !pd_is_vbus_present(port))
		set_state_tc(port, TC_TRY_WAIT_SNK);
	else if (now > tc[port].timeout)
		set_state_tc(port, TC_TRY_WAIT_SNK);
}

/**
 * TryWait.SNK
 *
 * Super State Entry Actions:
 *   Vconn Off
 *   Place Rd on CC
 *   Set power role to SINK
 */
static void tc_try_wait_snk_entry(const int port)
{
	print_current_state(port);
}

static void tc_try_wait_snk_run(const int port)
{
	const uint64_t now = get_time().val;

	/*
	 * The port shall transition to Attached.SNK after tCCDebounce if or
	 * when VBUS is detected.
	 */
	if (now > tc[port].cc_last_change + PD_T_CC_DEBOUNCE &&
	    pd_is_vbus_present(port))
		set_state_tc(port, TC_ATTACHED_SNK);
	/*
	 * The port shall transition to Unattached.SNK when the state of both
	 * of the CC1 and CC2 pins is SNK.Open for at least tPDDebounce.
	 */
	else if (now > tc[port].cc_last_change + PD_T_PD_DEBOUNCE &&
	    tc[port].cc_state == PD_CC_NONE)
		set_state_tc(port, TC_UNATTACHED_SNK);
}

#endif

#if defined(CONFIG_USB_PE_SM)
/*
 * CTUnattached.SNK
 */
static void tc_ct_unattached_snk_entry(int port)
{
	print_current_state(port);

	/*
	 * Both CC1 and CC2 pins shall be independently terminated to
	 * ground through Rd. Reset last cc change time.
	 */
	tcpm_select_rp_value(port, CONFIG_USB_PD_PULLUP);
	tcpm_set_cc(port, TYPEC_CC_RD);
	tc[port].cc_last_change = get_time().val;

	/* Set power role to sink */
	tc_set_power_role(port, PD_ROLE_SINK);
	tcpm_set_msg_header(port, tc[port].power_role, tc[port].data_role);

	/*
	 * The policy engine is in the disabled state. Disable PD and
	 * re-enable it
	 */
	tc[port].pd_enable = 0;

	tc[port].timeout = get_time().val + PD_POWER_SUPPLY_TURN_ON_DELAY;
}

static void tc_ct_unattached_snk_run(int port)
{
	if (tc[port].timeout > 0 && get_time().val > tc[port].timeout) {
		tc[port].pd_enable = 1;
		tc[port].timeout = 0;
	}

	if (tc[port].timeout > 0)
		return;

	/* Wait until Protocol Layer is ready */
	if (!prl_is_running(port))
		return;

	/*
	 * Hard Reset is sent when the PE layer is disabled due to a
	 * CTVPD connection.
	 */
	if (TC_CHK_FLAG(port, TC_FLAGS_HARD_RESET)) {
		TC_CLR_FLAG(port, TC_FLAGS_HARD_RESET);
		/* Nothing to do. Just signal hard reset completion */
		pe_ps_reset_complete(port);
	}

	/*
	 * The port shall transition to CTAttached.SNK when VBUS is detected.
	 */
	if (pd_is_vbus_present(port)) {
		set_state_tc(port, TC_CT_ATTACHED_SNK);
	/*
	 * The port shall transition to Unattached.SNK if the state of
	 * the CC pin is SNK.Open for tVPDDetach after VBUS is vSafe0V.
	 */
	} else if (get_time().val > tc[port].cc_last_change + PD_T_VPDDETACH &&
	    tc[port].cc_state == PD_CC_NONE) {
		if (IS_ENABLED(CONFIG_USB_PD_ALT_MODE_DFP))
			pd_dfp_exit_mode(port, 0, 0);

		set_state_tc(port, TC_UNATTACHED_SNK);
	}
}

/**
 * CTAttached.SNK
 */
static void tc_ct_attached_snk_entry(int port)
{
	print_current_state(port);

	/* The port shall reject a VCONN swap request. */
	TC_SET_FLAG(port, TC_FLAGS_REJECT_VCONN_SWAP);
}

static void tc_ct_attached_snk_run(int port)
{
	/*
	 * Hard Reset is sent when the PE layer is disabled due to a
	 * CTVPD connection.
	 */
	if (TC_CHK_FLAG(port, TC_FLAGS_HARD_RESET)) {
		TC_CLR_FLAG(port, TC_FLAGS_HARD_RESET);
		/* Nothing to do. Just signal hard reset completion */
		pe_ps_reset_complete(port);
	}

	/*
	 * A port that is not in the process of a USB PD Hard Reset shall
	 * transition to CTUnattached.SNK within tSinkDisconnect when VBUS
	 * falls below vSinkDisconnect
	 */
	if (!pd_is_vbus_present(port)) {
		set_state_tc(port, TC_CT_UNATTACHED_SNK);
		return;
	}

	/*
	 *  The port shall operate in one of the Sink Power Sub-States
	 *  and remain within the Sink Power Sub-States, until either VBUS is
	 *  removed or a USB PD contract is established with the source.
	 */
	if (!pe_is_explicit_contract(port))
		sink_power_sub_states(port);
}

static void tc_ct_attached_snk_exit(int port)
{
	/* Stop drawing power */
	sink_stop_drawing_current(port);

	TC_CLR_FLAG(port, TC_FLAGS_REJECT_VCONN_SWAP);
}
#endif /* CONFIG_USB_PE_SM */

/**
 * Super State CC_OPEN
 */
static void tc_cc_open_entry(const int port)
{
	/* Disable VBUS */
	pd_power_supply_reset(port);

	/* Disable VCONN */
	if (TC_CHK_FLAG(port, TC_FLAGS_VCONN_ON))
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
}

/**
 * Super State CC_RD
 */
static void tc_cc_rd_entry(const int port)
{
	set_rd(port);
}

/**
 * Super State CC_RP
 */
static void tc_cc_rp_entry(const int port)
{
	set_rp(port);
}

/**
 * Super State Unattached
 *
 * Ensures that any time we unattached we can perform an action without
 * repeating it during DRP toggle
 */
static void tc_unattached_entry(const int port)
{
	/* This only prints the first time we enter a unattached state */
	print_current_state(port);

	/* This disables the mux when we disconnect on a port */
	if (IS_ENABLED(CONFIG_USBC_SS_MUX))
		set_usb_mux_with_current_data_role(port);
}

void tc_run(const int port)
{
	run_state(port, &tc[port].ctx);
}

int pd_is_ufp(int port)
{
	/* Returns true if port partner is UFP */
	return tc[port].cc_state == PD_CC_UFP_ATTACHED ||
	       tc[port].cc_state == PD_CC_UFP_DEBUG_ACC ||
	       tc[port].cc_state == PD_CC_UFP_AUDIO_ACC;
}

int pd_is_debug_acc(int port)
{
	return tc[port].cc_state == PD_CC_UFP_DEBUG_ACC ||
	       tc[port].cc_state == PD_CC_DFP_DEBUG_ACC;
}

/*
 * Type-C State Hierarchy (Sub-States are listed inside the boxes)
 *
 * |TC_UNATTACHED ---------|
 * |                       |
 * |    TC_UNATTACHED_SNK  |
 * |    TC_UNATTACHED_SRC  |
 * |-----------------------|
 *
 * |TC_CC_RD --------------|	|TC_CC_RP ------------------------|
 * |			   |	|				  |
 * |	TC_ATTACH_WAIT_SNK |	|	TC_ATTACH_WAIT_SRC        |
 * |	TC_TRY_WAIT_SNK    |	|	TC_TRY_SRC                |
 * |	TC_DBG_ACC_SNK     |	|	TC_UNORIENTED_DBG_ACC_SRC |
 * |	TC_ATTACHED_SNK    |	|	TC_ATTACHED_SRC		  |
 * |-----------------------|	|---------------------------------|
 *
 * |TC_CC_OPEN -----------|
 * |                      |
 * |	TC_DISABLED       |
 * |	TC_ERROR_RECOVERY |
 * |----------------------|
 *
 */
static const struct usb_state tc_states[] = {
	/* Super States */
	[TC_CC_OPEN] = {
		.entry	= tc_cc_open_entry,
	},
	[TC_CC_RD] = {
		.entry	= tc_cc_rd_entry,
	},
	[TC_CC_RP] = {
		.entry	= tc_cc_rp_entry,
	},
	[TC_UNATTACHED] = {
		.entry	= tc_unattached_entry,
	},
	/* Normal States */
	[TC_DISABLED] = {
		.entry	= tc_disabled_entry,
		.run	= tc_disabled_run,
		.exit	= tc_disabled_exit,
		.parent = &tc_states[TC_CC_OPEN],
	},
	[TC_ERROR_RECOVERY] = {
		.entry	= tc_error_recovery_entry,
		.run	= tc_error_recovery_run,
		.parent = &tc_states[TC_CC_OPEN],
	},
	[TC_UNATTACHED_SNK] = {
		.entry	= tc_unattached_snk_entry,
		.run	= tc_unattached_snk_run,
		.parent = &tc_states[TC_UNATTACHED],
	},
	[TC_ATTACH_WAIT_SNK] = {
		.entry	= tc_attach_wait_snk_entry,
		.run	= tc_attach_wait_snk_run,
		.parent = &tc_states[TC_CC_RD],
	},
	[TC_ATTACHED_SNK] = {
		.entry	= tc_attached_snk_entry,
		.run	= tc_attached_snk_run,
		.exit	= tc_attached_snk_exit,
		.parent = &tc_states[TC_CC_RD],
	},
	[TC_UNORIENTED_DBG_ACC_SRC] = {
		.entry	= tc_unoriented_dbg_acc_src_entry,
		.run	= tc_unoriented_dbg_acc_src_run,
		.parent = &tc_states[TC_CC_RP],
	},
	[TC_DBG_ACC_SNK] = {
		.entry	= tc_dbg_acc_snk_entry,
		.run	= tc_dbg_acc_snk_run,
		.parent = &tc_states[TC_CC_RD],
	},
	[TC_UNATTACHED_SRC] = {
		.entry	= tc_unattached_src_entry,
		.run	= tc_unattached_src_run,
		.parent = &tc_states[TC_UNATTACHED],
	},
	[TC_ATTACH_WAIT_SRC] = {
		.entry	= tc_attach_wait_src_entry,
		.run	= tc_attach_wait_src_run,
		.parent = &tc_states[TC_CC_RP],
	},
	[TC_ATTACHED_SRC] = {
		.entry	= tc_attached_src_entry,
		.run	= tc_attached_src_run,
		.exit	= tc_attached_src_exit,
		.parent = &tc_states[TC_CC_RP],
	},
#ifdef CONFIG_USB_PD_TRY_SRC
	[TC_TRY_SRC] = {
		.entry	= tc_try_src_entry,
		.run	= tc_try_src_run,
		.parent = &tc_states[TC_CC_RP],
	},
	[TC_TRY_WAIT_SNK] = {
		.entry	= tc_try_wait_snk_entry,
		.run	= tc_try_wait_snk_run,
		.parent = &tc_states[TC_CC_RD],
	},
#endif /* CONFIG_USB_PD_TRY_SRC */
#ifdef CONFIG_USB_PE_SM
	[TC_CT_UNATTACHED_SNK] = {
		.entry = tc_ct_unattached_snk_entry,
		.run   = tc_ct_unattached_snk_run,
	},
	[TC_CT_ATTACHED_SNK] = {
		.entry = tc_ct_attached_snk_entry,
		.run   = tc_ct_attached_snk_run,
		.exit  = tc_ct_attached_snk_exit,
	},
#endif
};

#ifdef TEST_BUILD
const struct test_sm_data test_tc_sm_data[] = {
	{
		.base = tc_states,
		.size = ARRAY_SIZE(tc_states),
		.names = tc_state_names,
		.names_size = ARRAY_SIZE(tc_state_names),
	},
};
const int test_tc_sm_data_size = ARRAY_SIZE(test_tc_sm_data);
#endif
