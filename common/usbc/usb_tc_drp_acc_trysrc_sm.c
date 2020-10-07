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
#include "usb_pd_dpm.h"
#include "usb_pe_sm.h"
#include "usb_prl_sm.h"
#include "usb_sm.h"
#include "usb_tc_sm.h"
#include "usbc_ppc.h"
#include "vboot.h"

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

#define CPRINTF_LX(x, format, args...) \
	do { \
		if (tc_debug_level >= x) \
			CPRINTF(format, ## args); \
	} while (0)
#define CPRINTF_L1(format, args...) CPRINTF_LX(1, format, ## args)
#define CPRINTF_L2(format, args...) CPRINTF_LX(2, format, ## args)
#define CPRINTF_L3(format, args...) CPRINTF_LX(3, format, ## args)

#define CPRINTS_LX(x, format, args...) \
	do { \
		if (tc_debug_level >= x) \
			CPRINTS(format, ## args); \
	} while (0)
#define CPRINTS_L1(format, args...) CPRINTS_LX(1, format, ## args)
#define CPRINTS_L2(format, args...) CPRINTS_LX(2, format, ## args)
#define CPRINTS_L3(format, args...) CPRINTS_LX(3, format, ## args)

/*
 * Define DEBUG_PRINT_FLAG_AND_EVENT_NAMES to print flag names when set and
 * cleared, and event names when handled by tc_event_check().
 */
#undef DEBUG_PRINT_FLAG_AND_EVENT_NAMES

#ifdef DEBUG_PRINT_FLAG_AND_EVENT_NAMES
void print_flag(int set_or_clear, int flag);
#define TC_SET_FLAG(port, flag)                                \
	do {                                                   \
		print_flag(1, flag);                           \
		deprecated_atomic_or(&tc[port].flags, (flag)); \
	} while (0)
#define TC_CLR_FLAG(port, flag)                                        \
	do {                                                           \
		print_flag(0, flag);                                   \
		deprecated_atomic_clear_bits(&tc[port].flags, (flag)); \
	} while (0)
#else
#define TC_SET_FLAG(port, flag) deprecated_atomic_or(&tc[port].flags, (flag))
#define TC_CLR_FLAG(port, flag) \
	deprecated_atomic_clear_bits(&tc[port].flags, (flag))
#endif
#define TC_CHK_FLAG(port, flag) (tc[port].flags & (flag))

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
/* Flag to note CVTPD has been detected */
#define TC_FLAGS_CTVPD_DETECTED         BIT(5)
/* Flag to note request to swap to VCONN on */
#define TC_FLAGS_REQUEST_VC_SWAP_ON     BIT(6)
/* Flag to note request to swap to VCONN off */
#define TC_FLAGS_REQUEST_VC_SWAP_OFF    BIT(7)
/* Flag to note request to swap VCONN is being rejected */
#define TC_FLAGS_REJECT_VCONN_SWAP      BIT(8)
/* Flag to note request to power role swap */
#define TC_FLAGS_REQUEST_PR_SWAP        BIT(9)
/* Flag to note request to data role swap */
#define TC_FLAGS_REQUEST_DR_SWAP        BIT(10)
/* Flag to note request to power off sink */
#define TC_FLAGS_POWER_OFF_SNK          BIT(11)
/* Flag to note port partner has unconstrained power */
#define TC_FLAGS_PARTNER_UNCONSTRAINED  BIT(12)
/* Flag to note port partner is Dual Role Data */
#define TC_FLAGS_PARTNER_DR_DATA        BIT(13)
/* Flag to note port partner is Dual Role Power */
#define TC_FLAGS_PARTNER_DR_POWER       BIT(14)
/* Flag to note port partner is Power Delivery capable */
#define TC_FLAGS_PARTNER_PD_CAPABLE     BIT(15)
/* Flag to note hard reset has been requested */
#define TC_FLAGS_HARD_RESET_REQUESTED   BIT(16)
/* Flag to note port partner is USB comms capable */
#define TC_FLAGS_PARTNER_USB_COMM       BIT(17)
/* Flag to note we are currently performing PR Swap */
#define TC_FLAGS_PR_SWAP_IN_PROGRESS    BIT(18)
/* Flag to note we are performing Discover Identity */
#define TC_FLAGS_DISC_IDENT_IN_PROGRESS BIT(19)
/* Flag to note we should check for connection */
#define TC_FLAGS_CHECK_CONNECTION       BIT(20)
/* Flag to note pd_set_suspend SUSPEND state */
#define TC_FLAGS_SUSPEND                BIT(21)

/*
 * Clear all flags except TC_FLAGS_LPM_ENGAGED and TC_FLAGS_SUSPEND.
 */
#define CLR_ALL_BUT_LPM_FLAGS(port) TC_CLR_FLAG(port, \
	~(TC_FLAGS_LPM_ENGAGED | TC_FLAGS_SUSPEND))

/* 100 ms is enough time for any TCPC transaction to complete. */
#define PD_LPM_DEBOUNCE_US (100 * MSEC)

/*
 * This delay is not part of the USB Type-C specification or the USB port
 * controller specification. Some TCPCs require extra time before the CC_STATUS
 * register is updated when exiting low power mode.
 *
 * This delay can be possibly shortened or removed by checking VBUS state
 * before trying to re-enter LPM.
 *
 * TODO(b/162347811): TCPMv2: Wait for debounce on Vbus and CC lines
 */
#define PD_LPM_EXIT_DEBOUNCE_US CONFIG_USB_PD_TCPC_LPM_EXIT_DEBOUNCE

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

/* Unreachable time in future */
#define TIMER_DISABLED 0xffffffffffffffff

enum ps_reset_sequence {
	PS_STATE0,
	PS_STATE1,
	PS_STATE2,
};

/* List of all TypeC-level states */
enum usb_tc_state {
	/* Super States */
	TC_CC_OPEN,
	TC_CC_RD,
	TC_CC_RP,
	/* Normal States */
	TC_DISABLED,
	TC_ERROR_RECOVERY,
	TC_UNATTACHED_SNK,
	TC_ATTACH_WAIT_SNK,
	TC_ATTACHED_SNK,
	TC_UNATTACHED_SRC,
	TC_ATTACH_WAIT_SRC,
	TC_ATTACHED_SRC,
	TC_TRY_SRC,
	TC_TRY_WAIT_SNK,
	TC_DRP_AUTO_TOGGLE,
	TC_LOW_POWER_MODE,
	TC_CT_UNATTACHED_SNK,
	TC_CT_ATTACHED_SNK,
};
/* Forward declare the full list of states. This is indexed by usb_tc_state */
static const struct usb_state tc_states[];

/*
 * Remove all of the states that aren't support at link time. This allows
 * IS_ENABLED to work.
 */
#ifndef CONFIG_USB_PD_DUAL_ROLE_AUTO_TOGGLE
GEN_NOT_SUPPORTED(TC_DRP_AUTO_TOGGLE);
#define TC_DRP_AUTO_TOGGLE TC_DRP_AUTO_TOGGLE_NOT_SUPPORTED
#endif /* CONFIG_USB_PD_DUAL_ROLE_AUTO_TOGGLE */

#ifndef CONFIG_USB_PD_TCPC_LOW_POWER
GEN_NOT_SUPPORTED(TC_LOW_POWER_MODE);
#define TC_LOW_POWER_MODE TC_LOW_POWER_MODE_NOT_SUPPORTED
#endif /* CONFIG_USB_PD_TCPC_LOW_POWER */

#ifndef CONFIG_USB_PE_SM
GEN_NOT_SUPPORTED(TC_CT_UNATTACHED_SNK);
#define TC_CT_UNATTACHED_SNK TC_CT_UNATTACHED_SNK_NOT_SUPPORTED
GEN_NOT_SUPPORTED(TC_CT_ATTACHED_SNK);
#define TC_CT_ATTACHED_SNK TC_CT_ATTACHED_SNK_NOT_SUPPORTED
#endif /* CONFIG_USB_PE_SM */

/*
 * We will use DEBUG LABELS if we will be able to print (COMMON RUNTIME)
 * and either CONFIG_USB_PD_DEBUG_LEVEL is not defined (no override) or
 * we are overriding and the level is not DISABLED.
 *
 * If we can't print or the CONFIG_USB_PD_DEBUG_LEVEL is defined to be 0
 * then the DEBUG LABELS will be removed from the build.
 */
#if defined(CONFIG_COMMON_RUNTIME) && \
	(!defined(CONFIG_USB_PD_DEBUG_LEVEL) || \
	 (CONFIG_USB_PD_DEBUG_LEVEL > 0))
#define USB_PD_DEBUG_LABELS
#endif

/*
 * Helper Macro to determine if the machine is in state
 * TC_ATTACHED_SRC
 */
#define IS_ATTACHED_SRC(port) (get_state_tc(port) == TC_ATTACHED_SRC)

/*
 * Helper Macro to determine if the machine is in state
 * TC_ATTACHED_SNK
 */
#define IS_ATTACHED_SNK(port) (get_state_tc(port) == TC_ATTACHED_SNK)


#ifdef USB_PD_DEBUG_LABELS
/* List of human readable state names for console debugging */
static const char * const tc_state_names[] = {
	[TC_DISABLED] = "Disabled",
	[TC_ERROR_RECOVERY] = "ErrorRecovery",
	[TC_UNATTACHED_SNK] = "Unattached.SNK",
	[TC_ATTACH_WAIT_SNK] = "AttachWait.SNK",
	[TC_ATTACHED_SNK] = "Attached.SNK",
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
#else
/*
 * Reference so IS_ENABLED section below that references the names
 * will compile and the optimizer will remove it.
 */
extern const char **tc_state_names;
#endif

/* Debug log level - higher number == more log */
#ifdef CONFIG_USB_PD_DEBUG_LEVEL
static const enum debug_level tc_debug_level = CONFIG_USB_PD_DEBUG_LEVEL;
#else
static enum debug_level tc_debug_level = DEBUG_LEVEL_1;
#endif

#ifdef DEBUG_PRINT_FLAG_AND_EVENT_NAMES
struct bit_name {
	int		value;
	const char	*name;
};

static struct bit_name flag_bit_names[] = {
	{ TC_FLAGS_VCONN_ON, "VCONN_ON" },
	{ TC_FLAGS_TS_DTS_PARTNER, "TS_DTS_PARTNER" },
	{ TC_FLAGS_VBUS_NEVER_LOW, "VBUS_NEVER_LOW" },
	{ TC_FLAGS_LPM_TRANSITION, "LPM_TRANSITION" },
	{ TC_FLAGS_LPM_ENGAGED, "LPM_ENGAGED" },
	{ TC_FLAGS_CTVPD_DETECTED, "CTVPD_DETECTED" },
	{ TC_FLAGS_REQUEST_VC_SWAP_ON, "REQUEST_VC_SWAP_ON" },
	{ TC_FLAGS_REQUEST_VC_SWAP_OFF, "REQUEST_VC_SWAP_OFF" },
	{ TC_FLAGS_REJECT_VCONN_SWAP, "REJECT_VCONN_SWAP" },
	{ TC_FLAGS_REQUEST_PR_SWAP, "REQUEST_PR_SWAP" },
	{ TC_FLAGS_REQUEST_DR_SWAP, "REQUEST_DR_SWAP" },
	{ TC_FLAGS_POWER_OFF_SNK, "POWER_OFF_SNK" },
	{ TC_FLAGS_PARTNER_UNCONSTRAINED, "PARTNER_UNCONSTRAINED" },
	{ TC_FLAGS_PARTNER_DR_DATA, "PARTNER_DR_DATA" },
	{ TC_FLAGS_PARTNER_DR_POWER, "PARTNER_DR_POWER" },
	{ TC_FLAGS_PARTNER_PD_CAPABLE, "PARTNER_PD_CAPABLE" },
	{ TC_FLAGS_HARD_RESET_REQUESTED, "HARD_RESET_REQUESTED" },
	{ TC_FLAGS_PARTNER_USB_COMM, "PARTNER_USB_COMM" },
	{ TC_FLAGS_PR_SWAP_IN_PROGRESS, "PR_SWAP_IN_PROGRESS" },
	{ TC_FLAGS_DISC_IDENT_IN_PROGRESS, "DISC_IDENT_IN_PROGRESS" },
	{ TC_FLAGS_CHECK_CONNECTION, "CHECK_CONNECTION" },
	{ TC_FLAGS_SUSPEND, "SUSPEND" },
};

static struct bit_name event_bit_names[] = {
	{ TASK_EVENT_SYSJUMP_READY, "SYSJUMP_READY" },
	{ TASK_EVENT_IPC_READY, "IPC_READY" },
	{ TASK_EVENT_PD_AWAKE, "PD_AWAKE" },
	{ TASK_EVENT_PECI_DONE, "PECI_DONE" },
	{ TASK_EVENT_I2C_IDLE, "I2C_IDLE" },
	{ TASK_EVENT_PS2_DONE, "PS2_DONE" },
	{ TASK_EVENT_DMA_TC, "DMA_TC" },
	{ TASK_EVENT_ADC_DONE, "ADC_DONE" },
	{ TASK_EVENT_RESET_DONE, "RESET_DONE" },
	{ TASK_EVENT_WAKE, "WAKE" },
	{ TASK_EVENT_MUTEX, "MUTEX" },
	{ TASK_EVENT_TIMER, "TIMER" },
	{ PD_EVENT_TX, "TX" },
	{ PD_EVENT_CC, "CC" },
	{ PD_EVENT_TCPC_RESET, "TCPC_RESET" },
	{ PD_EVENT_UPDATE_DUAL_ROLE, "UPDATE_DUAL_ROLE" },
	{ PD_EVENT_DEVICE_ACCESSED, "DEVICE_ACCESSED" },
	{ PD_EVENT_POWER_STATE_CHANGE, "POWER_STATE_CHANGE" },
	{ PD_EVENT_SEND_HARD_RESET, "SEND_HARD_RESET" },
	{ PD_EVENT_SYSJUMP, "SYSJUMP" },
};

static void print_bits(const char *desc, int value,
		       struct bit_name *names, int names_size)
{
	int i;

	CPRINTF("%s 0x%x : ", desc, value);
	for (i = 0; i < names_size; i++) {
		if (value & names[i].value)
			CPRINTF("%s | ", names[i].name);
		value &= ~names[i].value;
	}
	if (value != 0)
		CPRINTF("0x%x", value);
	CPRINTF("\n");
}

void print_flag(int set_or_clear, int flag)
{
	print_bits(set_or_clear ? "Set" : "Clr", flag, flag_bit_names,
		   ARRAY_SIZE(flag_bit_names));
}
#endif /* DEBUG_PRINT_FLAG_AND_EVENT_NAMES */

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
	/* Time a port shall wait before it can determine it is attached */
	uint64_t cc_debounce;
	/*
	 * Time a Sink port shall wait before it can determine it is detached
	 * due to the potential for USB PD signaling on CC as described in
	 * the state definitions.
	 */
	uint64_t pd_debounce;
	/*
	 * Time to ignore Vbus absence due to external IC debounce detection
	 * logic immediately after a power role swap.
	 */
	uint64_t vbus_debounce_time;
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
	/* Time to debounce exit low power mode */
	uint64_t low_power_exit_time;
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

	/* Selected TCPC CC/Rp values */
	enum tcpc_cc_pull select_cc_pull;
	enum tcpc_rp_value select_current_limit_rp;
	enum tcpc_rp_value select_collision_rp;
} tc[CONFIG_USB_PD_PORT_MAX_COUNT];

/* Port dual-role state */
static volatile __maybe_unused
enum pd_dual_role_states drp_state[CONFIG_USB_PD_PORT_MAX_COUNT] = {
	[0 ... (CONFIG_USB_PD_PORT_MAX_COUNT - 1)] =
		CONFIG_USB_PD_INITIAL_DRP_STATE};

static void set_vconn(int port, int enable);

/* Forward declare common, private functions */
static __maybe_unused int reset_device_and_notify(int port);
static __maybe_unused void check_drp_connection(const int port);
static void sink_power_sub_states(int port);

#ifdef CONFIG_POWER_COMMON
static void handle_new_power_state(int port);
#endif /* CONFIG_POWER_COMMON */

static void pd_update_dual_role_config(int port);

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
		if (IS_ATTACHED_SRC(port))
			pd_dpm_request(port, DPM_REQUEST_SRC_CAP_CHANGE);
	}
}

