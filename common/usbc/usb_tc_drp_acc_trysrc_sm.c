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
#define CPRINTF(format, args...) cprintf(CC_USBPD, format, ## args)
#define CPRINTS(format, args...) cprints(CC_USBPD, format, ## args)
#else /* CONFIG_COMMON_RUNTIME */
#define CPRINTF(format, args...)
#define CPRINTS(format, args...)
#endif

/* Type-C Layer Flags */
/* Flag to note we are sourcing VCONN */
#define TC_FLAGS_VCONN_ON               BIT(0)
/* Flag to note port partner has Rp/Rp or Rd/Rd */
#define TC_FLAGS_TS_DTS_PARTNER         BIT(1)
/* Flag to note VBus input has never been low */
#define TC_FLAGS_VBUS_NEVER_LOW         BIT(2)
/* Flag to note Low Power Mode transition is currently happening */
#define TC_FLAGS_LPM_TRANSITION         BIT(3)
/* Flag to note Low Power Mode is currently on */
#define TC_FLAGS_LPM_ENGAGED            BIT(4)
/* Flag to note Low Power Mode is requested. Not currently used */
#define TC_FLAGS_LPM_REQUESTED          BIT(5)
/* Flag to note CVTPD has been detected */
#define TC_FLAGS_CTVPD_DETECTED         BIT(6)
/* Flag to note request to swap to VCONN on */
#define TC_FLAGS_REQUEST_VC_SWAP_ON     BIT(7)
/* Flag to note request to swap to VCONN off */
#define TC_FLAGS_REQUEST_VC_SWAP_OFF    BIT(8)
/* Flag to note request to swap VCONN is being rejected */
#define TC_FLAGS_REJECT_VCONN_SWAP      BIT(9)
/* Flag to note request to power role swap */
#define TC_FLAGS_REQUEST_PR_SWAP        BIT(10)
/* Flag to note request to data role swap */
#define TC_FLAGS_REQUEST_DR_SWAP        BIT(11)
/* Flag to note request to power off sink */
#define TC_FLAGS_POWER_OFF_SNK          BIT(12)
/* Flag to note port partner has unconstrained power */
#define TC_FLAGS_PARTNER_UNCONSTRAINED  BIT(13)
/* Flag to note port partner is Dual Role Data */
#define TC_FLAGS_PARTNER_DR_DATA        BIT(14)
/* Flag to note port partner is Dual Role Power */
#define TC_FLAGS_PARTNER_DR_POWER       BIT(15)
/* Flag to note port partner is Power Delivery capable */
#define TC_FLAGS_PARTNER_PD_CAPABLE     BIT(16)
/* Flag to note hard reset has been triggered */
#define TC_FLAGS_HARD_RESET             BIT(17)
/* Flag to note port partner is USB comms capable */
#define TC_FLAGS_PARTNER_USB_COMM       BIT(18)
/* Flag to note we are currently performing PR Swap */
#define TC_FLAGS_PR_SWAP_IN_PROGRESS    BIT(19)
/* Flag to note we need to perform PR Swap */
#define TC_FLAGS_DO_PR_SWAP             BIT(20)
/* Flag to note we are performing Discover Identity */
#define TC_FLAGS_DISC_IDENT_IN_PROGRESS BIT(21)
/* Flag to note we should wake from LPM */
#define TC_FLAGS_WAKE_FROM_LPM          BIT(22)
/* Flag to note the TCPM supports auto toggle */
#define TC_FLAGS_AUTO_TOGGLE_SUPPORTED  BIT(23)

/*
 * Clear all flags except TC_FLAGS_AUTO_TOGGLE_SUPPORTED,
 * TC_FLAGS_LPM_REQUESTED, and TC_FLAGS_LPM_ENGAGED if
 * they are set.
 */
#define CLR_ALL_BUT_LPM_FLAGS(port) (tc[port].flags &= \
	(TC_FLAGS_AUTO_TOGGLE_SUPPORTED | \
	TC_FLAGS_LPM_REQUESTED | \
	TC_FLAGS_LPM_ENGAGED))

/* 100 ms is enough time for any TCPC transaction to complete. */
#define PD_LPM_DEBOUNCE_US (100 * MSEC)

/*
 * The TypeC state machine uses this bit to disable/enable PD
 * This bit corresponds to bit-0 of pd_disabled_mask
 */
#define PD_DISABLED_NO_CONNECTION  BIT(0)
/*
 * Console and Host commands use this bit to override the
 * PD_DISABLED_NO_CONNECTION bit that was set by the TypeC
 * state machine.
 * This bit corresponds to bit-1 of pd_disabled_mask
 */
#define PD_DISABLED_BY_POLICY       BIT(1)

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
#ifdef CONFIG_USB_PD_DUAL_ROLE_AUTO_TOGGLE
	TC_DRP_AUTO_TOGGLE,
#endif
#ifdef CONFIG_USB_PD_TCPC_LOW_POWER
	TC_LOW_POWER_MODE,
#endif
#ifdef CONFIG_USB_PE_SM
	TC_CT_UNATTACHED_SNK,
	TC_CT_ATTACHED_SNK,
#endif
	/* Super States */
	TC_CC_OPEN,
	TC_CC_RD,
	TC_CC_RP,
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
#ifdef CONFIG_USB_PD_DUAL_ROLE_AUTO_TOGGLE
	[TC_DRP_AUTO_TOGGLE] = "DRPAutoToggle",
#endif
#ifdef CONFIG_USB_PD_TCPC_LOW_POWER
	[TC_LOW_POWER_MODE] = "LowPowerMode",
#endif
#ifdef CONFIG_USB_PE_SM
	[TC_CT_UNATTACHED_SNK] =  "CTUnattached.SNK",
	[TC_CT_ATTACHED_SNK] = "CTAttached.SNK",
#endif
	/* Super States */
	[TC_CC_OPEN] = "SS:CC_OPEN",
	[TC_CC_RD] = "SS:CC_RD",
	[TC_CC_RP] = "SS:CC_RP",
};
#endif

/* Generate a compiler error if invalid states are referenced */
#ifndef CONFIG_USB_PD_TRY_SRC
#define TC_TRY_SRC	TC_TRY_SRC_UNDEFINED
#define TC_TRY_WAIT_SNK	TC_TRY_WAIT_SNK_UNDEFINED
#endif

static struct type_c {
	/* state machine context */
	struct sm_ctx ctx;
	/* current port power role (SOURCE or SINK) */
	enum pd_power_role power_role;
	/* current port data role (DFP or UFP) */
	enum pd_data_role data_role;
	/*
	 * Higher-level power deliver state machines are enabled if false,
	 * else they're disabled if bits PD_DISABLED_NO_CONNECTION or
	 * PD_DISABLED_BY_POLICY are set.
	 */
	uint32_t pd_disabled_mask;
	/*
	 * Timer for handling TOGGLE_OFF/FORCE_SINK mode when auto-toggle
	 * enabled. See drp_auto_toggle_next_state() for details.
	 */
	uint64_t drp_sink_time;
#ifdef CONFIG_USB_PE_SM
	/* Power supply reset sequence during a hard reset */
	enum ps_reset_sequence ps_reset_state;
#endif
	/* Port polarity */
	enum tcpc_cc_polarity polarity;
	/* port flags, see TC_FLAGS_* */
	uint32_t flags;
	/* event timeout */
	uint64_t evt_timeout;
	/* Time a port shall wait before it can determine it is attached */
	uint64_t cc_debounce;
	/*
	 * Time a Sink port shall wait before it can determine it is detached
	 * due to the potential for USB PD signaling on CC as described in
	 * the state definitions.
	 */
	uint64_t pd_debounce;
#ifdef CONFIG_USB_PD_TRY_SRC
	/*
	 * Time a port shall wait before it can determine it is
	 * re-attached during the try-wait process.
	 */
	uint64_t try_wait_debounce;
#endif
	/* The cc state */
	enum pd_cc_states cc_state;
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
	/* Voltage on CC pin */
	enum tcpc_cc_voltage_status cc_voltage;
	/* Type-C current */
	typec_current_t typec_curr;
	/* Type-C current change */
	typec_current_t typec_curr_change;
	/* Attached ChromeOS device id, RW hash, and current RO / RW image */
	uint16_t dev_id;
	uint32_t dev_rw_hash[PD_RW_HASH_SIZE/4];
	enum ec_image current_image;
} tc[CONFIG_USB_PD_PORT_MAX_COUNT];

/* Port dual-role state */
static volatile __maybe_unused
enum pd_dual_role_states drp_state[CONFIG_USB_PD_PORT_MAX_COUNT] = {
	[0 ... (CONFIG_USB_PD_PORT_MAX_COUNT - 1)] =
		CONFIG_USB_PD_INITIAL_DRP_STATE};

#ifdef CONFIG_USBC_VCONN
static void set_vconn(int port, int enable);
#endif

#ifdef CONFIG_USB_PE_SM