void pd_request_source_voltage(int port, int mv)
{
	if (IS_ENABLED(CONFIG_USB_PE_SM)) {
		pd_set_max_voltage(mv);

		if (IS_ATTACHED_SNK(port))
			pd_dpm_request(port, DPM_REQUEST_NEW_POWER_LEVEL);
		else
			pd_dpm_request(port, DPM_REQUEST_PR_SWAP);

		task_wake(PD_PORT_TO_TASK_ID(port));
	}
}

void pd_set_external_voltage_limit(int port, int mv)
{
	if (IS_ENABLED(CONFIG_USB_PE_SM)) {
		pd_set_max_voltage(mv);

		/* Must be in Attached.SNK when this function is called */
		if (get_state_tc(port) == TC_ATTACHED_SNK)
			pd_dpm_request(port, DPM_REQUEST_NEW_POWER_LEVEL);

		task_wake(PD_PORT_TO_TASK_ID(port));
	}
}

void pd_set_new_power_request(int port)
{
	if (IS_ENABLED(CONFIG_USB_PE_SM)) {
		/* Must be in Attached.SNK when this function is called */
		if (get_state_tc(port) == TC_ATTACHED_SNK)
			pd_dpm_request(port, DPM_REQUEST_NEW_POWER_LEVEL);
	}
}

void tc_request_power_swap(int port)
{
	if (IS_ENABLED(CONFIG_USB_PE_SM)) {
		/*
		 * Must be in Attached.SRC or Attached.SNK
		 */
		if (IS_ATTACHED_SRC(port) || IS_ATTACHED_SNK(port)) {
			TC_SET_FLAG(port, TC_FLAGS_PR_SWAP_IN_PROGRESS);

			/* Let tc_pr_swap_complete start the Vbus debounce */
			tc[port].vbus_debounce_time = TIMER_DISABLED;
		}

		/*
		 * TCPCI Rev2 V1.1 4.4.5.4.4
		 * Disconnect Detection by the Sink TCPC during a Connection
		 *
		 * Upon reception of or prior to transmitting a PR_Swap
		 * message, the TCPM acting as a Sink shall disable the Sink
		 * disconnect detection to retain PD message delivery when
		 * Power Role Swap happens. Disable AutoDischargeDisconnect.
		 */
		if (IS_ATTACHED_SNK(port))
			tcpm_enable_auto_discharge_disconnect(port, 0);
	}
}

static bool pd_comm_allowed_by_policy(void)
{
	if (system_is_in_rw())
		return true;

	if (vboot_allow_usb_pd())
		return true;

	/*
	 * If enable PD in RO on a non-EFS2 device, a hard reset will be issued
	 * when sysjump to RW that makes the device brownout on the dead-battery
	 * case. Disable PD for this special case as a workaround.
	 */
	if (IS_ENABLED(CONFIG_SYSTEM_UNLOCKED) &&
	    (IS_ENABLED(CONFIG_VBOOT_EFS2) ||
	     usb_get_battery_soc() >= CONFIG_USB_PD_TRY_SRC_MIN_BATT_SOC))
		return true;

	return false;
}

static void tc_policy_pd_enable(int port, int en)
{
	if (en)
		deprecated_atomic_clear_bits(&tc[port].pd_disabled_mask,
					     PD_DISABLED_BY_POLICY);
	else
		deprecated_atomic_or(&tc[port].pd_disabled_mask,
				     PD_DISABLED_BY_POLICY);

	CPRINTS("C%d: PD comm policy %sabled", port, en ? "en" : "dis");
}

static void tc_enable_pd(int port, int en)
{
	if (en)
		deprecated_atomic_clear_bits(&tc[port].pd_disabled_mask,
					     PD_DISABLED_NO_CONNECTION);
	else
		deprecated_atomic_or(&tc[port].pd_disabled_mask,
				     PD_DISABLED_NO_CONNECTION);
}