#ifdef CONFIG_USB_PD_ALT_MODE_DFP
/* Tracker for which task is waiting on sysjump prep to finish */
static volatile task_id_t sysjump_task_waiting = TASK_ID_INVALID;
#endif

/* Forward declare common, private functions */
static __maybe_unused int reset_device_and_notify(int port);

#ifdef CONFIG_POWER_COMMON
static void handle_new_power_state(int port);
#endif /* CONFIG_POWER_COMMON */

static void pd_update_dual_role_config(int port);
#endif /* CONFIG_USB_PE_SM */

/* Forward declare common, private functions */
static void set_state_tc(const int port, const enum usb_tc_state new_state);
test_export_static enum usb_tc_state get_state_tc(const int port);

#ifdef CONFIG_USB_PD_TRY_SRC
/* Enable variable for Try.SRC states */
static uint32_t pd_try_src;
static volatile enum try_src_override_t pd_try_src_override;
static void pd_update_try_source(void);
#endif

static void sink_stop_drawing_current(int port);

static bool is_try_src_enabled(int port)
{
	return IS_ENABLED(CONFIG_USB_PD_TRY_SRC) &&
		((pd_try_src_override == TRY_SRC_OVERRIDE_ON) ||
		(pd_try_src_override == TRY_SRC_NO_OVERRIDE && pd_try_src));
}

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

void tc_request_power_swap(int port)
{
	if (IS_ENABLED(CONFIG_USB_PE_SM)) {
		/*
		 * Must be in Attached.SRC, Attached.SNK, UnorientedDbgAcc.SRC,
		 * or DbgAcc.SNK, when this function is called.
		 */
		if (get_state_tc(port) == TC_ATTACHED_SRC ||
				get_state_tc(port) == TC_ATTACHED_SNK ||
				get_state_tc(port) == TC_DBG_ACC_SNK ||
				get_state_tc(port) ==
					TC_UNORIENTED_DBG_ACC_SRC) {
			TC_SET_FLAG(port, TC_FLAGS_PR_SWAP_IN_PROGRESS);
		}
	}
}

static void tc_policy_pd_enable(int port, int en)
{
	if (en)
		atomic_clear(&tc[port].pd_disabled_mask, PD_DISABLED_BY_POLICY);
	else
		atomic_or(&tc[port].pd_disabled_mask, PD_DISABLED_BY_POLICY);
}

static void tc_enable_pd(int port, int en)
{
	if (en)
		atomic_clear(&tc[port].pd_disabled_mask,
						PD_DISABLED_NO_CONNECTION);
	else
		atomic_or(&tc[port].pd_disabled_mask,
						PD_DISABLED_NO_CONNECTION);

}

static void tc_enable_try_src(int en)
{
	if (en)
		atomic_or(&pd_try_src, 1);
	else
		atomic_clear(&pd_try_src, 1);
}

static inline void pd_set_dual_role_no_wakeup(int port,
				enum pd_dual_role_states state)
{
	drp_state[port] = state;

	if (IS_ENABLED(CONFIG_USB_PD_TRY_SRC))
		pd_update_try_source();
}

void pd_set_dual_role(int port, enum pd_dual_role_states state)
{
	pd_set_dual_role_no_wakeup(port, state);

	/* Wake task up to process change */
	task_set_event(PD_PORT_TO_TASK_ID(port),
			PD_EVENT_UPDATE_DUAL_ROLE, 0);
}

#ifdef CONFIG_USB_PE_SM
bool pd_get_partner_data_swap_capable(int port)
{
	/* return data swap capable status of port partner */
	return !!TC_CHK_FLAG(port, TC_FLAGS_PARTNER_DR_DATA);
}

int pd_comm_is_enabled(int port)
{
	return tc_get_pd_enabled(port);
}

void pd_send_vdm(int port, uint32_t vid, int cmd, const uint32_t *data,
							int count)
{
	pe_send_vdm(port, vid, cmd, data, count);
}

void pd_request_data_swap(int port)
{
	/*
	 * Must be in Attached.SRC, Attached.SNK, DebugAccessory.SNK,
	 * or UnorientedDebugAccessory.SRC when this function
	 * is called
	 */
	if (get_state_tc(port) == TC_ATTACHED_SRC ||
			get_state_tc(port) == TC_ATTACHED_SNK ||
			get_state_tc(port) == TC_DBG_ACC_SNK ||
			get_state_tc(port) == TC_UNORIENTED_DBG_ACC_SRC) {
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
bool pd_capable(int port)
{
	return !!TC_CHK_FLAG(port, TC_FLAGS_PARTNER_PD_CAPABLE);
}

/*
 * Return true if partner port is capable of communication over USB data
 * lines.
 */
bool pd_get_partner_usb_comm_capable(int port)
{
	return !!TC_CHK_FLAG(port, TC_FLAGS_PARTNER_USB_COMM);
}

enum pd_dual_role_states pd_get_dual_role(int port)
{
	return drp_state[port];
}

#ifdef CONFIG_CMD_PD_DEV_DUMP_INFO
static inline void pd_dev_dump_info(uint16_t dev_id, uint32_t *hash)
{
	int j;

	ccprintf("DevId:%d.%d Hash:", HW_DEV_ID_MAJ(dev_id),
		 HW_DEV_ID_MIN(dev_id));
	for (j = 0; j < PD_RW_HASH_SIZE / 4; j++)
		ccprintf(" %08x ", hash[i]);
	ccprintf("\n");
}
#endif /* CONFIG_CMD_PD_DEV_DUMP_INFO */

int pd_dev_store_rw_hash(int port, uint16_t dev_id, uint32_t *rw_hash,
					uint32_t current_image)
{
	int i;

	tc[port].dev_id = dev_id;
	memcpy(tc[port].dev_rw_hash, rw_hash, PD_RW_HASH_SIZE);
#ifdef CONFIG_CMD_PD_DEV_DUMP_INFO
	pd_dev_dump_info(dev_id, rw_hash);
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

void pd_dev_get_rw_hash(int port, uint16_t *dev_id, uint8_t *rw_hash,
			 uint32_t *current_image)
{
	*dev_id = tc[port].dev_id;
	*current_image = tc[port].current_image;
	if (*dev_id)
		memcpy(rw_hash, tc[port].dev_rw_hash, PD_RW_HASH_SIZE);
}

void pd_got_frs_signal(int port)
{
	pe_got_frs_signal(port);
}

const char *tc_get_current_state(int port)
{
	return tc_state_names[get_state_tc(port)];
}

uint32_t tc_get_flags(int port)
{
	return tc[port].flags;
}

void tc_print_dev_info(int port)
{
	int i;

	ccprintf("Hash ");
	for (i = 0; i < PD_RW_HASH_SIZE / 4; i++)
		ccprintf("%08x ", tc[port].dev_rw_hash[i]);

	ccprintf("\nImage %s\n", ec_image_to_string(
		tc[port].current_image));
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

void tc_partner_unconstrainedpower(int port, int en)
{
	if (en)
		TC_SET_FLAG(port, TC_FLAGS_PARTNER_UNCONSTRAINED);
	else
		TC_CLR_FLAG(port, TC_FLAGS_PARTNER_UNCONSTRAINED);
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

void pd_try_vconn_src(int port)
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

void tc_try_src_override(enum try_src_override_t ov)
{
	if (IS_ENABLED(CONFIG_USB_PD_TRY_SRC)) {
		switch (ov) {
		case TRY_SRC_OVERRIDE_OFF: /* 0 */
			pd_try_src_override = TRY_SRC_OVERRIDE_OFF;
			break;
		case TRY_SRC_OVERRIDE_ON: /* 1 */
			pd_try_src_override = TRY_SRC_OVERRIDE_ON;
			break;
		default:
			pd_try_src_override = TRY_SRC_NO_OVERRIDE;
		}
	}
}

enum try_src_override_t tc_get_try_src_override(void)
{
	return pd_try_src_override;
}

void tc_snk_power_off(int port)
{
	if (get_state_tc(port) == TC_ATTACHED_SNK ||
			get_state_tc(port) == TC_DBG_ACC_SNK) {
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
	if (get_state_tc(port) == TC_ATTACHED_SRC ||
			get_state_tc(port) == TC_UNORIENTED_DBG_ACC_SRC) {
		/* Remove VBUS */
		pd_power_supply_reset(port);

		if (IS_ENABLED(CONFIG_CHARGE_MANAGER))
			charge_manager_set_ceil(port, CEIL_REQUESTOR_PD,
						CHARGE_CEIL_NONE);
	}
}

void pd_set_suspend(int port, int suspend)
{
	if (pd_is_port_enabled(port) == !suspend)
		return;

	/* TODO(crbug/1052432): This function should use flags to influence the
	 * TC state machine, not call set_state_tc directly.
	 */
	set_state_tc(port,
		suspend ? TC_DISABLED : TC_UNATTACHED_SNK);
	/* If the state was TC_DISABLED, pd_task needs to be awakened to respond
	 * to the state change.
	 */
	task_wake(PD_PORT_TO_TASK_ID(port));
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

enum tcpc_cc_polarity pd_get_polarity(int port)
{
	return tc[port].polarity;
}

enum pd_data_role pd_get_data_role(int port)
{
	return tc[port].data_role;
}

enum pd_power_role pd_get_power_role(int port)
{
	return tc[port].power_role;
}

enum pd_cc_states pd_get_task_cc_state(int port)
{
	return tc[port].cc_state;
}

uint8_t pd_get_task_state(int port)
{
	return get_state_tc(port);
}

bool pd_get_vconn_state(int port)
{
	return !!TC_CHK_FLAG(port, TC_FLAGS_VCONN_ON);
}

bool pd_get_partner_dual_role_power(int port)
{
	return !!TC_CHK_FLAG(port, TC_FLAGS_PARTNER_DR_POWER);
}

bool pd_get_partner_unconstr_power(int port)
{
	return !!TC_CHK_FLAG(port, TC_FLAGS_PARTNER_UNCONSTRAINED);
}

const char *pd_get_task_state_name(int port)
{
	return tc_state_names[get_state_tc(port)];
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

bool pd_is_disconnected(int port)
{
	return !pd_is_connected(port);
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
		for (i = 0; i < board_get_usb_pd_port_count(); i++) {
			/*
			 * We can't be in an alternate mode if PD comm is
			 * disabled, so no need to send the event
			 */
			if (tc_get_pd_enabled(i))
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

static __maybe_unused void bc12_role_change_handler(int port)
{
	int event;
	int task_id = USB_CHG_PORT_TO_TASK_ID(port);

	/* Get the data role of our device */
	switch (pd_get_data_role(port)) {
	case PD_ROLE_UFP:
		event = USB_CHG_EVENT_DR_UFP;
		break;
	case PD_ROLE_DFP:
		event = USB_CHG_EVENT_DR_DFP;
		break;
	case PD_ROLE_DISCONNECTED:
		event = USB_CHG_EVENT_CC_OPEN;
		break;
	default:
		return;
	}
	task_set_event(task_id, event, 0);
}

#ifdef CONFIG_USB_PE_SM
/*
 * This function performs a source hard reset. It should be called
 * repeatedly until a true value is returned, signaling that the
 * source hard reset is complete. A false value is returned otherwise.
 */
static bool tc_perform_src_hard_reset(int port)
{
	switch (tc[port].ps_reset_state) {
	case PS_STATE0:
		/* Remove VBUS */
		tc_src_power_off(port);

		/* Turn off VCONN */
		set_vconn(port, 0);

		/* Set role to DFP */
		tc_set_data_role(port, PD_ROLE_DFP);

		tc[port].ps_reset_state = PS_STATE1;
		tc[port].timeout = get_time().val + PD_T_SRC_RECOVER;
		return false;
	case PS_STATE1:
		/* Enable VBUS */
		pd_set_power_supply_ready(port);

		/* Turn off VCONN */
		set_vconn(port, 1);

		tc[port].ps_reset_state = PS_STATE3;
		tc[port].timeout = get_time().val +
				PD_POWER_SUPPLY_TURN_ON_DELAY;
		return false;
	case PS_STATE2:
	case PS_STATE3:
		/* Tell Policy Engine Hard Reset is complete */
		pe_ps_reset_complete(port);

		tc[port].ps_reset_state = PS_STATE0;
		return true;
	}

	/*
	 * This return is added to appease the compiler. It should
	 * never be reached because the switch handles all possible
	 * cases of the enum ps_reset_sequence type.
	 */
	return true;
}
#endif

static void tc_perform_snk_hard_reset(int port)
{
	tc_set_data_role(port, PD_ROLE_UFP);

	/* Clear the input current limit */
	sink_stop_drawing_current(port);

	/*
	 * When VCONN is supported, the Hard Reset Shall cause
	 * the Port with the Rd resistor asserted to turn off
	 * VCONN.
	 */
#ifdef CONFIG_USBC_VCONN
	if (TC_CHK_FLAG(port, TC_FLAGS_VCONN_ON))
		set_vconn(port, 0);
#endif

	/*
	 * Inform policy engine that power supply
	 * reset is complete
	 */
	pe_ps_reset_complete(port);
}

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

#ifdef CONFIG_USB_PD_DUAL_ROLE_AUTO_TOGGLE
	/*
	 * Some TCPCs may not support DRP Auto Toggle, so query the
	 * query the TCPC for DRP Auto toggle support.
	 */
	if (tcpm_auto_toggle_supported(port))
		TC_SET_FLAG(port, TC_FLAGS_AUTO_TOGGLE_SUPPORTED);
#endif

#ifdef CONFIG_USB_PE_SM
	tc_enable_pd(port, 0);
	tc[port].ps_reset_state = PS_STATE0;
#endif
}

void tc_state_init(int port)
{
	/* Unattached.SNK is the default starting state. */
	restart_tc_sm(port, TC_UNATTACHED_SNK);

	/*
	 * If the TCPC isn't accessed, it will enter low power mode
	 * after PD_LPM_DEBOUNCE_US.
	 */
	tc[port].low_power_time = get_time().val + PD_LPM_DEBOUNCE_US;

	/* Allow system to set try src enable */
	tc_try_src_override(TRY_SRC_NO_OVERRIDE);

	/*
	 * Allowing PD by policy and host or console commands
	 * can disable PD by policy later.
	 */
	tc_policy_pd_enable(port, 1);
}

enum pd_cable_plug tc_get_cable_plug(int port)
{
	/*
	 * Messages sent by this state machine are always from a DFP/UFP,
	 * i.e. the chromebook.
	 */
	return PD_PLUG_FROM_DFP_UFP;
}

void pd_comm_enable(int port, int en)
{
	tc_policy_pd_enable(port, en);
}

uint8_t tc_get_polarity(int port)
{
	return tc[port].polarity;
}

uint8_t tc_get_pd_enabled(int port)
{
	return !tc[port].pd_disabled_mask;
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

/* Get the previous TypeC state. */
static enum usb_tc_state get_last_state_tc(const int port)
{
	return tc[port].ctx.previous - &tc_states[0];
}

static void print_current_state(const int port)
{
	CPRINTS("C%d: %s", port, tc_state_names[get_state_tc(port)]);
}

#ifdef CONFIG_USB_PE_SM
static void handle_device_access(int port)
{
	tc[port].low_power_time = get_time().val + PD_LPM_DEBOUNCE_US;
}
#endif

void tc_event_check(int port, int evt)
{
#ifdef CONFIG_USB_PE_SM
	if (IS_ENABLED(CONFIG_USB_PD_TCPC_LOW_POWER)) {
		if (evt & PD_EXIT_LOW_POWER_EVENT_MASK)
			TC_SET_FLAG(port, TC_FLAGS_WAKE_FROM_LPM);
		if (evt & PD_EVENT_DEVICE_ACCESSED)
			handle_device_access(port);
	}

	/* if TCPC has reset, then need to initialize it again */
	if (evt & PD_EVENT_TCPC_RESET)
		reset_device_and_notify(port);

#ifdef CONFIG_POWER_COMMON
	if (IS_ENABLED(CONFIG_POWER_COMMON)) {
		if (evt & PD_EVENT_POWER_STATE_CHANGE)
			handle_new_power_state(port);
	}
#endif /* CONFIG_POWER_COMMON */
#ifdef CONFIG_USB_PD_ALT_MODE_DFP
	if (IS_ENABLED(CONFIG_USB_PD_ALT_MODE_DFP)) {
		if (evt & PD_EVENT_SYSJUMP) {
			pe_exit_dp_mode(port);
			notify_sysjump_ready(&sysjump_task_waiting);
		}
	}
#endif

	if (evt & PD_EVENT_UPDATE_DUAL_ROLE)
		pd_update_dual_role_config(port);
#endif
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

	/*
	 * For BC1.2 detection that is triggered on data role change events
	 * instead of VBUS changes, need to set an event to wake up the USB_CHG
	 * task and indicate the current data role.
	 */
	if (IS_ENABLED(CONFIG_BC12_DETECT_DATA_ROLE_TRIGGER))
		bc12_role_change_handler(port);

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
static void pd_update_try_source(void)
{
	tc_enable_try_src(pd_is_try_source_capable());
}
DECLARE_HOOK(HOOK_BATTERY_SOC_CHANGE, pd_update_try_source, HOOK_PRIO_DEFAULT);
#endif /* CONFIG_USB_PD_TRY_SRC */

static void set_vconn(int port, int enable)
{
	if (enable)
		TC_SET_FLAG(port, TC_FLAGS_VCONN_ON);
	else
		TC_CLR_FLAG(port, TC_FLAGS_VCONN_ON);

	/*
	 * TODO(chromium:951681): When we are sourcing VCONN, we should make
	 * sure to remove our termination on that CC line first.
	 */

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

#ifdef CONFIG_USB_PE_SM
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

#ifdef CONFIG_POWER_COMMON
static void handle_new_power_state(int port)
{
	if (IS_ENABLED(CONFIG_POWER_COMMON) &&
	    IS_ENABLED(CONFIG_USB_PE_SM)) {
		if (chipset_in_or_transitioning_to_state(
					CHIPSET_STATE_ANY_OFF)) {
			/*
			 * The SoC will negotiated DP mode again when it
			 * boots up
			 */
			pe_exit_dp_mode(port);

			/*
			 * Reset mux to USB. DP mode is selected
			 * again at boot up.
			 */
			set_usb_mux_with_current_data_role(port);
		} else if (chipset_in_or_transitioning_to_state(
					CHIPSET_STATE_ON)) {
			/* Enter any previously exited alt modes */
			pe_dpm_request(port, DPM_REQUEST_PORT_DISCOVERY);
		}
	}
}
#endif /* CONFIG_POWER_COMMON */

/*
 * HOST COMMANDS
 */
#ifdef HAS_TASK_HOSTCMD

static enum ec_status hc_remote_flash(struct host_cmd_handler_args *args)
{
	const struct ec_params_usb_pd_fw_update *p = args->params;
	int port = p->port;
	int rv = EC_RES_SUCCESS;
	const uint32_t *data = &(p->size) + 1;
	int i, size;

	if (port >= board_get_usb_pd_port_count())
		return EC_RES_INVALID_PARAM;

	if (p->size + sizeof(*p) > args->params_size)
		return EC_RES_INVALID_PARAM;

#if defined(CONFIG_BATTERY) && \
	(defined(CONFIG_BATTERY_PRESENT_CUSTOM) ||   \
	 defined(CONFIG_BATTERY_PRESENT_GPIO))
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

void pd_request_vconn_swap(int port)
{
	pe_dpm_request(port, DPM_REQUEST_VCONN_SWAP);
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

static __maybe_unused int reset_device_and_notify(int port)
{
	int rv;
	int task, waiting_tasks;

	/* This should only be called from the PD task */
	assert(port == TASK_ID_TO_PD_PORT(task_get_current()));

	rv = tc_restart_tcpc(port);

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

	/* Wake up all waiting tasks. */
	while (waiting_tasks) {
		task = __fls(waiting_tasks);
		waiting_tasks &= ~BIT(task);
		task_set_event(task, TASK_EVENT_PD_AWAKE, 0);
	}

	return rv;
}

#ifdef CONFIG_USB_PD_TCPC_LOW_POWER
void pd_wait_exit_low_power(int port)
{
	if (TC_CHK_FLAG(port, TC_FLAGS_LPM_ENGAGED)) {
		TC_SET_FLAG(port, TC_FLAGS_WAKE_FROM_LPM);

		if (port != TASK_ID_TO_PD_PORT(task_get_current())) {
			/*
			 * Otherwise, we need to wait for the TCPC reset to
			 * complete
			 */
			atomic_or(&tc[port].tasks_waiting_on_reset,
				1 << task_get_current());
			/*
			 * NOTE: We could be sending the PD task the reset
			 * event while it is already processing the reset event.
			 * If that occurs, then we will reset the TCPC multiple
			 * times, which is undesirable but most likely benign.
			 * Empirically, this doesn't happen much, but it if
			 * starts occurring, we can add a guard to
			 * prevent/reduce it.
			 */
			task_set_event(PD_PORT_TO_TASK_ID(port),
					PD_EVENT_TCPC_RESET, 0);
			task_wait_event_mask(TASK_EVENT_PD_AWAKE, -1);
		}
	}
}

/*
 * This can be called from any task. If we are in the PD task, we can handle
 * immediately. Otherwise, we need to notify the PD task via event.
 */
void pd_device_accessed(int port)
{
	if (port == TASK_ID_TO_PD_PORT(task_get_current()))
		handle_device_access(port);
	else
		task_set_event(PD_PORT_TO_TASK_ID(port),
			PD_EVENT_DEVICE_ACCESSED, 0);
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

/**
 * Unattached.SNK
 */
static void tc_unattached_snk_entry(const int port)
{
	if (get_last_state_tc(port) != TC_UNATTACHED_SRC) {
		/* Detect USB PD cc disconnect */
		hook_notify(HOOK_USB_PD_DISCONNECT);
		print_current_state(port);
	}

	tc[port].data_role = PD_ROLE_DISCONNECTED;

	/*
	 * When data role set events are used to enable BC1.2, then CC
	 * detach events are used to notify BC1.2 that it can be powered
	 * down.
	 */
	if (IS_ENABLED(CONFIG_BC12_DETECT_DATA_ROLE_TRIGGER))
		bc12_role_change_handler(port);

	if (IS_ENABLED(CONFIG_CHARGE_MANAGER))
		charge_manager_update_dualrole(port, CAP_UNKNOWN);

	/*
	 * Indicate that the port is disconnected so the board
	 * can restore state from any previous data swap.
	 */
	pd_execute_data_swap(port, PD_ROLE_DISCONNECTED);
	tc[port].next_role_swap = get_time().val + PD_T_DRP_SNK;

	if (IS_ENABLED(CONFIG_USBC_SS_MUX))
		usb_mux_set(port, USB_PD_MUX_NONE,
			USB_SWITCH_DISCONNECT, tc[port].polarity);

	if (IS_ENABLED(CONFIG_USB_PE_SM)) {
		CLR_ALL_BUT_LPM_FLAGS(port);
		tc_enable_pd(port, 0);
	}
}

static void tc_unattached_snk_run(const int port)
{
	enum tcpc_cc_voltage_status cc1, cc2;

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

	/* Check for connection */
	tcpm_get_cc(port, &cc1, &cc2);

#ifdef CONFIG_USB_PD_DUAL_ROLE_AUTO_TOGGLE
	/*
	 * Attempt TCPC auto DRP toggle if it is
	 * not already auto toggling.
	 */
	if (drp_state[port] == PD_DRP_TOGGLE_ON &&
		TC_CHK_FLAG(port, TC_FLAGS_AUTO_TOGGLE_SUPPORTED) &&
		cc_is_open(cc1, cc2)) {

		/*
		 * We are disconnected and going to DRP
		 *     PC.AutoDischargeDisconnect=0b
		 *     Set RC.DRP=1b (DRP)
		 *     Set RC.CC1=10b (Rd)
		 *     Set RC.CC2=10b (Rd)
		 */
		tcpm_enable_auto_discharge_disconnect(port, 0);
		tcpm_set_connection(port, TYPEC_CC_RD, 0);

		set_state_tc(port, TC_DRP_AUTO_TOGGLE);
		return;
	}
#endif

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
		set_state_tc(port, TC_ATTACH_WAIT_SNK);
	} else if (get_time().val > tc[port].next_role_swap &&
		drp_state[port] == PD_DRP_TOGGLE_ON) {
		/* DRP Toggle */
		set_state_tc(port, TC_UNATTACHED_SRC);
	}

#ifdef CONFIG_USB_PD_TCPC_LOW_POWER
	else if (drp_state[port] == PD_DRP_FORCE_SINK ||
		drp_state[port] == PD_DRP_TOGGLE_OFF) {

		/*
		 * We are disconnecting without DRP.
		 *     PC.AutoDischargeDisconnect=0b
		 */
		tcpm_enable_auto_discharge_disconnect(port, 0);

		set_state_tc(port, TC_LOW_POWER_MODE);
	}
#endif
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

	tc[port].cc_state = PD_CC_UNSET;
}

static void tc_attach_wait_snk_run(const int port)
{
	enum tcpc_cc_voltage_status cc1, cc2;
	enum pd_cc_states new_cc_state;

	/* Check for connection */
	tcpm_get_cc(port, &cc1, &cc2);

	if (cc_is_rp(cc1) && cc_is_rp(cc2))
		new_cc_state = PD_CC_DFP_DEBUG_ACC;
	else if (cc_is_rp(cc1) || cc_is_rp(cc2))
		new_cc_state = PD_CC_DFP_ATTACHED;
	else
		new_cc_state = PD_CC_NONE;

	/* Debounce the cc state */
	if (new_cc_state != tc[port].cc_state) {
		tc[port].cc_debounce = get_time().val + PD_T_CC_DEBOUNCE;
		tc[port].pd_debounce = get_time().val + PD_T_PD_DEBOUNCE;
		tc[port].cc_state = new_cc_state;
		return;
	}

	/*
	 * A DRP shall transition to Unattached.SNK when the state of both
	 * the CC1 and CC2 pins is SNK.Open for at least tPDDebounce.
	 */
	if (new_cc_state == PD_CC_NONE &&
				get_time().val > tc[port].pd_debounce) {
		if (IS_ENABLED(CONFIG_USB_PE_SM) &&
				IS_ENABLED(CONFIG_USB_PD_ALT_MODE_DFP)) {
			pd_dfp_exit_mode(port, 0, 0);
		}

		/* We are detached */
		set_state_tc(port, TC_UNATTACHED_SRC);
		return;
	}

	/* Wait for CC debounce */
	if (get_time().val < tc[port].cc_debounce)
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
		if (new_cc_state == PD_CC_DFP_ATTACHED) {
			if (is_try_src_enabled(port))
				set_state_tc(port, TC_TRY_SRC);
			else
				set_state_tc(port, TC_ATTACHED_SNK);
		} else {
			/* new_cc_state is PD_CC_DFP_DEBUG_ACC */
			TC_SET_FLAG(port, TC_FLAGS_TS_DTS_PARTNER);
			set_state_tc(port, TC_DBG_ACC_SNK);
		}

		if (IS_ENABLED(CONFIG_USB_PE_SM) &&
				IS_ENABLED(CONFIG_USB_PD_ALT_MODE_DFP)) {
			hook_call_deferred(&pd_usb_billboard_deferred_data,
								PD_T_AME);
		}
	}
}

/**
 * Attached.SNK
 */
static void tc_attached_snk_entry(const int port)
{
	enum tcpc_cc_voltage_status cc1, cc2;

	print_current_state(port);

	/* Clear Low Power Mode Request */
	TC_CLR_FLAG(port, TC_FLAGS_LPM_REQUESTED);

#ifdef CONFIG_USB_PE_SM
	if (TC_CHK_FLAG(port, TC_FLAGS_PR_SWAP_IN_PROGRESS)) {
		/*
		 * Both CC1 and CC2 pins shall be independently terminated to
		 * ground through Rd.
		 */
		tcpm_set_cc(port, TYPEC_CC_RD);

		/* Change role to sink */
		tc_set_power_role(port, PD_ROLE_SINK);
		tcpm_set_msg_header(port, tc[port].power_role,
							tc[port].data_role);

		/*
		 * Maintain VCONN supply state, whether ON or OFF, and its
		 * data role / usb mux connections.
		 */
	} else
#endif
	{
		/* Get connector orientation */
		tcpm_get_cc(port, &cc1, &cc2);
		tc[port].polarity = get_snk_polarity(cc1, cc2);
		pd_set_polarity(port, tc[port].polarity);

		/*
		 * Initial data role for sink is UFP
		 * This also sets the usb mux
		 */
		tc_set_data_role(port, PD_ROLE_UFP);

		hook_notify(HOOK_USB_PD_CONNECT);

		if (IS_ENABLED(CONFIG_CHARGE_MANAGER)) {
			tc[port].typec_curr =
			usb_get_typec_current_limit(tc[port].polarity,
								cc1, cc2);
			typec_set_input_current_limit(port,
					tc[port].typec_curr, TYPE_C_VOLTAGE);
			charge_manager_update_dualrole(port,
				pd_is_port_partner_dualrole(port) ?
				CAP_DUALROLE : CAP_DEDICATED);
		}
	}

	/* Apply Rd */
	tcpm_set_cc(port, TYPEC_CC_RD);

	tc[port].cc_debounce = 0;

	/* Enable PD */
	if (IS_ENABLED(CONFIG_USB_PE_SM))
		tc_enable_pd(port, 1);
}

static void tc_attached_snk_run(const int port)
{
#ifdef CONFIG_USB_PE_SM
	/*
	 * Perform Hard Reset
	 */
	if (TC_CHK_FLAG(port, TC_FLAGS_HARD_RESET)) {
		TC_CLR_FLAG(port, TC_FLAGS_HARD_RESET);
		tc_perform_snk_hard_reset(port);
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
	if (tc_get_pd_enabled(port) && prl_is_running(port)) {
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
	if (!TC_CHK_FLAG(port, TC_FLAGS_DO_PR_SWAP)) {
		/*
		 * If supplying VCONN, the port shall cease to supply
		 * it within tVCONNOFF of exiting Attached.SNK if not
		 * PR swapping.
		 */
		if (TC_CHK_FLAG(port, TC_FLAGS_VCONN_ON))
			set_vconn(port, 0);
	}

	/* Clear flags after checking Vconn status */
	TC_CLR_FLAG(port, TC_FLAGS_DO_PR_SWAP | TC_FLAGS_POWER_OFF_SNK);

	/* Stop drawing power */
	sink_stop_drawing_current(port);
}

/**
 * UnorientedDebugAccessory.SRC
 *
 * Super State Entry Actions:
 *  Place Rp on CC
 *  Set power role to SOURCE
 */
static void tc_unoriented_dbg_acc_src_entry(const int port)
{
	enum tcpc_cc_voltage_status cc1, cc2;

	print_current_state(port);

	/* Run function relies on timeout being 0 or meaningful */
	tc[port].timeout = 0;

	/* Clear Low Power Mode Request */
	TC_CLR_FLAG(port, TC_FLAGS_LPM_REQUESTED);

	if (TC_CHK_FLAG(port, TC_FLAGS_PR_SWAP_IN_PROGRESS)) {
		/* Enable VBUS */
		pd_set_power_supply_ready(port);

		/*
		 * Maintain VCONN supply state, whether ON or OFF, and its
		 * data role / usb mux connections.
		 */
	} else {
		/* Get connector orientation */
		tcpm_get_cc(port, &cc1, &cc2);
		tc[port].polarity = (cc1 != TYPEC_CC_VOLT_RD);
		pd_set_polarity(port, tc[port].polarity);

		/*
		 * Initial data role for sink is DFP
		 * This also sets the usb mux
		 */
		tc_set_data_role(port, PD_ROLE_DFP);

		/* Enable VBUS */
		if (pd_set_power_supply_ready(port)) {
			if (IS_ENABLED(CONFIG_USBC_SS_MUX))
				usb_mux_set(port, USB_PD_MUX_NONE,
				USB_SWITCH_DISCONNECT, tc[port].polarity);
		}

#ifdef CONFIG_USB_PE_SM
		tc_enable_pd(port, 0);
		tc[port].timeout = get_time().val +
					PD_POWER_SUPPLY_TURN_ON_DELAY;
#endif
	}

	/* Inform PPC that a sink is connected. */
	if (IS_ENABLED(CONFIG_USBC_PPC))
		ppc_sink_is_connected(port, 1);
}

static void tc_unoriented_dbg_acc_src_run(const int port)
{
	enum tcpc_cc_voltage_status cc1, cc2;
	enum pd_cc_states new_cc_state;

#ifdef CONFIG_USB_PE_SM
	/*
	 * Enable PD communications after power supply has fully
	 * turned on
	 */
	if (tc[port].timeout > 0 && get_time().val > tc[port].timeout) {
		tc_enable_pd(port, 1);
		tc[port].timeout = 0;
	}

	if (!tc_get_pd_enabled(port))
		return;

	/*
	 * Handle Hard Reset from Policy Engine
	 */
	if (TC_CHK_FLAG(port, TC_FLAGS_HARD_RESET)) {
		/* Ignoring Hard Resets while the power supply is resetting.*/
		if (get_time().val < tc[port].timeout)
			return;

		if (tc_perform_src_hard_reset(port))
			TC_CLR_FLAG(port, TC_FLAGS_HARD_RESET);

		return;
	}
#endif

	/* Check for connection */
	tcpm_get_cc(port, &cc1, &cc2);

	if (tc[port].polarity)
		cc1 = cc2;

	if (cc1 == TYPEC_CC_VOLT_OPEN)
		new_cc_state = PD_CC_NONE;
	else
		new_cc_state = PD_CC_UFP_ATTACHED;

	/* Debounce the cc state */
	if (new_cc_state != tc[port].cc_state) {
		tc[port].cc_state = new_cc_state;
		tc[port].cc_debounce = get_time().val + PD_T_SRC_DISCONNECT;
	}

	if (get_time().val < tc[port].cc_debounce)
		return;

	if (tc[port].cc_state == PD_CC_NONE &&
			!TC_CHK_FLAG(port, TC_FLAGS_PR_SWAP_IN_PROGRESS) &&
			!TC_CHK_FLAG(port, TC_FLAGS_DISC_IDENT_IN_PROGRESS)) {

		set_state_tc(port, TC_UNATTACHED_SNK);
	}

#ifdef CONFIG_USB_PE_SM
	/*
	 * PD swap commands
	 */
	if (tc_get_pd_enabled(port)) {
		/*
		 * Power Role Swap Request
		 */
		if (TC_CHK_FLAG(port, TC_FLAGS_DO_PR_SWAP)) {
			/* Clear TC_FLAGS_DO_PR_SWAP on exit */
			return set_state_tc(port, TC_DBG_ACC_SNK);
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
	}
#endif
}

static void tc_unoriented_dbg_acc_src_exit(const int port)
{
	/*
	 * A port shall cease to supply VBUS within tVBUSOFF of exiting
	 * UnorientedDbg.SRC.
	 */
	tc_src_power_off(port);

	/* Clear PR swap flag */
	TC_CLR_FLAG(port, TC_FLAGS_DO_PR_SWAP);
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
	enum tcpc_cc_voltage_status cc1, cc2;

	print_current_state(port);

	if (TC_CHK_FLAG(port, TC_FLAGS_PR_SWAP_IN_PROGRESS)) {
		/*
		 * Both CC1 and CC2 pins shall be independently terminated to
		 * ground through Rd.
		 */
		tcpm_set_cc(port, TYPEC_CC_RD);

		/* Change role to sink */
		tc_set_power_role(port, PD_ROLE_SINK);
		tcpm_set_msg_header(port, tc[port].power_role,
						tc[port].data_role);

		/*
		 * Maintain VCONN supply state, whether ON or OFF, and its
		 * data role / usb mux connections.
		 */
	} else {
		/* Get connector orientation */
		tcpm_get_cc(port, &cc1, &cc2);
		tc[port].polarity = get_snk_polarity(cc1, cc2);
		pd_set_polarity(port, tc[port].polarity);

		/*
		 * Initial data role for sink is UFP
		 * This also sets the usb mux
		 */
		tc_set_data_role(port, PD_ROLE_UFP);

		if (IS_ENABLED(CONFIG_CHARGE_MANAGER)) {
			tc[port].typec_curr =
			usb_get_typec_current_limit(tc[port].polarity,
								cc1, cc2);
			typec_set_input_current_limit(port,
					tc[port].typec_curr, TYPE_C_VOLTAGE);
			charge_manager_update_dualrole(port,
				pd_is_port_partner_dualrole(port) ?
				CAP_DUALROLE : CAP_DEDICATED);
		}
	}
	/* Apply Rd */
	tcpm_set_cc(port, TYPEC_CC_RD);

	/* Enable PD */
	tc_enable_pd(port, 1);
}

static void tc_dbg_acc_snk_run(const int port)
{

	if (!IS_ENABLED(CONFIG_USB_PE_SM)) {
		/* Detach detection */
		if (!pd_is_vbus_present(port)) {
			set_state_tc(port, TC_UNATTACHED_SNK);
			return;
		}

		/* Run Sink Power Sub-State */
		sink_power_sub_states(port);

		return;
	}

	/*
	 * Perform Hard Reset
	 */
	if (TC_CHK_FLAG(port, TC_FLAGS_HARD_RESET)) {
		TC_CLR_FLAG(port, TC_FLAGS_HARD_RESET);
		tc_perform_snk_hard_reset(port);
	}

	/*
	 * The sink will be powered off during a power role swap but we
	 * don't want to trigger a disconnect
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

	/* PD swap commands */

	/*
	 * Power Role Swap
	 */
	if (TC_CHK_FLAG(port, TC_FLAGS_DO_PR_SWAP)) {
		/* Clear PR_SWAP flag in exit */
		set_state_tc(port, TC_UNORIENTED_DBG_ACC_SRC);
		return;
	}

	/*
	 * Data Role Swap
	 */
	if (TC_CHK_FLAG(port, TC_FLAGS_REQUEST_DR_SWAP)) {
		TC_CLR_FLAG(port, TC_FLAGS_REQUEST_DR_SWAP);

		/* Perform Data Role Swap */
		tc_set_data_role(port, tc[port].data_role == PD_ROLE_UFP ?
				PD_ROLE_DFP : PD_ROLE_UFP);
	}
}

static void tc_dbg_acc_snk_exit(const int port)
{
	TC_CLR_FLAG(port, TC_FLAGS_DO_PR_SWAP | TC_FLAGS_POWER_OFF_SNK);

	/* Stop drawing power */
	sink_stop_drawing_current(port);
}

/**
 * Unattached.SRC
 */
static void tc_unattached_src_entry(const int port)
{
	if (get_last_state_tc(port) != TC_UNATTACHED_SNK) {
		/* Detect USB PD cc disconnect */
		hook_notify(HOOK_USB_PD_DISCONNECT);
		print_current_state(port);
	}

	tc[port].data_role = PD_ROLE_DISCONNECTED;

	/*
	 * When data role set events are used to enable BC1.2, then CC
	 * detach events are used to notify BC1.2 that it can be powered
	 * down.
	 */
	if (IS_ENABLED(CONFIG_BC12_DETECT_DATA_ROLE_TRIGGER))
		bc12_role_change_handler(port);

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
		CLR_ALL_BUT_LPM_FLAGS(port);
		tc_enable_pd(port, 0);
	}

	tc[port].next_role_swap = get_time().val + PD_T_DRP_SRC;
}

static void tc_unattached_src_run(const int port)
{
	enum tcpc_cc_voltage_status cc1, cc2;

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
		set_state_tc(port, TC_ATTACH_WAIT_SRC);
	else if (get_time().val > tc[port].next_role_swap &&
			drp_state[port] != PD_DRP_FORCE_SOURCE &&
			drp_state[port] != PD_DRP_FREEZE)
		set_state_tc(port, TC_UNATTACHED_SNK);
#ifdef CONFIG_USB_PD_DUAL_ROLE_AUTO_TOGGLE
	/*
	 * Attempt TCPC auto DRP toggle
	 */
	else if (drp_state[port] == PD_DRP_TOGGLE_ON &&
		TC_CHK_FLAG(port, TC_FLAGS_AUTO_TOGGLE_SUPPORTED) &&
		cc_is_open(cc1, cc2)) {

		/*
		 * We are disconnected and going to DRP
		 *     PC.AutoDischargeDisconnect=0b
		 *     Set RC.DRP=1b (DRP)
		 *     Set RC.RpValue=00b (smallest Rp to save power)
		 *     Set RC.CC1=01b (Rp)
		 *     Set RC.CC2=01b (Rp)
		 */
		tcpm_enable_auto_discharge_disconnect(port, 0);
		tcpm_set_connection(port, TYPEC_CC_RP, 0);

		set_state_tc(port, TC_DRP_AUTO_TOGGLE);
	}
#endif

#ifdef CONFIG_USB_PD_TCPC_LOW_POWER
	else if (drp_state[port] == PD_DRP_FORCE_SOURCE ||
		drp_state[port] == PD_DRP_TOGGLE_OFF) {

		/*
		 * We are disconnecting without DRP.
		 *     PC.AutoDischargeDisconnect=0b
		 */
		tcpm_enable_auto_discharge_disconnect(port, 0);

		set_state_tc(port, TC_LOW_POWER_MODE);
	}
#endif
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

	tc[port].cc_state = PD_CC_UNSET;
}

static void tc_attach_wait_src_run(const int port)
{
	enum tcpc_cc_voltage_status cc1, cc2;
	enum pd_cc_states new_cc_state;

	/* Check for connection */
	tcpm_get_cc(port, &cc1, &cc2);

	/* Debug accessory */
	if (cc_is_snk_dbg_acc(cc1, cc2)) {
		/* Debug accessory */
		new_cc_state = PD_CC_UFP_DEBUG_ACC;
	} else if (cc_is_at_least_one_rd(cc1, cc2)) {
		/* UFP attached */
		new_cc_state = PD_CC_UFP_ATTACHED;
	} else if (cc_is_audio_acc(cc1, cc2)) {
		/* AUDIO Accessory not supported. Just ignore */
		new_cc_state = PD_CC_UFP_AUDIO_ACC;
	} else {
		/* No UFP */
		set_state_tc(port, TC_UNATTACHED_SNK);
		return;
	}

	/* Debounce the cc state */
	if (new_cc_state != tc[port].cc_state) {
		tc[port].cc_debounce = get_time().val + PD_T_CC_DEBOUNCE;
		tc[port].cc_state = new_cc_state;
		return;
	}

	/* Wait for CC debounce */
	if (get_time().val < tc[port].cc_debounce)
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
		if (new_cc_state == PD_CC_UFP_ATTACHED) {
			set_state_tc(port, TC_ATTACHED_SRC);
			return;
		} else if (new_cc_state == PD_CC_UFP_DEBUG_ACC) {
			set_state_tc(port, TC_UNORIENTED_DBG_ACC_SRC);
			return;
		}
	}
}

/**
 * Attached.SRC
 */
static void tc_attached_src_entry(const int port)
{
	enum tcpc_cc_voltage_status cc1, cc2;

	print_current_state(port);

	/* Run function relies on timeout being 0 or meaningful */
	tc[port].timeout = 0;

	/* Clear Low Power Mode Request */
	TC_CLR_FLAG(port, TC_FLAGS_LPM_REQUESTED);

#if defined(CONFIG_USB_PE_SM)
	if (TC_CHK_FLAG(port, TC_FLAGS_PR_SWAP_IN_PROGRESS)) {
		/* Change role to source */
		tc_set_power_role(port, PD_ROLE_SOURCE);
		tcpm_set_msg_header(port,
				tc[port].power_role, tc[port].data_role);
		/*
		 * Both CC1 and CC2 pins shall be independently terminated to
		 * pulled up through Rp.
		 */
		tcpm_select_rp_value(port, CONFIG_USB_PD_PULLUP);

		/* Enable VBUS */
		pd_set_power_supply_ready(port);

		/*
		 * Maintain VCONN supply state, whether ON or OFF, and its
		 * data role / usb mux connections.
		 */
	} else {
		/* Get connector orientation */
		tcpm_get_cc(port, &cc1, &cc2);
		tc[port].polarity = get_src_polarity(cc1, cc2);
		pd_set_polarity(port, tc[port].polarity);

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
				usb_mux_set(port, USB_PD_MUX_NONE,
				USB_SWITCH_DISCONNECT, tc[port].polarity);
		}

		tc_enable_pd(port, 0);
		tc[port].timeout = get_time().val +
			MAX(PD_POWER_SUPPLY_TURN_ON_DELAY, PD_T_VCONN_STABLE);
	}
#else
	/* Get connector orientation */
	tcpm_get_cc(port, &cc1, &cc2);
	tc[port].polarity = get_src_polarity(cc1, cc2);
	pd_set_polarity(port, tc[port].polarity);

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
			usb_mux_set(port, USB_PD_MUX_NONE,
			USB_SWITCH_DISCONNECT, tc[port].polarity);
	}
#endif /* CONFIG_USB_PE_SM */

	/* Inform PPC that a sink is connected. */
	if (IS_ENABLED(CONFIG_USBC_PPC))
		ppc_sink_is_connected(port, 1);

	/*
	 * Only notify if we're not performing a power role swap.  During a
	 * power role swap, the port partner is not disconnecting/connecting.
	 */
	if (!TC_CHK_FLAG(port, TC_FLAGS_PR_SWAP_IN_PROGRESS))
		hook_notify(HOOK_USB_PD_CONNECT);
}

static void tc_attached_src_run(const int port)
{
	enum tcpc_cc_voltage_status cc1, cc2;
	enum pd_cc_states new_cc_state;

#ifdef CONFIG_USB_PE_SM
	/*
	 * Enable PD communications after power supply has fully
	 * turned on
	 */
	if (tc[port].timeout > 0 && get_time().val > tc[port].timeout) {
		tc_enable_pd(port, 1);
		tc[port].timeout = 0;
	}

	if (!tc_get_pd_enabled(port))
		return;

	/*
	 * Handle Hard Reset from Policy Engine
	 */
	if (TC_CHK_FLAG(port, TC_FLAGS_HARD_RESET)) {
		/* Ignoring Hard Resets while the power supply is resetting.*/
		if (get_time().val < tc[port].timeout)
			return;

		if (tc_perform_src_hard_reset(port))
			TC_CLR_FLAG(port, TC_FLAGS_HARD_RESET);

		return;
	}
#endif

	/* Check for connection */
	tcpm_get_cc(port, &cc1, &cc2);

	if (tc[port].polarity)
		cc1 = cc2;

	if (cc1 == TYPEC_CC_VOLT_OPEN)
		new_cc_state = PD_CC_NONE;
	else
		new_cc_state = PD_CC_UFP_ATTACHED;

	/* Debounce the cc state */
	if (new_cc_state != tc[port].cc_state) {
		tc[port].cc_state = new_cc_state;
		tc[port].cc_debounce = get_time().val + PD_T_SRC_DISCONNECT;
	}

	if (get_time().val < tc[port].cc_debounce)
		return;

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

		set_state_tc(port, IS_ENABLED(CONFIG_USB_PD_TRY_SRC) ?
			TC_TRY_WAIT_SNK : TC_UNATTACHED_SNK);
	}

#ifdef CONFIG_USB_PE_SM
	/*
	 * PD swap commands
	 */
	if (tc_get_pd_enabled(port) && prl_is_running(port)) {
		/*
		 * Power Role Swap Request
		 */
		if (TC_CHK_FLAG(port, TC_FLAGS_DO_PR_SWAP)) {
			/* Clear TC_FLAGS_DO_PR_SWAP on exit */
			return set_state_tc(port, TC_ATTACHED_SNK);
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

	if (!TC_CHK_FLAG(port, TC_FLAGS_DO_PR_SWAP)) {
		/* Disable VCONN if not power role swapping */
		if (TC_CHK_FLAG(port, TC_FLAGS_VCONN_ON))
			set_vconn(port, 0);
	}

	/* Clear PR swap flag after checking for Vconn */
	TC_CLR_FLAG(port, TC_FLAGS_DO_PR_SWAP);
}

#ifdef CONFIG_USB_PD_DUAL_ROLE_AUTO_TOGGLE
/**
 * DrpAutoToggle
 */
static void tc_drp_auto_toggle_entry(const int port)
{
	print_current_state(port);

	/*
	 * The PD_EXIT_LOW_POWER_EVENT_MASK flag may have been set
	 * due to a CC event. Clear it now since we haven't engaged
	 * low power mode.
	 */
	atomic_clear(task_get_event_bitmap(task_get_current()),
		PD_EXIT_LOW_POWER_EVENT_MASK);

	if (drp_state[port] == PD_DRP_TOGGLE_ON)
		tcpm_enable_drp_toggle(port);
}

static void tc_drp_auto_toggle_run(const int port)
{
	enum pd_drp_next_states next_state;
	enum tcpc_cc_voltage_status cc1, cc2;

	/*
	 * If SW decided we should be in a low power state and
	 * the CC lines did not change, then don't talk with the
	 * TCPC otherwise we might wake it up.
	 */
#ifdef CONFIG_USB_PD_TCPC_LOW_POWER
	if (TC_CHK_FLAG(port, TC_FLAGS_LPM_REQUESTED) &&
	    !TC_CHK_FLAG(port, TC_FLAGS_WAKE_FROM_LPM)) {
		if (get_time().val > tc[port].low_power_time)
			set_state_tc(port, TC_LOW_POWER_MODE);
		return;
	}
#endif

	/* Check for connection */
	tcpm_get_cc(port, &cc1, &cc2);

	tc[port].drp_sink_time = get_time().val;
	next_state = drp_auto_toggle_next_state(&tc[port].drp_sink_time,
		tc[port].power_role, drp_state[port], cc1, cc2);

	/*
	 * The next state is not determined just by what is
	 * attached, but also depends on DRP_STATE. Regardless
	 * of next state, if nothing is attached, then always
	 * request low power mode.
	 */
	if (IS_ENABLED(CONFIG_USB_PD_TCPC_LOW_POWER)) {
		if (cc1 == TYPEC_CC_VOLT_OPEN && cc2 == TYPEC_CC_VOLT_OPEN &&
					!tc[port].tasks_preventing_lpm) {
			TC_SET_FLAG(port, TC_FLAGS_LPM_REQUESTED);
			TC_CLR_FLAG(port, TC_FLAGS_WAKE_FROM_LPM);
		}
	}

	switch (next_state) {
	case DRP_TC_DEFAULT:
		set_state_tc(port, PD_DEFAULT_STATE(port));
		break;
	case DRP_TC_UNATTACHED_SNK:
		/*
		 * New SNK connection.
		 *     Set RC.CC1 & RC.CC2 per decision
		 *     Set RC.DRP=0
		 *     Set TCPC_CONTROl.PlugOrientation
		 *     PC.AutoDischargeDisconnect=1b
		 */
		tcpm_set_connection(port, TYPEC_CC_RD, 1);
		tcpm_enable_auto_discharge_disconnect(port, 1);

		set_state_tc(port, TC_UNATTACHED_SNK);
		break;
	case DRP_TC_UNATTACHED_SRC:
		/*
		 * New SRC connection.
		 *     Set RC.CC1 & RC.CC2 per decision
		 *     Set RC.DRP=0
		 *     Set TCPC_CONTROl.PlugOrientation
		 *     PC.AutoDischargeDisconnect=1b
		 */
		tcpm_set_connection(port, TYPEC_CC_RP, 1);
		tcpm_enable_auto_discharge_disconnect(port, 1);

		set_state_tc(port, TC_UNATTACHED_SRC);
		break;
	case DRP_TC_DRP_AUTO_TOGGLE:
		/*
		 * We are staying in PD_STATE_DRP_AUTO_TOGGLE
		 */
		break;
	}
}
#endif /* CONFIG_USB_PD_DUAL_ROLE_AUTO_TOGGLE */

#ifdef CONFIG_USB_PD_TCPC_LOW_POWER
static void tc_low_power_mode_entry(const int port)
{
	print_current_state(port);
	CPRINTS("TCPC p%d Enter Low Power Mode", port);
	tcpm_enter_low_power_mode(port);
	TC_SET_FLAG(port, TC_FLAGS_LPM_ENGAGED);
}

static void tc_low_power_mode_run(const int port)
{
#ifdef CONFIG_USB_PD_DUAL_ROLE_AUTO_TOGGLE
	if (TC_CHK_FLAG(port, TC_FLAGS_WAKE_FROM_LPM)) {
		set_state_tc(port, TC_DRP_AUTO_TOGGLE);
		return;
	}
#endif
	tc_pause_event_loop(port);
}

static void tc_low_power_mode_exit(const int port)
{
	CPRINTS("TCPC p%d Exit Low Power Mode", port);
	TC_CLR_FLAG(port, TC_FLAGS_LPM_REQUESTED | TC_FLAGS_LPM_ENGAGED |
		TC_FLAGS_WAKE_FROM_LPM);
	reset_device_and_notify(port);
	tc_start_event_loop(port);
}
#endif


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

	tc[port].cc_state = PD_CC_UNSET;
	tc[port].try_wait_debounce = get_time().val + PD_T_DRP_TRY;
	tc[port].timeout = get_time().val + PD_T_TRY_TIMEOUT;
}

static void tc_try_src_run(const int port)
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
			set_state_tc(port, TC_ATTACHED_SRC);
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
			set_state_tc(port, TC_TRY_WAIT_SNK);
		}
	}
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

	tc_enable_pd(port, 0);
	tc[port].cc_state = PD_CC_UNSET;
	tc[port].try_wait_debounce = get_time().val + PD_T_CC_DEBOUNCE;
}

static void tc_try_wait_snk_run(const int port)
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
		set_state_tc(port, TC_UNATTACHED_SNK);
		return;
	}

	/*
	 * The port shall transition to Attached.SNK after tCCDebounce if or
	 * when VBUS is detected.
	 */
	if (get_time().val > tc[port].try_wait_debounce &&
						pd_is_vbus_present(port))
		set_state_tc(port, TC_ATTACHED_SNK);
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
	 * ground through Rd.
	 */
	tcpm_select_rp_value(port, CONFIG_USB_PD_PULLUP);
	tcpm_set_cc(port, TYPEC_CC_RD);
	tc[port].cc_state = PD_CC_UNSET;

	/* Set power role to sink */
	tc_set_power_role(port, PD_ROLE_SINK);
	tcpm_set_msg_header(port, tc[port].power_role, tc[port].data_role);

	/*
	 * The policy engine is in the disabled state. Disable PD and
	 * re-enable it
	 */
	tc_enable_pd(port, 0);

	tc[port].timeout = get_time().val + PD_POWER_SUPPLY_TURN_ON_DELAY;
}

static void tc_ct_unattached_snk_run(int port)
{
	enum tcpc_cc_voltage_status cc1;
	enum tcpc_cc_voltage_status cc2;
	enum pd_cc_states new_cc_state;

	if (tc[port].timeout > 0 && get_time().val > tc[port].timeout) {
		tc_enable_pd(port, 1);
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
		tc[port].cc_debounce = get_time().val + PD_T_VPDDETACH;
	}

	/*
	 * The port shall transition to Unattached.SNK if the state of
	 * the CC pin is SNK.Open for tVPDDetach after VBUS is vSafe0V.
	 */
	if (get_time().val > tc[port].cc_debounce) {
		if (new_cc_state == PD_CC_NONE && !pd_is_vbus_present(port)) {
			if (IS_ENABLED(CONFIG_USB_PD_ALT_MODE_DFP))
				pd_dfp_exit_mode(port, 0, 0);

			set_state_tc(port, TC_UNATTACHED_SNK);
			return;
		}
	}

	/*
	 * The port shall transition to CTAttached.SNK when VBUS is detected.
	 */
	if (pd_is_vbus_present(port))
		set_state_tc(port, TC_CT_ATTACHED_SNK);
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
 * Super State CC_RD
 */
static void tc_cc_rd_entry(const int port)
{
	/* Disable VCONN */
	if (IS_ENABLED(CONFIG_USBC_VCONN))
		set_vconn(port, 0);

	/* Disable VBUS */
	pd_power_supply_reset(port);

	/* Set power role to sink */
	tc_set_power_role(port, PD_ROLE_SINK);
	tcpm_set_msg_header(port, tc[port].power_role, tc[port].data_role);

	/*
	 * Both CC1 and CC2 pins shall be independently terminated to
	 * ground through Rd.
	 */
	tcpm_set_cc(port, TYPEC_CC_RD);
}


/**
 * Super State CC_RP
 */
static void tc_cc_rp_entry(const int port)
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
}

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

void tc_run(const int port)
{
	run_state(port, &tc[port].ctx);
}

static void pd_chipset_resume(void)
{
	int i;

	for (i = 0; i < CONFIG_USB_PD_PORT_MAX_COUNT; i++) {
		pd_set_dual_role(i, PD_DRP_TOGGLE_ON);
		task_set_event(PD_PORT_TO_TASK_ID(i),
				PD_EVENT_POWER_STATE_CHANGE, 0);
	}

	CPRINTS("PD:S3->S0");
}
DECLARE_HOOK(HOOK_CHIPSET_RESUME, pd_chipset_resume, HOOK_PRIO_DEFAULT);

static void pd_chipset_suspend(void)
{
	int i;

	for (i = 0; i < CONFIG_USB_PD_PORT_MAX_COUNT; i++) {
		pd_set_dual_role(i, PD_DRP_TOGGLE_OFF);
		task_set_event(PD_PORT_TO_TASK_ID(i),
			PD_EVENT_POWER_STATE_CHANGE, 0);
	}

	CPRINTS("PD:S0->S3");
}
DECLARE_HOOK(HOOK_CHIPSET_SUSPEND, pd_chipset_suspend, HOOK_PRIO_DEFAULT);

static void pd_chipset_startup(void)
{
	int i;

	for (i = 0; i < CONFIG_USB_PD_PORT_MAX_COUNT; i++) {
		pd_set_dual_role_no_wakeup(i, PD_DRP_TOGGLE_OFF);
		task_set_event(PD_PORT_TO_TASK_ID(i),
				PD_EVENT_POWER_STATE_CHANGE |
				PD_EVENT_UPDATE_DUAL_ROLE,
				0);
	}

	CPRINTS("PD:S5->S3");
}
DECLARE_HOOK(HOOK_CHIPSET_STARTUP, pd_chipset_startup, HOOK_PRIO_DEFAULT);

static void pd_chipset_shutdown(void)
{
	int i;

	for (i = 0; i < CONFIG_USB_PD_PORT_MAX_COUNT; i++) {
		pd_set_dual_role_no_wakeup(i, PD_DRP_FORCE_SINK);
		task_set_event(PD_PORT_TO_TASK_ID(i),
				PD_EVENT_POWER_STATE_CHANGE |
				PD_EVENT_UPDATE_DUAL_ROLE,
				0);
	}

	CPRINTS("PD:S3->S5");
}
DECLARE_HOOK(HOOK_CHIPSET_SHUTDOWN, pd_chipset_shutdown, HOOK_PRIO_DEFAULT);


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
 * TC_ATTACHED_SNK    TC_ATTACHED_SRC    TC_DRP_AUTO_TOGGLE    TC_LOW_POWER_MODE
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
		.parent = &tc_states[TC_CC_RD],
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
	},
	[TC_UNORIENTED_DBG_ACC_SRC] = {
		.entry	= tc_unoriented_dbg_acc_src_entry,
		.run	= tc_unoriented_dbg_acc_src_run,
		.exit   = tc_unoriented_dbg_acc_src_exit,
		.parent = &tc_states[TC_CC_RP],
	},
	[TC_DBG_ACC_SNK] = {
		.entry	= tc_dbg_acc_snk_entry,
		.run	= tc_dbg_acc_snk_run,
		.exit   = tc_dbg_acc_snk_exit,
		.parent = &tc_states[TC_CC_RD],
	},
	[TC_UNATTACHED_SRC] = {
		.entry	= tc_unattached_src_entry,
		.run	= tc_unattached_src_run,
		.parent = &tc_states[TC_CC_RP],
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
#ifdef CONFIG_USB_PD_DUAL_ROLE_AUTO_TOGGLE
	[TC_DRP_AUTO_TOGGLE] = {
		.entry = tc_drp_auto_toggle_entry,
		.run   = tc_drp_auto_toggle_run,
	},
#endif /* CONFIG_USB_PD_DUAL_ROLE_AUTO_TOGGLE */
#ifdef CONFIG_USB_PD_TCPC_LOW_POWER
	[TC_LOW_POWER_MODE] = {
		.entry = tc_low_power_mode_entry,
		.run   = tc_low_power_mode_run,
		.exit  = tc_low_power_mode_exit,
	},
#endif /* CONFIG_USB_PD_TCPC_LOW_POWER */
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
BUILD_ASSERT(ARRAY_SIZE(tc_states) == ARRAY_SIZE(tc_state_names));
const int test_tc_sm_data_size = ARRAY_SIZE(test_tc_sm_data);
#endif