static void tc_enable_try_src(int en)
{
	if (en)
		deprecated_atomic_or(&pd_try_src, 1);
	else
		deprecated_atomic_clear_bits(&pd_try_src, 1);
}

static void tc_detached(int port)
{
	TC_CLR_FLAG(port, TC_FLAGS_TS_DTS_PARTNER);
	hook_notify(HOOK_USB_PD_DISCONNECT);
	tc_pd_connection(port, 0);
	tcpm_debug_accessory(port, 0);
}

static inline void pd_set_dual_role_and_event(int port,
				enum pd_dual_role_states state, uint32_t event)
{
	drp_state[port] = state;

	if (IS_ENABLED(CONFIG_USB_PD_TRY_SRC))
		pd_update_try_source();

	if (event != 0)
		task_set_event(PD_PORT_TO_TASK_ID(port), event, 0);
}

void pd_set_dual_role(int port, enum pd_dual_role_states state)
{
	pd_set_dual_role_and_event(port, state, PD_EVENT_UPDATE_DUAL_ROLE);
}

bool pd_get_partner_data_swap_capable(int port)
{
	/* return data swap capable status of port partner */
	return !!TC_CHK_FLAG(port, TC_FLAGS_PARTNER_DR_DATA);
}

int pd_comm_is_enabled(int port)
{
	return tc_get_pd_enabled(port);
}

void pd_request_data_swap(int port)
{
	/*
	 * Must be in Attached.SRC, Attached.SNK, DebugAccessory.SNK,
	 * or UnorientedDebugAccessory.SRC when this function
	 * is called
	 */
	if (IS_ATTACHED_SRC(port) || IS_ATTACHED_SNK(port)) {
		TC_SET_FLAG(port, TC_FLAGS_REQUEST_DR_SWAP);
		task_wake(PD_PORT_TO_TASK_ID(port));
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

const char *tc_get_current_state(int port)
{
	if (IS_ENABLED(USB_PD_DEBUG_LABELS))
		return tc_state_names[get_state_tc(port)];
	else
		return "";
}

uint32_t tc_get_flags(int port)
{
	return tc[port].flags;
}

int tc_is_attached_src(int port)
{
	return IS_ATTACHED_SRC(port);
}

int tc_is_attached_snk(int port)
{
	return IS_ATTACHED_SNK(port);
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
	if (en) {
		TC_SET_FLAG(port, TC_FLAGS_PARTNER_PD_CAPABLE);
		/* If a PD device is attached then disable deep sleep */
		if (IS_ENABLED(CONFIG_LOW_POWER_IDLE) &&
		    !IS_ENABLED(CONFIG_USB_PD_TCPC_ON_CHIP)) {
			disable_sleep(SLEEP_MASK_USB_PD);
		}
	} else {
		TC_CLR_FLAG(port, TC_FLAGS_PARTNER_PD_CAPABLE);
		/* If a PD device isn't attached then enable deep sleep */
		if (IS_ENABLED(CONFIG_LOW_POWER_IDLE) &&
		    !IS_ENABLED(CONFIG_USB_PD_TCPC_ON_CHIP)) {
			int i;

			/* If all ports are not connected, allow the sleep */
			for (i = 0; i < board_get_usb_pd_port_count(); i++) {
				if (pd_capable(i))
					break;
			}
			if (i == board_get_usb_pd_port_count())
				enable_sleep(SLEEP_MASK_USB_PD);
		}
	}
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

void tc_pr_swap_complete(int port, bool success)
{
	if (IS_ATTACHED_SNK(port)) {
		/*
		 * Give the ADCs in the TCPC or PPC time to react following
		 * a PS_RDY message received during a SRC to SNK swap.
		 * Note: This is empirically determined, not strictly
		 * part of the USB PD spec.
		 * Note: Swap in progress should not be cleared until the
		 * debounce is completed.
		 */
		tc[port].vbus_debounce_time = get_time().val + PD_T_DEBOUNCE;
	} else {
		/* PR Swap is no longer in progress */
		TC_CLR_FLAG(port, TC_FLAGS_PR_SWAP_IN_PROGRESS);

		/*
		 * AutoDischargeDisconnect was turned off near the SNK->SRC
		 * PR-Swap message. If the swap was a success, Vbus should be
		 * valid, so re-enable AutoDischargeDisconnect
		 */
		if (success)
			tcpm_enable_auto_discharge_disconnect(port, 1);
	}
}

void tc_prs_src_snk_assert_rd(int port)
{
	/*
	 * Must be in Attached.SRC or UnorientedDebugAccessory.SRC
	 * when this function is called
	 */
	if (IS_ATTACHED_SRC(port)) {
		/*
		 * Transition to Attached.SNK to
		 * DebugAccessory.SNK assert Rd
		 */
		TC_SET_FLAG(port, TC_FLAGS_REQUEST_PR_SWAP);
		task_wake(PD_PORT_TO_TASK_ID(port));
	}
}

void tc_prs_snk_src_assert_rp(int port)
{
	/*
	 * Must be in Attached.SNK or DebugAccessory.SNK
	 * when this function is called
	 */
	if (IS_ATTACHED_SNK(port)) {
		/*
		 * Transition to Attached.SRC or
		 * UnorientedDebugAccessory.SRC to assert Rp
		 */
		TC_SET_FLAG(port, TC_FLAGS_REQUEST_PR_SWAP);
		task_wake(PD_PORT_TO_TASK_ID(port));
	}
}

/*
 * Hard Reset is being requested.  This should not allow a TC connection
 * to go to an unattached state until the connection is recovered from
 * the hard reset.  It is possible for a Hard Reset to cause a timeout
 * in trying to recover and an additional Hard Reset would be issued.
 * During this entire process it is important that the TC is not allowed
 * to go to an unattached state.
 *
 * Type-C Spec Rev 2.0 section 4.5.2.2.5.2
 * Exiting from Attached.SNK State
 * A port that is not a V CONN-Powered USB Device and is not in the
 * process of a USB PD PR_Swap or a USB PD Hard Reset or a USB PD
 * FR_Swap shall transition to Unattached.SNK
 */
void tc_hard_reset_request(int port)
{
	TC_SET_FLAG(port, TC_FLAGS_HARD_RESET_REQUESTED);
	task_wake(PD_PORT_TO_TASK_ID(port));
}

void tc_disc_ident_in_progress(int port)
{
	TC_SET_FLAG(port, TC_FLAGS_DISC_IDENT_IN_PROGRESS);
}

void tc_disc_ident_complete(int port)
{
	TC_CLR_FLAG(port, TC_FLAGS_DISC_IDENT_IN_PROGRESS);
}

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
	if (IS_ATTACHED_SNK(port)) {
		TC_SET_FLAG(port, TC_FLAGS_POWER_OFF_SNK);
		sink_stop_drawing_current(port);
	}
}

int tc_src_power_on(int port)
{
	if (IS_ATTACHED_SRC(port))
		return pd_set_power_supply_ready(port);

	return 0;
}

void tc_src_power_off(int port)
{
	/* Remove VBUS */
	pd_power_supply_reset(port);

	if (IS_ENABLED(CONFIG_CHARGE_MANAGER))
		charge_manager_set_ceil(port, CEIL_REQUESTOR_PD,
					CHARGE_CEIL_NONE);
}

/*
 * Depending on the load on the processor and the tasks running
 * it can take a while for the task associated with this port
 * to run.  So build in 1ms delays, for up to 300ms, to wait for
 * the suspend to actually happen.
 */
#define SUSPEND_SLEEP_DELAY	1
#define SUSPEND_SLEEP_RETRIES	300

void pd_set_suspend(int port, int suspend)
{
	if (pd_is_port_enabled(port) == !suspend)
		return;

	/* Track if we are suspended or not */
	if (suspend) {
		int wait = 0;

		TC_SET_FLAG(port, TC_FLAGS_SUSPEND);

		/*
		 * Avoid deadlock when running from task
		 * which we are going to suspend
		 */
		if (PD_PORT_TO_TASK_ID(port) == task_get_current())
			return;

		task_wake(PD_PORT_TO_TASK_ID(port));

		/* Sleep this task if we are not suspended */
		while (pd_is_port_enabled(port)) {
			if (++wait > SUSPEND_SLEEP_RETRIES) {
				CPRINTS("C%d: NOT SUSPENDED after %dms",
					port, wait * SUSPEND_SLEEP_DELAY);
				return;
			}
			msleep(SUSPEND_SLEEP_DELAY);
		}
	} else {
		TC_CLR_FLAG(port, TC_FLAGS_SUSPEND);
		task_wake(PD_PORT_TO_TASK_ID(port));
	}
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
	return tc_get_current_state(port);
}

void pd_vbus_low(int port)
{
	TC_CLR_FLAG(port, TC_FLAGS_VBUS_NEVER_LOW);
}

int pd_is_connected(int port)
{
	return (IS_ATTACHED_SRC(port) ||
#ifdef CONFIG_USB_PE_SM
		(get_state_tc(port) == TC_CT_ATTACHED_SNK) ||
#endif
		IS_ATTACHED_SNK(port));
}

bool pd_is_disconnected(int port)
{
	return !pd_is_connected(port);
}

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

/*
 * TCPC CC/Rp management
 */
static void typec_select_pull(int port, enum tcpc_cc_pull pull)
{
	tc[port].select_cc_pull = pull;
}
void typec_select_src_current_limit_rp(int port, enum tcpc_rp_value rp)
{
	tc[port].select_current_limit_rp = rp;
}
void typec_select_src_collision_rp(int port, enum tcpc_rp_value rp)
{
	tc[port].select_collision_rp = rp;
}
static enum tcpc_rp_value typec_get_active_select_rp(int port)
{
	/* Explicit contract will use the collision Rp */
	if (IS_ENABLED(CONFIG_USB_PD_REV30) &&
	    pe_is_explicit_contract(port))
		return tc[port].select_collision_rp;
	return tc[port].select_current_limit_rp;
}
int typec_update_cc(int port)
{
	int rv;
	enum tcpc_cc_pull pull = tc[port].select_cc_pull;
	enum tcpc_rp_value rp = typec_get_active_select_rp(port);

	rv = tcpm_select_rp_value(port, rp);
	if (rv)
		return rv;

	return tcpm_set_cc(port, pull);
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
		tc_src_power_on(port);

		/* Update the Rp Value */
		typec_update_cc(port);

		/* Turn off VCONN */
		set_vconn(port, 1);

		tc[port].ps_reset_state = PS_STATE2;
		tc[port].timeout = get_time().val +
				PD_POWER_SUPPLY_TURN_ON_DELAY;
		return false;
	case PS_STATE2:
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

/*
 * Wait for recovery after a hard reset.  Call repeatedly until true is
 * returned, signaling that the hard reset is complete.
 */
static bool tc_perform_snk_hard_reset(int port)
{
	switch (tc[port].ps_reset_state) {
	case PS_STATE0:
		/* Shutting off power, Disable AutoDischargeDisconnect */
		tcpm_enable_auto_discharge_disconnect(port, 0);

		/* Hard reset sets us back to default data role */
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
		/* Wait tSafe0V + tSrcRecover, then check for Vbus presence */
		tc[port].ps_reset_state = PS_STATE1;
		tc[port].timeout = get_time().val + PD_T_SAFE_0V +
							PD_T_SRC_RECOVER_MAX;
		return false;
	case PS_STATE1:
		if (get_time().val < tc[port].timeout)
			return false;

		/* Watch for Vbus to return */
		tc[port].ps_reset_state = PS_STATE2;
		tc[port].timeout = get_time().val + PD_T_SRC_TURN_ON;
		return false;
	case PS_STATE2:
		if (pd_is_vbus_present(port)) {
			/*
			 * Inform policy engine that power supply
			 * reset is complete
			 */
			tc[port].ps_reset_state = PS_STATE0;
			pe_ps_reset_complete(port);

			/*
			 * Now that VBUS is back, let's notify charge manager
			 * regarding the source's current capabilities.
			 * sink_power_sub_states() reacts to changes in CC
			 * terminations, however during a HardReset, the
			 * terminations of a non-PD port partner will not
			 * change.  Therefore, set the debounce time to right
			 * now, such that we'll actually reset the correct input
			 * current limit.
			 */
			tc[port].cc_debounce = get_time().val;
			sink_power_sub_states(port);

			/* Power is back, Enable AutoDischargeDisconnect */
			tcpm_enable_auto_discharge_disconnect(port, 1);
			return true;
		}
		/*
		 * If Vbus isn't back after wait + tSrcTurnOn, go unattached
		 */
		if (get_time().val > tc[port].timeout) {
			tc[port].ps_reset_state = PS_STATE0;
			set_state_tc(port, TC_UNATTACHED_SNK);
			return true;
		}
	}

	return false;
}
#endif /* CONFIG_USB_PE_SM */

void tc_start_error_recovery(int port)
{
	assert(port == TASK_ID_TO_PD_PORT(task_get_current()));

	/*
	 *   The port should transition to the ErrorRecovery state
	 *   from any other state when directed.
	 */
	set_state_tc(port, TC_ERROR_RECOVERY);
}

static void restart_tc_sm(int port, enum usb_tc_state start_state)
{
	int res;

	/* Clear flags before we transitions states */
	tc[port].flags = 0;

	res = tcpm_init(port);

	CPRINTS("C%d: TCPC init %s", port, res ? "failed" : "ready");

	/*
	 * Update the Rp Value. We don't need to update CC lines though as that
	 * happens in below set_state transition.
	 */
	typec_select_src_current_limit_rp(port, CONFIG_USB_PD_PULLUP);

	/* Disable if restart failed, otherwise start in default state. */
	set_state_tc(port, res ? TC_DISABLED : start_state);

	if (IS_ENABLED(CONFIG_USBC_SS_MUX))
		/* Initialize USB mux to its default state */
		usb_mux_init(port);

	if (IS_ENABLED(CONFIG_USBC_PPC)) {
		/*
		 * Wait to initialize the PPC after tcpc, which sets
		 * the correct Rd values; otherwise the TCPC might
		 * not be pulling the CC lines down when the PPC connects the
		 * CC lines from the USB connector to the TCPC cause the source
		 * to drop Vbus causing a brown out.
		 */
		ppc_init(port);
	}

	if (IS_ENABLED(CONFIG_CHARGE_MANAGER)) {
		/* Initialize PD and type-C supplier current limits to 0 */
		pd_set_input_current_limit(port, 0, 0);
		typec_set_input_current_limit(port, 0, 0);
		charge_manager_update_dualrole(port, CAP_UNKNOWN);
	}

#ifdef CONFIG_USB_PE_SM
	tc_enable_pd(port, 0);
	tc[port].ps_reset_state = PS_STATE0;
#endif
}

void tc_state_init(int port)
{
	enum usb_tc_state first_state;

	/* For test builds, replicate static initialization */
	if (IS_ENABLED(TEST_BUILD)) {
		int i;

		for (i = 0; i < CONFIG_USB_PD_PORT_MAX_COUNT; ++i) {
			memset(&tc[i], 0, sizeof(tc[i]));
			drp_state[i] = CONFIG_USB_PD_INITIAL_DRP_STATE;
		}
	}

	/* If port is not available, there is nothing to initialize */
	if (port >= board_get_usb_pd_port_count()) {
		tc_enable_pd(port, 0);
		tc_pause_event_loop(port);
		TC_SET_FLAG(port, TC_FLAGS_SUSPEND);
		return;
	}

	/* Allow system to set try src enable */
	tc_try_src_override(TRY_SRC_NO_OVERRIDE);

	/*
	 * Set initial PD communication policy.
	 */
	tc_policy_pd_enable(port, pd_comm_allowed_by_policy());

	/* Set dual-role state based on chipset power state */
	if (chipset_in_state(CHIPSET_STATE_ANY_OFF))
		pd_set_dual_role_and_event(port, PD_DRP_FORCE_SINK, 0);
	else if (chipset_in_state(CHIPSET_STATE_ANY_SUSPEND))
		pd_set_dual_role_and_event(port, pd_get_drp_state_in_suspend(), 0);
	else /* CHIPSET_STATE_ON */
		pd_set_dual_role_and_event(port, PD_DRP_TOGGLE_ON, 0);

	/*
	 * If we just lost power, don't apply CC open. Otherwise we would boot
	 * loop, and if this is a fresh power on, then we know there isn't any
	 * stale PD state as well.
	 */
	if (system_get_reset_flags() &
	    (EC_RESET_FLAG_BROWNOUT | EC_RESET_FLAG_POWER_ON)) {
		first_state = TC_UNATTACHED_SNK;

		/* Turn off any previous sourcing */
		tc_src_power_off(port);
		set_vconn(port, 0);
	} else {
		first_state = TC_ERROR_RECOVERY;
	}

#ifdef CONFIG_USB_PD_TCPC_BOARD_INIT
	/* Board specific TCPC init */
	board_tcpc_init();
#endif

	/*
	 * Start with ErrorRecovery state if we can to put us in
	 * a clean state from any previous boots.
	 */
	restart_tc_sm(port, first_state);
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

bool pd_alt_mode_capable(int port)
{
	return IS_ENABLED(CONFIG_USB_PE_SM) && tc_get_pd_enabled(port);
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
	assert(port == TASK_ID_TO_PD_PORT(task_get_current()));

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
	if (IS_ENABLED(USB_PD_DEBUG_LABELS))
		CPRINTS_L1("C%d: %s", port, tc_state_names[get_state_tc(port)]);
	else
		CPRINTS("C%d: tc-st%d", port, get_state_tc(port));
}

static void handle_device_access(int port)
{
	tc[port].low_power_time = get_time().val + PD_LPM_DEBOUNCE_US;
}

void tc_event_check(int port, int evt)
{
#ifdef DEBUG_PRINT_FLAG_AND_EVENT_NAMES
	if (evt != TASK_EVENT_TIMER)
		print_bits("Event", evt, event_bit_names,
			   ARRAY_SIZE(event_bit_names));
#endif

	if (evt & PD_EXIT_LOW_POWER_EVENT_MASK)
		TC_SET_FLAG(port, TC_FLAGS_CHECK_CONNECTION);

	if (evt & PD_EVENT_DEVICE_ACCESSED)
		handle_device_access(port);

	if (evt & PD_EVENT_TCPC_RESET)
		reset_device_and_notify(port);

	if (evt & PD_EVENT_RX_HARD_RESET)
		pd_execute_hard_reset(port);

	if (evt & PD_EVENT_SEND_HARD_RESET)
		tc_hard_reset_request(port);

#ifdef CONFIG_POWER_COMMON
	if (IS_ENABLED(CONFIG_POWER_COMMON)) {
		if (evt & PD_EVENT_POWER_STATE_CHANGE)
			handle_new_power_state(port);
	}
#endif /* CONFIG_POWER_COMMON */

#ifdef CONFIG_USB_PD_ALT_MODE_DFP
	{
		int i;

		if (IS_ENABLED(CONFIG_USB_PD_ALT_MODE_DFP)) {
			/*
			 * Notify all ports of sysjump
			 */
			if (evt & PD_EVENT_SYSJUMP) {
				for (i = 0; i <
					CONFIG_USB_PD_PORT_MAX_COUNT; i++)
					dpm_set_mode_exit_request(i);
				notify_sysjump_ready();
			}
		}
	}
#endif

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
	 * Disable PPC Vconn first then TCPC in case the voltage feeds back
	 * to TCPC and damages.
	 */
	if (IS_ENABLED(CONFIG_USBC_PPC_VCONN) && !enable)
		ppc_set_vconn(port, 0);

	/*
	 * We always need to tell the TCPC to enable Vconn first, otherwise some
	 * TCPCs get confused and think the CC line is in over voltage mode and
	 * immediately disconnects. If there is a PPC, both devices will
	 * potentially source Vconn, but that should be okay since Vconn has
	 * "make before break" electrical requirements when swapping anyway.
	 */
	tcpm_set_vconn(port, enable);

	if (IS_ENABLED(CONFIG_USBC_PPC_VCONN) && enable)
		ppc_set_vconn(port, 1);
}

/* This must only be called from the PD task */
static void pd_update_dual_role_config(int port)
{
	if (tc[port].power_role == PD_ROLE_SOURCE &&
			((drp_state[port] == PD_DRP_FORCE_SINK &&
			!pd_ts_dts_plugged(port)) ||
			(drp_state[port] == PD_DRP_TOGGLE_OFF &&
			get_state_tc(port) == TC_UNATTACHED_SRC))) {
		/*
		 * Change to sink if port is currently a source AND (new DRP
		 * state is force sink OR new DRP state is either toggle off
		 * or debug accessory toggle only and we are in the source
		 * disconnected state).
		 */
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
			 * The SoC will negotiate alternate mode again when it
			 * boots up
			 */
			dpm_set_mode_exit_request(port);

			/*
			 * The following function will disconnect both USB and
			 * DP mux, as the chipset is transitioning to OFF.
			 */
			set_usb_mux_with_current_data_role(port);
		}
	}
}
#endif /* CONFIG_POWER_COMMON */

#if defined(CONFIG_USB_PD_ALT_MODE) && !defined(CONFIG_USB_PD_ALT_MODE_DFP)
void pd_send_hpd(int port, enum hpd_event hpd)
{
	uint32_t data[1];
	int opos = pd_alt_mode(port, TCPC_TX_SOP, USB_SID_DISPLAYPORT);

	if (!opos)
		return;

	data[0] = VDO_DP_STATUS((hpd == hpd_irq), /* IRQ_HPD */
				(hpd != hpd_low), /* HPD_HI|LOW */
				0, /* request exit DP */
				0, /* request exit USB */
				0, /* MF pref */
				1, /* enabled */
				0, /* power low */
				0x2);
	pd_send_vdm(port, USB_SID_DISPLAYPORT, VDO_OPOS(opos) | CMD_ATTENTION,
		    data, 1);
}
#endif

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
	pd_dpm_request(port, DPM_REQUEST_VCONN_SWAP);
}
#endif

int tc_is_vconn_src(int port)
{
	if (IS_ENABLED(CONFIG_USBC_VCONN))
		return TC_CHK_FLAG(port, TC_FLAGS_VCONN_ON);
	else
		return 0;
}

static __maybe_unused int reset_device_and_notify(int port)
{
	int rv;
	int task, waiting_tasks;

	/* This should only be called from the PD task */
	assert(port == TASK_ID_TO_PD_PORT(task_get_current()));

	TC_SET_FLAG(port, TC_FLAGS_LPM_TRANSITION);
	rv = tcpm_init(port);
	TC_CLR_FLAG(port, TC_FLAGS_LPM_TRANSITION);
	TC_CLR_FLAG(port, TC_FLAGS_LPM_ENGAGED);
	tc_start_event_loop(port);

	if (rv == EC_SUCCESS)
		CPRINTS("C%d: TCPC init ready", port);
	else
		CPRINTS("C%d: TCPC init failed!", port);

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
	deprecated_atomic_clear_bits(task_get_event_bitmap(task_get_current()),
				     PD_EVENT_TCPC_RESET);

	waiting_tasks =
		deprecated_atomic_read_clear(&tc[port].tasks_waiting_on_reset);

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
	if (!TC_CHK_FLAG(port, TC_FLAGS_LPM_ENGAGED))
		return;

	if (port == TASK_ID_TO_PD_PORT(task_get_current())) {
		if (!TC_CHK_FLAG(port, TC_FLAGS_LPM_TRANSITION))
			reset_device_and_notify(port);
	} else {
		/* Otherwise, we need to wait for the TCPC reset to complete */
		deprecated_atomic_or(&tc[port].tasks_waiting_on_reset,
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
		deprecated_atomic_or(&tc[port].tasks_preventing_lpm,
				     current_task_mask);
	else
		deprecated_atomic_clear_bits(&tc[port].tasks_preventing_lpm,
					     current_task_mask);
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
	/*
	 * If pd_set_suspend SUSPEND state changes to
	 * no longer be suspended then we need to exit
	 * our current state and go UNATTACHED_SNK
	 */
	if (!TC_CHK_FLAG(port, TC_FLAGS_SUSPEND))
		set_state_tc(port, TC_UNATTACHED_SNK);

	task_wait_event(-1);
}

static void tc_disabled_exit(const int port)
{
	if (!IS_ENABLED(CONFIG_USB_PD_TCPC)) {
		if (tcpm_init(port) != 0) {
			CPRINTS("C%d: TCPC restart failed!", port);
			return;
		}
	}

	CPRINTS("C%d: TCPC resumed!", port);
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
	if (get_time().val < tc[port].timeout)
		return;

	/*
	 * If we transitioned to error recovery as the first state and we
	 * didn't brown out, we don't need to reinitialized the tc statemachine
	 * because we just did that. So transition to the state directly.
	 */
	if (tc[port].ctx.previous == NULL) {
		set_state_tc(port, TC_UNATTACHED_SNK);
		return;
	}

	/*
	 * If try src support is active (e.g. in S0). Then try to become the
	 * SRC, otherwise we should try to be the sink.
	 */
	restart_tc_sm(port, is_try_src_enabled(port) ? TC_UNATTACHED_SRC :
						       TC_UNATTACHED_SNK);
}

/**
 * Unattached.SNK
 */
static void tc_unattached_snk_entry(const int port)
{
	if (get_last_state_tc(port) != TC_UNATTACHED_SRC) {
		tc_detached(port);
		print_current_state(port);
	}

	/*
	 * We are in an unattached state and considering to be a SNK
	 * searching for a SRC partner.  We set the CC pull value to
	 * to indicate our intent to be SNK in hopes a partner SRC
	 * will is there to attach to.
	 *
	 * Both CC1 and CC2 pins shall be independently terminated to
	 * ground through Rd.
	 */
	typec_select_pull(port, TYPEC_CC_RD);
	typec_update_cc(port);

	/*
	 * Tell Policy Engine to invalidate the explicit contract.
	 * This mainly used to clear the BB Ram Explicit Contract
	 * value.
	 */
	pe_invalidate_explicit_contract(port);

	tc[port].data_role = PD_ROLE_DISCONNECTED;

	/*
	 * Saved SRC_Capabilities are no longer valid on disconnect
	 */
	pd_set_src_caps(port, 0, NULL);

	/*
	 * When data role set events are used to enable BC1.2, then CC
	 * detach events are used to notify BC1.2 that it can be powered
	 * down.
	 */
	if (IS_ENABLED(CONFIG_BC12_DETECT_DATA_ROLE_TRIGGER))
		bc12_role_change_handler(port);

	if (IS_ENABLED(CONFIG_CHARGE_MANAGER))
		charge_manager_update_dualrole(port, CAP_UNKNOWN);

	if (IS_ENABLED(CONFIG_USBC_PPC)) {
		/*
		 * Clear the overcurrent event counter
		 * since we've detected a disconnect.
		 */
		ppc_clear_oc_event_counter(port);
	}

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
		if (TC_CHK_FLAG(port, TC_FLAGS_HARD_RESET_REQUESTED)) {
			TC_CLR_FLAG(port, TC_FLAGS_HARD_RESET_REQUESTED);
			tc_set_data_role(port, PD_ROLE_UFP);
			/* Inform Policy Engine that hard reset is complete */
			pe_ps_reset_complete(port);
		}
	}

	/* Check for connection */
	tcpm_get_cc(port, &cc1, &cc2);

	/*
	 * Attempt TCPC auto DRP toggle if it is
	 * not already auto toggling.
	 */
	if (IS_ENABLED(CONFIG_USB_PD_DUAL_ROLE_AUTO_TOGGLE) &&
	    drp_state[port] == PD_DRP_TOGGLE_ON &&
	    tcpm_auto_toggle_supported(port) && cc_is_open(cc1, cc2)) {
		set_state_tc(port, TC_DRP_AUTO_TOGGLE);
		return;
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
	if (cc_is_rp(cc1) || cc_is_rp(cc2)) {
		/* Connection Detected */
		set_state_tc(port, TC_ATTACH_WAIT_SNK);
	} else if (get_time().val > tc[port].next_role_swap &&
		   drp_state[port] == PD_DRP_TOGGLE_ON) {
		/* DRP Toggle */
		set_state_tc(port, TC_UNATTACHED_SRC);
	} else if (IS_ENABLED(CONFIG_USB_PD_TCPC_LOW_POWER) &&
		   (drp_state[port] == PD_DRP_FORCE_SINK ||
		    drp_state[port] == PD_DRP_TOGGLE_OFF)) {
		set_state_tc(port, TC_LOW_POWER_MODE);
	}
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
	 * A DRP shall transition to Unattached.SRC when the state of both
	 * the CC1 and CC2 pins is SNK.Open for at least tPDDebounce, however
	 * when DRP state prevents switch to SRC the next state should be
	 * Unattached.SNK.
	 */
	if (new_cc_state == PD_CC_NONE &&
				get_time().val > tc[port].pd_debounce) {
		if (IS_ENABLED(CONFIG_USB_PE_SM) &&
				IS_ENABLED(CONFIG_USB_PD_ALT_MODE_DFP)) {
			pd_dfp_exit_mode(port, TCPC_TX_SOP, 0, 0);
			pd_dfp_exit_mode(port, TCPC_TX_SOP_PRIME, 0, 0);
			pd_dfp_exit_mode(port, TCPC_TX_SOP_PRIME_PRIME, 0, 0);
		}

		/* We are detached */
		if (drp_state[port] == PD_DRP_TOGGLE_OFF
		    || drp_state[port] == PD_DRP_FREEZE
		    || drp_state[port] == PD_DRP_FORCE_SINK)
			set_state_tc(port, TC_UNATTACHED_SNK);
		else
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
			CPRINTS("C%d: Debug accessory detected", port);
			TC_SET_FLAG(port, TC_FLAGS_TS_DTS_PARTNER);
			set_state_tc(port, TC_ATTACHED_SNK);
		}

		if (IS_ENABLED(CONFIG_USB_PE_SM) &&
				IS_ENABLED(CONFIG_USB_PD_ALT_MODE_DFP)) {
			hook_call_deferred(&pd_usb_billboard_deferred_data,
								PD_T_AME);
		}
	}
}

/**
 * Attached.SNK, shared with Debug Accessory.SNK
 */
static void tc_attached_snk_entry(const int port)
{
	enum tcpc_cc_voltage_status cc1, cc2;

	print_current_state(port);

	/*
	 * Known state of attach is SNK.  We need to apply this pull value
	 * to make it set in hardware at the correct time but set the common
	 * pull here.
	 *
	 * Both CC1 and CC2 pins shall be independently terminated to
	 * ground through Rd.
	 */
	typec_select_pull(port, TYPEC_CC_RD);

#ifdef CONFIG_USB_PE_SM
	if (TC_CHK_FLAG(port, TC_FLAGS_PR_SWAP_IN_PROGRESS)) {
		/* Flipping power role - Disable AutoDischargeDisconnect */
		tcpm_enable_auto_discharge_disconnect(port, 0);

		/* Apply Rd */
		typec_update_cc(port);

		/* Change role to sink */
		tc_set_power_role(port, PD_ROLE_SINK);
		tcpm_set_msg_header(port, tc[port].power_role,
							tc[port].data_role);

		/*
		 * Maintain VCONN supply state, whether ON or OFF, and its
		 * data role / usb mux connections. Do not re-enable
		 * AutoDischargeDisconnect until the swap is completed
		 * and tc_pr_swap_complete is called.
		 */
	} else
#endif
	{
		/* Get connector orientation */
		tcpm_get_cc(port, &cc1, &cc2);
		tc[port].polarity = get_snk_polarity(cc1, cc2);
		pd_set_polarity(port, tc[port].polarity);

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

		/* Attached.SNK - enable AutoDischargeDisconnect */
		tcpm_enable_auto_discharge_disconnect(port, 1);

		/* Apply Rd */
		typec_update_cc(port);
	}

	tc[port].cc_debounce = 0;

	/* Enable PD */
	if (IS_ENABLED(CONFIG_USB_PE_SM))
		tc_enable_pd(port, 1);

	if (TC_CHK_FLAG(port, TC_FLAGS_TS_DTS_PARTNER))
		tcpm_debug_accessory(port, 1);
}

static void tc_attached_snk_run(const int port)
{
#ifdef CONFIG_USB_PE_SM
	/*
	 * Perform Hard Reset
	 */
	if (TC_CHK_FLAG(port, TC_FLAGS_HARD_RESET_REQUESTED)) {
		/*
		 * Wait to clear the hard reset request until Vbus has returned
		 * to default (or, if it didn't return, we transition to
		 * unattached)
		 */
		if (tc_perform_snk_hard_reset(port))
			TC_CLR_FLAG(port, TC_FLAGS_HARD_RESET_REQUESTED);

		return;
	}

	/*
	 * From 4.5.2.2.5.2 Exiting from Attached.SNK State:
	 *
	 * "A port that is not a Vconn-Powered USB Device and is not in the
	 * process of a USB PD PR_Swap or a USB PD Hard Reset or a USB PD
	 * FR_Swap shall transition to Unattached.SNK within tSinkDisconnect
	 * when Vbus falls below vSinkDisconnect for Vbus operating at or
	 * below 5 V or below vSinkDisconnectPD when negotiated by USB PD
	 * to operate above 5 V."
	 *
	 * TODO(b/149530538): Use vSinkDisconnectPD when above 5V
	 */

	/*
	 * Debounce Vbus before we drop that we are doing a PR_Swap
	 */
	if (TC_CHK_FLAG(port, TC_FLAGS_PR_SWAP_IN_PROGRESS) &&
	    tc[port].vbus_debounce_time < get_time().val) {
		/* PR Swap is no longer in progress */
		TC_CLR_FLAG(port, TC_FLAGS_PR_SWAP_IN_PROGRESS);

		/*
		 * AutoDischargeDisconnect was turned off when we
		 * hit Safe0V on SRC->SNK PR-Swap. We now are done
		 * with the swap and should have Vbus, so re-enable
		 * AutoDischargeDisconnect.
		 */
		if (!pd_check_vbus_level(port, VBUS_REMOVED))
			tcpm_enable_auto_discharge_disconnect(port, 1);
	}

	/*
	 * The sink will be powered off during a power role swap but we don't
	 * want to trigger a disconnect.
	 */
	if (!TC_CHK_FLAG(port, TC_FLAGS_POWER_OFF_SNK) &&
	    !TC_CHK_FLAG(port, TC_FLAGS_PR_SWAP_IN_PROGRESS)) {
		/*
		 * Detach detection
		 */
		if (pd_check_vbus_level(port, VBUS_REMOVED)) {
			if (IS_ENABLED(CONFIG_USB_PD_ALT_MODE_DFP)) {
				pd_dfp_exit_mode(port, TCPC_TX_SOP, 0, 0);
				pd_dfp_exit_mode(port, TCPC_TX_SOP_PRIME, 0, 0);
				pd_dfp_exit_mode(port, TCPC_TX_SOP_PRIME_PRIME,
						0, 0);
			}

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
		if (TC_CHK_FLAG(port, TC_FLAGS_REQUEST_PR_SWAP)) {
			/*
			 * We may want to verify partner is applying Rd before
			 * we swap. However, some TCPCs (such as TUSB422) will
			 * not report the correct CC status before VBUS falls to
			 * vSafe0V, so this will be problematic in the FRS case.
			 */
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
		 * UnorientedDebugAccessory.SRC shall not drive Vconn
		 */
		if (!TC_CHK_FLAG(port, TC_FLAGS_TS_DTS_PARTNER)) {
			if (TC_CHK_FLAG(port, TC_FLAGS_REQUEST_VC_SWAP_ON)) {
				TC_CLR_FLAG(port, TC_FLAGS_REQUEST_VC_SWAP_ON);

				set_vconn(port, 1);
				/*
				 * Inform policy engine that vconn swap is
				 * complete
				 */
				pe_vconn_swap_complete(port);
			} else if (TC_CHK_FLAG(port,
					       TC_FLAGS_REQUEST_VC_SWAP_OFF)) {
				TC_CLR_FLAG(port, TC_FLAGS_REQUEST_VC_SWAP_OFF);

				set_vconn(port, 0);
				/*
				 * Inform policy engine that vconn swap is
				 * complete
				 */
				pe_vconn_swap_complete(port);
			}
		}
#endif
		if (!TC_CHK_FLAG(port, TC_FLAGS_TS_DTS_PARTNER)) {
			/*
			 * If the port supports Charge-Through VCONN-Powered USB
			 * devices, and an explicit PD contract has failed to be
			 * negotiated, the port shall query the identity of the
			 * cable via USB PD on SOP’
			 */
			if (!pe_is_explicit_contract(port) &&
			    TC_CHK_FLAG(port, TC_FLAGS_CTVPD_DETECTED)) {
				/*
				 * A port that via SOP’ has detected an
				 * attached Charge-Through VCONN-Powered USB
				 * device shall transition to Unattached.SRC
				 * if an explicit PD contract has failed to
				 * be negotiated.
				 */
				/* CTVPD detected */
				set_state_tc(port, TC_UNATTACHED_SRC);
			}
		}
	}

#else /* CONFIG_USB_PE_SM */

	/* Detach detection */
	if (pd_check_vbus_level(port, VBUS_REMOVED)) {
		set_state_tc(port, TC_UNATTACHED_SNK);
		return;
	}

	/* Run Sink Power Sub-State */
	sink_power_sub_states(port);
#endif /* CONFIG_USB_PE_SM */
}

static void tc_attached_snk_exit(const int port)
{
	if (!TC_CHK_FLAG(port, TC_FLAGS_REQUEST_PR_SWAP)) {
		/*
		 * If supplying VCONN, the port shall cease to supply
		 * it within tVCONNOFF of exiting Attached.SNK if not
		 * PR swapping.
		 */
		if (TC_CHK_FLAG(port, TC_FLAGS_VCONN_ON))
			set_vconn(port, 0);

		/*
		 * Attached.SNK exit - disable AutoDischargeDisconnect
		 * NOTE: This should not happen if we are suspending. It will
		 * happen in tc_cc_open_entry if that is the path we are
		 * taking.
		 */
		if (!TC_CHK_FLAG(port, TC_FLAGS_SUSPEND))
			tcpm_enable_auto_discharge_disconnect(port, 0);
	}

	/* Clear flags after checking Vconn status */
	TC_CLR_FLAG(port, TC_FLAGS_REQUEST_PR_SWAP | TC_FLAGS_POWER_OFF_SNK);

	/* Stop drawing power */
	sink_stop_drawing_current(port);
}

/**
 * Unattached.SRC
 */
static void tc_unattached_src_entry(const int port)
{
	if (get_last_state_tc(port) != TC_UNATTACHED_SNK) {
		tc_detached(port);
		print_current_state(port);
	}

	/*
	 * We are in an unattached state and considering to be a SRC
	 * searching for a SNK partner.  We set the CC pull value to
	 * to indicate our intent to be SRC in hopes a partner SNK
	 * will is there to attach to.
	 *
	 * Both CC1 and CC2 pins shall be independently terminated to
	 * ground through Rp.
	 */
	typec_select_pull(port, TYPEC_CC_RP);
	typec_select_src_current_limit_rp(port, CONFIG_USB_PD_PULLUP);
	typec_update_cc(port);

	tc[port].data_role = PD_ROLE_DISCONNECTED;

	/*
	 * Saved SRC_Capabilities are no longer valid on disconnect
	 */
	pd_set_src_caps(port, 0, NULL);

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
		if (TC_CHK_FLAG(port, TC_FLAGS_HARD_RESET_REQUESTED)) {
			TC_CLR_FLAG(port, TC_FLAGS_HARD_RESET_REQUESTED);
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
	 * Transition to AttachWait.SRC when:
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
	/*
	 * Attempt TCPC auto DRP toggle
	 */
	else if (IS_ENABLED(CONFIG_USB_PD_DUAL_ROLE_AUTO_TOGGLE) &&
		 drp_state[port] == PD_DRP_TOGGLE_ON &&
		 tcpm_auto_toggle_supported(port) && cc_is_open(cc1, cc2))
		set_state_tc(port, TC_DRP_AUTO_TOGGLE);
	else if (IS_ENABLED(CONFIG_USB_PD_TCPC_LOW_POWER) &&
		 (drp_state[port] == PD_DRP_FORCE_SOURCE ||
		  drp_state[port] == PD_DRP_TOGGLE_OFF))
		set_state_tc(port, TC_LOW_POWER_MODE);
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
	if (pd_check_vbus_level(port, VBUS_SAFE0V)) {
		if (new_cc_state == PD_CC_UFP_ATTACHED) {
			set_state_tc(port, TC_ATTACHED_SRC);
			return;
		} else if (new_cc_state == PD_CC_UFP_DEBUG_ACC) {
			CPRINTS("C%d: Debug accessory detected", port);
			TC_SET_FLAG(port, TC_FLAGS_TS_DTS_PARTNER);
			set_state_tc(port, TC_ATTACHED_SRC);
			return;
		}
	}
}

/**
 * Attached.SRC, shared with UnorientedDebugAccessory.SRC
 */
static void tc_attached_src_entry(const int port)
{
	enum tcpc_cc_voltage_status cc1, cc2;

	print_current_state(port);

	/* Run function relies on timeout being 0 or meaningful */
	tc[port].timeout = 0;

	/*
	 * Known state of attach is SRC.  We need to apply this pull value
	 * to make it set in hardware at the correct time but set the common
	 * pull here.
	 *
	 * Both CC1 and CC2 pins shall be independently terminated to
	 * pulled up through Rp.
	 */
	typec_select_pull(port, TYPEC_CC_RP);
	typec_select_src_current_limit_rp(port, CONFIG_USB_PD_PULLUP);

#if defined(CONFIG_USB_PE_SM)
	if (TC_CHK_FLAG(port, TC_FLAGS_PR_SWAP_IN_PROGRESS)) {
		/* Change role to source */
		tc_set_power_role(port, PD_ROLE_SOURCE);
		tcpm_set_msg_header(port,
				tc[port].power_role, tc[port].data_role);

		/* Enable VBUS */
		tc_src_power_on(port);

		/* Apply Rp */
		typec_update_cc(port);

		/*
		 * Maintain VCONN supply state, whether ON or OFF, and its
		 * data role / usb mux connections. Do not re-enable
		 * AutoDischargeDisconnect until the swap is completed
		 * and tc_pr_swap_complete is called.
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
		 *
		 * UnorientedDebugAccessory.SRC shall not drive Vconn
		 */
		if (IS_ENABLED(CONFIG_USBC_VCONN) &&
				!TC_CHK_FLAG(port, TC_FLAGS_TS_DTS_PARTNER))
			set_vconn(port, 1);

		/* Enable VBUS */
		if (tc_src_power_on(port)) {
			/* Stop sourcing Vconn if Vbus failed */
			if (IS_ENABLED(CONFIG_USBC_VCONN))
				set_vconn(port, 0);

			if (IS_ENABLED(CONFIG_USBC_SS_MUX))
				usb_mux_set(port, USB_PD_MUX_NONE,
				USB_SWITCH_DISCONNECT, tc[port].polarity);
		}

		/* Attached.SRC - enable AutoDischargeDisconnect */
		tcpm_enable_auto_discharge_disconnect(port, 1);

		/* Apply Rp */
		typec_update_cc(port);

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
	 *
	 * UnorientedDebugAccessory.SRC shall not drive Vconn
	 */
	if (IS_ENABLED(CONFIG_USBC_VCONN) &&
				!TC_CHK_FLAG(port, TC_FLAGS_TS_DTS_PARTNER))
		set_vconn(port, 1);

	/* Enable VBUS */
	if (tc_src_power_on(port)) {
		/* Stop sourcing Vconn if Vbus failed */
		if (IS_ENABLED(CONFIG_USBC_VCONN))
			set_vconn(port, 0);

		if (IS_ENABLED(CONFIG_USBC_SS_MUX))
			usb_mux_set(port, USB_PD_MUX_NONE,
			USB_SWITCH_DISCONNECT, tc[port].polarity);
	}

	/* Attached.SRC - enable AutoDischargeDisconnect */
	tcpm_enable_auto_discharge_disconnect(port, 1);

	/* Apply Rp */
	typec_update_cc(port);

#endif /* CONFIG_USB_PE_SM */

	/* Inform PPC that a sink is connected. */
	if (IS_ENABLED(CONFIG_USBC_PPC))
		ppc_sink_is_connected(port, 1);

	/*
	 * Only notify if we're not performing a power role swap.  During a
	 * power role swap, the port partner is not disconnecting/connecting.
	 */
	if (!TC_CHK_FLAG(port, TC_FLAGS_PR_SWAP_IN_PROGRESS)) {
		hook_notify(HOOK_USB_PD_CONNECT);
	}

	if (TC_CHK_FLAG(port, TC_FLAGS_TS_DTS_PARTNER))
		tcpm_debug_accessory(port, 1);
}

static void tc_attached_src_run(const int port)
{
	enum tcpc_cc_voltage_status cc1, cc2;

	/* Check for connection */
	tcpm_get_cc(port, &cc1, &cc2);

	if (tc[port].polarity)
		cc1 = cc2;

	if (cc1 == TYPEC_CC_VOLT_OPEN)
		tc[port].cc_state = PD_CC_NONE;
	else
		tc[port].cc_state = PD_CC_UFP_ATTACHED;

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

		const bool tryWait = is_try_src_enabled(port) &&
				!TC_CHK_FLAG(port, TC_FLAGS_TS_DTS_PARTNER);

		if (IS_ENABLED(CONFIG_USB_PE_SM))
			if (IS_ENABLED(CONFIG_USB_PD_ALT_MODE_DFP)) {
				pd_dfp_exit_mode(port, TCPC_TX_SOP, 0, 0);
				pd_dfp_exit_mode(port, TCPC_TX_SOP_PRIME, 0, 0);
				pd_dfp_exit_mode(port, TCPC_TX_SOP_PRIME_PRIME,
						0, 0);
			}

		set_state_tc(port, tryWait ?
					TC_TRY_WAIT_SNK : TC_UNATTACHED_SNK);
		return;
	}

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
	if (TC_CHK_FLAG(port, TC_FLAGS_HARD_RESET_REQUESTED)) {
		/* Ignoring Hard Resets while the power supply is resetting.*/
		if (get_time().val < tc[port].timeout)
			return;

		if (tc_perform_src_hard_reset(port))
			TC_CLR_FLAG(port, TC_FLAGS_HARD_RESET_REQUESTED);

		return;
	}

	/*
	 * PD swap commands
	 */
	if (tc_get_pd_enabled(port) && prl_is_running(port)) {
		/*
		 * Power Role Swap Request
		 */
		if (TC_CHK_FLAG(port, TC_FLAGS_REQUEST_PR_SWAP)) {
			/* Clear TC_FLAGS_REQUEST_PR_SWAP on exit */
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

		/*
		 * Vconn Swap Request
		 * UnorientedDebugAccessory.SRC shall not drive Vconn
		 */
		if (IS_ENABLED(CONFIG_USBC_VCONN) &&
				!TC_CHK_FLAG(port, TC_FLAGS_TS_DTS_PARTNER)) {
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
		if (!TC_CHK_FLAG(port, TC_FLAGS_TS_DTS_PARTNER) &&
			TC_CHK_FLAG(port, TC_FLAGS_CTVPD_DETECTED)) {
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

	if (!TC_CHK_FLAG(port, TC_FLAGS_REQUEST_PR_SWAP)) {
		/* Attached.SRC exit - disable AutoDischargeDisconnect */
		tcpm_enable_auto_discharge_disconnect(port, 0);

		/* Disable VCONN if not power role swapping */
		if (TC_CHK_FLAG(port, TC_FLAGS_VCONN_ON))
			set_vconn(port, 0);
	}

	/* Clear PR swap flag after checking for Vconn */
	TC_CLR_FLAG(port, TC_FLAGS_REQUEST_PR_SWAP);
}

static __maybe_unused void check_drp_connection(const int port)
{
	enum pd_drp_next_states next_state;
	enum tcpc_cc_voltage_status cc1, cc2;

	TC_CLR_FLAG(port, TC_FLAGS_CHECK_CONNECTION);

	/* Check for connection */
	tcpm_get_cc(port, &cc1, &cc2);

	tc[port].drp_sink_time = get_time().val;

	/* Get the next toggle state */
	next_state = drp_auto_toggle_next_state(&tc[port].drp_sink_time,
		tc[port].power_role, drp_state[port], cc1, cc2,
		tcpm_auto_toggle_supported(port));

	if (next_state == DRP_TC_DEFAULT)
		next_state = (PD_ROLE_DEFAULT(port) == PD_ROLE_SOURCE)
				? DRP_TC_UNATTACHED_SRC
				: DRP_TC_UNATTACHED_SNK;

	switch (next_state) {
	case DRP_TC_UNATTACHED_SNK:
		set_state_tc(port, TC_UNATTACHED_SNK);
		break;
	case DRP_TC_ATTACHED_WAIT_SNK:
		set_state_tc(port, TC_ATTACH_WAIT_SNK);
		break;
	case DRP_TC_UNATTACHED_SRC:
		set_state_tc(port, TC_UNATTACHED_SRC);
		break;
	case DRP_TC_ATTACHED_WAIT_SRC:
		set_state_tc(port, TC_ATTACH_WAIT_SRC);
		break;

#ifdef CONFIG_USB_PD_DUAL_ROLE_AUTO_TOGGLE
	case DRP_TC_DRP_AUTO_TOGGLE:
		set_state_tc(port, TC_DRP_AUTO_TOGGLE);
		break;
#endif

	default:
		CPRINTS("C%d: Error: DRP next state %d", port, next_state);
		break;
	}
}

#ifdef CONFIG_USB_PD_DUAL_ROLE_AUTO_TOGGLE
/**
 * DrpAutoToggle
 */
static void tc_drp_auto_toggle_entry(const int port)
{
	print_current_state(port);

	/*
	 * We need to ensure that we are waiting in the previous Rd or Rp state
	 * for the minimum of DRP SNK or SRC so the first toggle cause by
	 * transition into auto toggle doesn't violate spec timing.
	 */
	tc[port].timeout = get_time().val + MAX(PD_T_DRP_SNK, PD_T_DRP_SRC);
}

static void tc_drp_auto_toggle_run(const int port)
{
	/*
	 * A timer is running, but if a connection comes in while waiting
	 * then allow that to take higher priority.
	 */
	if (TC_CHK_FLAG(port, TC_FLAGS_CHECK_CONNECTION))
		check_drp_connection(port);

	else if (tc[port].timeout != TIMER_DISABLED) {
		if (tc[port].timeout > get_time().val)
			return;

		tc[port].timeout = TIMER_DISABLED;
		tcpm_enable_drp_toggle(port);

		if (IS_ENABLED(CONFIG_USB_PD_TCPC_LOW_POWER)) {
			set_state_tc(port, TC_LOW_POWER_MODE);
		}
	}
}
#endif /* CONFIG_USB_PD_DUAL_ROLE_AUTO_TOGGLE */

#ifdef CONFIG_USB_PD_TCPC_LOW_POWER
static void tc_low_power_mode_entry(const int port)
{
	print_current_state(port);
	tc[port].low_power_time = get_time().val + PD_LPM_DEBOUNCE_US;
	tc[port].low_power_exit_time = 0;
}

static void tc_low_power_mode_run(const int port)
{
	if (TC_CHK_FLAG(port, TC_FLAGS_CHECK_CONNECTION)) {
		uint64_t now = get_time().val;

		tc_start_event_loop(port);
		if (tc[port].low_power_exit_time == 0) {
			tc[port].low_power_exit_time = now
				+ PD_LPM_EXIT_DEBOUNCE_US;
		} else if (now > tc[port].low_power_exit_time) {
			CPRINTS("C%d: Exit Low Power Mode", port);
			check_drp_connection(port);
		}
		return;
	}

	if (tc[port].tasks_preventing_lpm)
		tc[port].low_power_time = get_time().val + PD_LPM_DEBOUNCE_US;

	if (get_time().val > tc[port].low_power_time) {
		CPRINTS("C%d: TCPC Enter Low Power Mode", port);
		TC_SET_FLAG(port, TC_FLAGS_LPM_ENGAGED);
		TC_SET_FLAG(port, TC_FLAGS_LPM_TRANSITION);
		tcpm_enter_low_power_mode(port);
		TC_CLR_FLAG(port, TC_FLAGS_LPM_TRANSITION);
		tc_pause_event_loop(port);

		tc[port].low_power_exit_time = 0;
	}
}
#endif /* CONFIG_USB_PD_TCPC_LOW_POWER */


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

	/*
	 * We are a SNK but would prefer to be a SRC.  Set the pull to
	 * indicate we want to be a SRC and looking for a SNK.
	 *
	 * Both CC1 and CC2 pins shall be independently terminated to
	 * ground through Rp.
	 */
	typec_select_pull(port, TYPEC_CC_RP);
	typec_select_src_current_limit_rp(port, CONFIG_USB_PD_PULLUP);

	/* Apply Rp */
	typec_update_cc(port);
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
	if (get_time().val > tc[port].cc_debounce &&
	    new_cc_state == PD_CC_UFP_ATTACHED)
		set_state_tc(port, TC_ATTACHED_SRC);

	/*
	 * The port shall transition to TryWait.SNK after tDRPTry and the
	 * SRC.Rd state has not been detected and VBUS is within vSafe0V,
	 * or after tTryTimeout and the SRC.Rd state has not been detected.
	 */
	if (new_cc_state == PD_CC_NONE) {
		if ((get_time().val > tc[port].try_wait_debounce &&
		     pd_check_vbus_level(port, VBUS_SAFE0V)) ||
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

	/*
	 * We were a SNK, tried to be a SRC and it didn't work out. Try to
	 * go back to being a SNK.  Set the pull to indicate we want to be
	 * a SNK and looking for a SRC.
	 *
	 * Both CC1 and CC2 pins shall be independently terminated to
	 * ground through Rd.
	 */
	typec_select_pull(port, TYPEC_CC_RD);

	/* Apply Rd */
	typec_update_cc(port);
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
	typec_select_pull(port, TYPEC_CC_RD);
	typec_select_src_current_limit_rp(port, CONFIG_USB_PD_PULLUP);
	typec_update_cc(port);

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
	if (TC_CHK_FLAG(port, TC_FLAGS_HARD_RESET_REQUESTED)) {
		TC_CLR_FLAG(port, TC_FLAGS_HARD_RESET_REQUESTED);
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
		if (new_cc_state == PD_CC_NONE &&
		    pd_check_vbus_level(port, VBUS_SAFE0V)) {
			if (IS_ENABLED(CONFIG_USB_PD_ALT_MODE_DFP)) {
				pd_dfp_exit_mode(port, TCPC_TX_SOP, 0, 0);
				pd_dfp_exit_mode(port, TCPC_TX_SOP_PRIME, 0, 0);
				pd_dfp_exit_mode(port, TCPC_TX_SOP_PRIME_PRIME,
						0, 0);
			}

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
	if (TC_CHK_FLAG(port, TC_FLAGS_HARD_RESET_REQUESTED)) {
		TC_CLR_FLAG(port, TC_FLAGS_HARD_RESET_REQUESTED);
		/* Nothing to do. Just signal hard reset completion */
		pe_ps_reset_complete(port);
	}

	/*
	 * A port that is not in the process of a USB PD Hard Reset shall
	 * transition to CTUnattached.SNK within tSinkDisconnect when VBUS
	 * falls below vSinkDisconnect
	 */
	if (pd_check_vbus_level(port, VBUS_REMOVED)) {
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

	/* Set power role to sink */
	tc_set_power_role(port, PD_ROLE_SINK);
	tcpm_set_msg_header(port, tc[port].power_role, tc[port].data_role);
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
}

/**
 * Super State CC_OPEN
 */
static void tc_cc_open_entry(const int port)
{
	/* Ensure we are not sourcing Vbus */
	tc_src_power_off(port);

	/* Disable VCONN */
	set_vconn(port, 0);

	/*
	 * Ensure we disable discharging before setting CC lines to open.
	 * If we were sourcing above, then we already drained Vbus. If partner
	 * is sourcing Vbus they will drain Vbus if they are PD-capable. This
	 * should only be done if a battery is present as a batteryless
	 * device will brown out when AutoDischargeDisconnect is disabled and
	 * we do not want this to happen until the set_cc open/open to make
	 * sure the TCPC has managed its internal states for disconnecting
	 * the only source of power it has.
	 */
	if (battery_is_present())
		tcpm_enable_auto_discharge_disconnect(port, 0);

	/* We may brown out after applying CC open, so flush console first. */
	CPRINTS("C%d: Applying CC Open!", port);
	cflush();

	/* Remove terminations from CC */
	typec_select_pull(port, TYPEC_CC_OPEN);
	typec_update_cc(port);

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

void tc_set_debug_level(enum debug_level debug_level)
{
#ifndef CONFIG_USB_PD_DEBUG_LEVEL
	tc_debug_level = debug_level;
#endif
}

void tc_run(const int port)
{
	/*
	 * If pd_set_suspend SUSPEND state changes to
	 * be suspended then we need to go directly to
	 * DISABLED
	 */
	if (TC_CHK_FLAG(port, TC_FLAGS_SUSPEND)) {
		/* Invalidate a contract, if there is one */
		pe_invalidate_explicit_contract(port);

		set_state_tc(port, TC_DISABLED);
	}

	run_state(port, &tc[port].ctx);
}

static void pd_chipset_resume(void)
{
	int i;

	for (i = 0; i < CONFIG_USB_PD_PORT_MAX_COUNT; i++) {
		pd_set_dual_role_and_event(i,
					   PD_DRP_TOGGLE_ON,
					   PD_EVENT_UPDATE_DUAL_ROLE
					   | PD_EVENT_POWER_STATE_CHANGE);
	}

	CPRINTS("PD:S3->S0");
}
DECLARE_HOOK(HOOK_CHIPSET_RESUME, pd_chipset_resume, HOOK_PRIO_DEFAULT);

static void pd_chipset_suspend(void)
{
	int i;

	for (i = 0; i < CONFIG_USB_PD_PORT_MAX_COUNT; i++) {
		pd_set_dual_role_and_event(i,
					   pd_get_drp_state_in_suspend(),
					   PD_EVENT_UPDATE_DUAL_ROLE
					   | PD_EVENT_POWER_STATE_CHANGE);
	}

	CPRINTS("PD:S0->S3");
}
DECLARE_HOOK(HOOK_CHIPSET_SUSPEND, pd_chipset_suspend, HOOK_PRIO_DEFAULT);

static void pd_chipset_startup(void)
{
	int i;

	for (i = 0; i < CONFIG_USB_PD_PORT_MAX_COUNT; i++) {
		set_usb_mux_with_current_data_role(i);
		pd_set_dual_role_and_event(i,
					   pd_get_drp_state_in_suspend(),
					   PD_EVENT_UPDATE_DUAL_ROLE
					   | PD_EVENT_POWER_STATE_CHANGE);
		/*
		 * Request port discovery to restore any
		 * alt modes.
		 * TODO(b/158042116): Do not start port discovery if there
		 * is an existing connection.
		 */
		if (IS_ENABLED(CONFIG_USB_PE_SM))
			pd_dpm_request(i, DPM_REQUEST_PORT_DISCOVERY);
	}

	CPRINTS("PD:S5->S3");
}
DECLARE_HOOK(HOOK_CHIPSET_STARTUP, pd_chipset_startup, HOOK_PRIO_DEFAULT);

static void pd_chipset_shutdown(void)
{
	int i;

	for (i = 0; i < CONFIG_USB_PD_PORT_MAX_COUNT; i++) {
		pd_set_dual_role_and_event(i,
					   PD_DRP_FORCE_SINK,
					   PD_EVENT_UPDATE_DUAL_ROLE
					   | PD_EVENT_POWER_STATE_CHANGE);
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

#if defined(TEST_BUILD) && defined(USB_PD_DEBUG_LABELS)
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
