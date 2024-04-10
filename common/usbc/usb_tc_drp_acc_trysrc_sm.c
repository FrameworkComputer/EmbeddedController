/* Copyright 2019 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "builtin/assert.h"
#include "charge_manager.h"
#include "charge_state.h"
#include "common.h"
#include "console.h"
#include "gpio.h"
#include "hooks.h"
#include "system.h"
#include "task.h"
#include "tcpm/tcpm.h"
#include "typec_control.h"
#include "usb_common.h"
#include "usb_mux.h"
#include "usb_pd.h"
#include "usb_pd_dpm_sm.h"
#include "usb_pd_tcpm.h"
#include "usb_pd_timer.h"
#include "usb_pe_sm.h"
#include "usb_prl_sm.h"
#include "usb_sm.h"
#include "usb_tc_sm.h"
#include "usbc_ocp.h"
#include "usbc_ppc.h"
#include "vboot.h"

/*
 * USB Type-C DRP with Accessory and Try.SRC module
 *   See Figure 4-16 in Release 1.4 of USB Type-C Spec.
 */
#ifdef CONFIG_COMMON_RUNTIME
#define CPRINTF(format, args...) cprintf(CC_USBPD, format, ##args)
#define CPRINTS(format, args...) cprints(CC_USBPD, format, ##args)
#else /* CONFIG_COMMON_RUNTIME */
#define CPRINTF(format, args...)
#define CPRINTS(format, args...)
#endif

#define CPRINTF_LX(x, format, args...)           \
	do {                                     \
		if (tc_debug_level >= x)         \
			CPRINTF(format, ##args); \
	} while (0)
#define CPRINTF_L1(format, args...) CPRINTF_LX(1, format, ##args)
#define CPRINTF_L2(format, args...) CPRINTF_LX(2, format, ##args)
#define CPRINTF_L3(format, args...) CPRINTF_LX(3, format, ##args)

#define CPRINTS_LX(x, format, args...)           \
	do {                                     \
		if (tc_debug_level >= x)         \
			CPRINTS(format, ##args); \
	} while (0)
#define CPRINTS_L1(format, args...) CPRINTS_LX(1, format, ##args)
#define CPRINTS_L2(format, args...) CPRINTS_LX(2, format, ##args)
#define CPRINTS_L3(format, args...) CPRINTS_LX(3, format, ##args)

/*
 * Define DEBUG_PRINT_FLAG_AND_EVENT_NAMES to print flag names when set and
 * cleared, and event names when handled by tc_event_check().
 */
#undef DEBUG_PRINT_FLAG_AND_EVENT_NAMES

#ifdef DEBUG_PRINT_FLAG_AND_EVENT_NAMES
void print_flag(int port, int set_or_clear, int flag);
#define TC_SET_FLAG(port, flag)                     \
	do {                                        \
		print_flag(port, 1, flag);          \
		atomic_or(&tc[port].flags, (flag)); \
	} while (0)
#define TC_CLR_FLAG(port, flag)                             \
	do {                                                \
		print_flag(port, 0, flag);                  \
		atomic_clear_bits(&tc[port].flags, (flag)); \
	} while (0)
#else
#define TC_SET_FLAG(port, flag) atomic_or(&tc[port].flags, (flag))
#define TC_CLR_FLAG(port, flag) atomic_clear_bits(&tc[port].flags, (flag))
#endif
#define TC_CHK_FLAG(port, flag) (tc[port].flags & (flag))

/* Type-C Layer Flags */
/* Flag to note we are sourcing VCONN */
#define TC_FLAGS_VCONN_ON BIT(0)
/* Flag to note port partner has Rp/Rp or Rd/Rd */
#define TC_FLAGS_TS_DTS_PARTNER BIT(1)
/* Flag to note VBus input has never been low */
#define TC_FLAGS_VBUS_NEVER_LOW BIT(2)
/* Flag to note Low Power Mode transition is currently happening */
#define TC_FLAGS_LPM_TRANSITION BIT(3)
/* Flag to note Low Power Mode is currently on */
#define TC_FLAGS_LPM_ENGAGED BIT(4)
/* Flag to note CVTPD has been detected */
#define TC_FLAGS_CTVPD_DETECTED BIT(5)
/* Flag to note request to swap to VCONN on */
#define TC_FLAGS_REQUEST_VC_SWAP_ON BIT(6)
/* Flag to note request to swap to VCONN off */
#define TC_FLAGS_REQUEST_VC_SWAP_OFF BIT(7)
/* Flag to note request to swap VCONN is being rejected */
#define TC_FLAGS_REJECT_VCONN_SWAP BIT(8)
/* Flag to note request to power role swap */
#define TC_FLAGS_REQUEST_PR_SWAP BIT(9)
/* Flag to note request to data role swap */
#define TC_FLAGS_REQUEST_DR_SWAP BIT(10)
/* Flag to note request to power off sink */
#define TC_FLAGS_POWER_OFF_SNK BIT(11)
/* Flag to note port partner is Power Delivery capable */
#define TC_FLAGS_PARTNER_PD_CAPABLE BIT(12)
/* Flag to note hard reset has been requested */
#define TC_FLAGS_HARD_RESET_REQUESTED BIT(13)
/* Flag to note we are currently performing PR Swap */
#define TC_FLAGS_PR_SWAP_IN_PROGRESS BIT(14)
/* Flag to note we should check for connection */
#define TC_FLAGS_CHECK_CONNECTION BIT(15)
/* Flag to note request from pd_set_suspend to enter TC_DISABLED state */
#define TC_FLAGS_REQUEST_SUSPEND BIT(16)
/* Flag to note we are in TC_DISABLED state */
#define TC_FLAGS_SUSPENDED BIT(17)
/* Flag to indicate the port current limit has changed */
#define TC_FLAGS_UPDATE_CURRENT BIT(18)
/* Flag to indicate USB mux should be updated */
#define TC_FLAGS_UPDATE_USB_MUX BIT(19)
/* Flag for retimer firmware update */
#define TC_FLAGS_USB_RETIMER_FW_UPDATE_RUN BIT(20)
#define TC_FLAGS_USB_RETIMER_FW_UPDATE_LTD_RUN BIT(21)
/* Flag for asynchronous call to request Error Recovery */
#define TC_FLAGS_REQUEST_ERROR_RECOVERY BIT(22)

/* For checking flag_bit_names[] array */
#define TC_FLAGS_COUNT 23

/* On disconnect, clear most of the flags. */
#define CLR_FLAGS_ON_DISCONNECT(port)                                         \
	TC_CLR_FLAG(port, ~(TC_FLAGS_LPM_ENGAGED | TC_FLAGS_REQUEST_SUSPEND | \
			    TC_FLAGS_SUSPENDED))

/*
 * 10 ms is enough time for any TCPC transaction to complete
 *
 * This value must be below ~39.7 ms to put ANX7447 into LPM due to bug in
 * silicon (see b/77544959 and b/149761477 for more details).
 */
#define PD_LPM_DEBOUNCE_US (10 * MSEC)

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
#ifdef CONFIG_USB_PD_TCPC_LOW_POWER
#define PD_LPM_EXIT_DEBOUNCE_US CONFIG_USB_PD_TCPC_LPM_EXIT_DEBOUNCE
#else
/*
 * Define this value regardless so it is not missing at compile time.
 */
#define PD_LPM_EXIT_DEBOUNCE_US 0
#endif

/*
 * The TypeC state machine uses this bit to disable/enable PD
 * This bit corresponds to bit-0 of pd_disabled_mask
 */
#define PD_DISABLED_NO_CONNECTION BIT(0)
/*
 * Console and Host commands use this bit to override the
 * PD_DISABLED_NO_CONNECTION bit that was set by the TypeC
 * state machine.
 * This bit corresponds to bit-1 of pd_disabled_mask
 */
#define PD_DISABLED_BY_POLICY BIT(1)

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

	TC_STATE_COUNT,
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
 * If CONFIG_ASSERT_CCD_MODE_ON_DTS_CONNECT is not defined then
 * _GPIO_CCD_MODE_ODL is not needed. Declare as extern so IS_ENABLED will work.
 */
#ifndef CONFIG_ASSERT_CCD_MODE_ON_DTS_CONNECT
extern int _GPIO_CCD_MODE_ODL;
#else
#define _GPIO_CCD_MODE_ODL GPIO_CCD_MODE_ODL
#endif /* CONFIG_ASSERT_CCD_MODE_ON_DTS_CONNECT */

/*
 * We will use DEBUG LABELS if we will be able to print (COMMON RUNTIME)
 * and either CONFIG_USB_PD_DEBUG_LEVEL is not defined (no override) or
 * we are overriding and the level is not DISABLED.
 *
 * If we can't print or the CONFIG_USB_PD_DEBUG_LEVEL is defined to be 0
 * then the DEBUG LABELS will be removed from the build.
 */
#if defined(CONFIG_COMMON_RUNTIME) && (!defined(CONFIG_USB_PD_DEBUG_LEVEL) || \
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

/* List of human readable state names for console debugging */
__maybe_unused static __const_data const char *const tc_state_names[] = {
#ifdef USB_PD_DEBUG_LABELS
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
	[TC_CT_UNATTACHED_SNK] = "CTUnattached.SNK",
	[TC_CT_ATTACHED_SNK] = "CTAttached.SNK",
#endif
	/* Super States */
	[TC_CC_OPEN] = "SS:CC_OPEN",
	[TC_CC_RD] = "SS:CC_RD",
	[TC_CC_RP] = "SS:CC_RP",

	[TC_STATE_COUNT] = "",
#endif
};

/* Debug log level - higher number == more log */
#ifdef CONFIG_USB_PD_DEBUG_LEVEL
static const enum debug_level tc_debug_level = CONFIG_USB_PD_DEBUG_LEVEL;
#elif defined(CONFIG_USB_PD_INITIAL_DEBUG_LEVEL)
static enum debug_level tc_debug_level = CONFIG_USB_PD_INITIAL_DEBUG_LEVEL;
#else
static enum debug_level tc_debug_level = DEBUG_LEVEL_1;
#endif

#ifdef DEBUG_PRINT_FLAG_AND_EVENT_NAMES
struct bit_name {
	int value;
	const char *name;
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
	{ TC_FLAGS_PARTNER_PD_CAPABLE, "PARTNER_PD_CAPABLE" },
	{ TC_FLAGS_HARD_RESET_REQUESTED, "HARD_RESET_REQUESTED" },
	{ TC_FLAGS_PR_SWAP_IN_PROGRESS, "PR_SWAP_IN_PROGRESS" },
	{ TC_FLAGS_CHECK_CONNECTION, "CHECK_CONNECTION" },
	{ TC_FLAGS_REQUEST_SUSPEND, "REQUEST_SUSPEND" },
	{ TC_FLAGS_SUSPENDED, "SUSPENDED" },
	{ TC_FLAGS_UPDATE_CURRENT, "UPDATE_CURRENT" },
	{ TC_FLAGS_UPDATE_USB_MUX, "UPDATE_USB_MUX" },
	{ TC_FLAGS_USB_RETIMER_FW_UPDATE_RUN, "USB_RETIMER_FW_UPDATE_RUN" },
	{ TC_FLAGS_USB_RETIMER_FW_UPDATE_LTD_RUN,
	  "USB_RETIMER_FW_UPDATE_LTD_RUN" },
	{ TC_FLAGS_REQUEST_ERROR_RECOVERY, "REQUEST_ERROR_RECOCVERY" },
};
BUILD_ASSERT(ARRAY_SIZE(flag_bit_names) == TC_FLAGS_COUNT);

static struct bit_name event_bit_names[] = {
	{ TASK_EVENT_SYSJUMP_READY, "SYSJUMP_READY" },
	{ TASK_EVENT_IPC_READY, "IPC_READY" },
	{ TASK_EVENT_PD_AWAKE, "PD_AWAKE" },
	{ TASK_EVENT_PECI_DONE, "PECI_DONE" },
	{ TASK_EVENT_I2C_IDLE, "I2C_IDLE" },
#ifdef TASK_EVENT_PS2_DONE
	{ TASK_EVENT_PS2_DONE, "PS2_DONE" },
#endif
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

static void print_bits(int port, const char *desc, int value,
		       struct bit_name *names, int names_size)
{
	int i;

	CPRINTF("C%d: %s 0x%x : ", port, desc, value);
	for (i = 0; i < names_size; i++) {
		if (value & names[i].value)
			CPRINTF("%s | ", names[i].name);
		value &= ~names[i].value;
	}
	if (value != 0)
		CPRINTF("0x%x", value);
	CPRINTF("\n");
}

void print_flag(int port, int set_or_clear, int flag)
{
	print_bits(port, set_or_clear ? "Set" : "Clr", flag, flag_bit_names,
		   ARRAY_SIZE(flag_bit_names));
}
#endif /* DEBUG_PRINT_FLAG_AND_EVENT_NAMES */

#ifndef CONFIG_USB_PD_TRY_SRC
extern int TC_TRY_SRC_UNDEFINED;
extern int TC_TRY_WAIT_SNK_UNDEFINED;
#define TC_TRY_SRC TC_TRY_SRC_UNDEFINED
#define TC_TRY_WAIT_SNK TC_TRY_WAIT_SNK_UNDEFINED
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
	atomic_t pd_disabled_mask;
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
	atomic_t flags;
	/* The cc state */
	enum pd_cc_states cc_state;
	/* Tasks to notify after TCPC has been reset */
	atomic_t tasks_waiting_on_reset;
	/* Tasks preventing TCPC from entering low power mode */
	atomic_t tasks_preventing_lpm;
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
static volatile __maybe_unused enum pd_dual_role_states
	drp_state[CONFIG_USB_PD_PORT_MAX_COUNT] = {
		[0 ...(CONFIG_USB_PD_PORT_MAX_COUNT - 1)] =
			CONFIG_USB_PD_INITIAL_DRP_STATE
	};

static void set_vconn(int port, int enable);

/* Forward declare common, private functions */
static __maybe_unused int reset_device_and_notify(int port);
static __maybe_unused void check_drp_connection(const int port);
static void sink_power_sub_states(int port);
static void set_ccd_mode(int port, bool enable);

__maybe_unused static void handle_new_power_state(int port);

static void pd_update_dual_role_config(int port);

/* Forward declare common, private functions */
static void set_state_tc(const int port, const enum usb_tc_state new_state);
test_export_static enum usb_tc_state get_state_tc(const int port);
static bool in_ct_state(int port);

/* Enable variable for Try.SRC states */
static atomic_t pd_try_src;
static volatile enum try_src_override_t pd_try_src_override;
static void pd_update_try_source(void);

static void sink_stop_drawing_current(int port);

__maybe_unused static bool is_try_src_enabled(int port)
{
	if (!IS_ENABLED(CONFIG_USB_PD_TRY_SRC))
		assert(0);

	return ((pd_try_src_override == TRY_SRC_OVERRIDE_ON) ||
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

__overridable void pd_set_vbus_discharge(int port, int enable)
{
	/* DO NOTHING */
}

#endif /* !CONFIG_USB_PRL_SM */

#ifndef CONFIG_USB_PE_SM

/*
 * These pd_ functions are implemented in the PE layer
 */
const uint32_t *const pd_get_src_caps(int port)
{
	return NULL;
}

uint8_t pd_get_src_cap_cnt(int port)
{
	return 0;
}

const uint32_t *const pd_get_snk_caps(int port)
{
	return NULL;
}

uint8_t pd_get_snk_cap_cnt(int port)
{
	return 0;
}

void pd_set_src_caps(int port, int cnt, uint32_t *src_caps)
{
}

int pd_get_rev(int port, enum tcpci_msg_type type)
{
	return PD_REV30;
}

#endif /* !CONFIG_USB_PR_SM */

#ifndef CONFIG_AP_POWER_CONTROL
__overridable enum pd_dual_role_states board_tc_get_initial_drp_mode(int port)
{
	/*
	 * DRP state is typically adjusted as the chipset state is changed. For
	 * projects which don't include an AP this function can be used for to
	 * specify what the starting DRP state should be.
	 */
	return PD_DRP_FORCE_SINK;
}
#endif

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
			pd_timer_disable(port, TC_TIMER_VBUS_DEBOUNCE);
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

/* Flag to indicate PD comm is disabled on init */
static int pd_disabled_on_init;

static void pd_update_pd_comm(void)
{
	int i;

	/*
	 * Some batteries take much longer time to report its SOC.
	 * The init function disabled PD comm on startup. Need this
	 * hook to enable PD comm when the battery level is enough.
	 */
	if (pd_disabled_on_init && pd_is_battery_capable()) {
		for (i = 0; i < CONFIG_USB_PD_PORT_MAX_COUNT; i++)
			pd_comm_enable(i, 1);
		pd_disabled_on_init = 0;
	}
}
DECLARE_HOOK(HOOK_BATTERY_SOC_CHANGE, pd_update_pd_comm, HOOK_PRIO_DEFAULT);

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
	if (!system_is_locked()) {
		if (IS_ENABLED(CONFIG_VBOOT_EFS2))
			return true;

		if (pd_is_battery_capable())
			return true;

		pd_disabled_on_init = 1;
	}

	return false;
}

static void tc_policy_pd_enable(int port, int en)
{
	if (en)
		atomic_clear_bits(&tc[port].pd_disabled_mask,
				  PD_DISABLED_BY_POLICY);
	else
		atomic_or(&tc[port].pd_disabled_mask, PD_DISABLED_BY_POLICY);

	CPRINTS("C%d: PD comm policy %sabled", port, en ? "en" : "dis");
}

static void tc_enable_pd(int port, int en)
{
	if (en)
		atomic_clear_bits(&tc[port].pd_disabled_mask,
				  PD_DISABLED_NO_CONNECTION);
	else
		atomic_or(&tc[port].pd_disabled_mask,
			  PD_DISABLED_NO_CONNECTION);
}

__maybe_unused static void tc_enable_try_src(int en)
{
	if (!IS_ENABLED(CONFIG_USB_PD_TRY_SRC))
		assert(0);

	if (en)
		atomic_or(&pd_try_src, 1);
	else
		atomic_clear_bits(&pd_try_src, 1);
}

/*
 * Exit all modes due to a detach event or hard reset
 *
 * Note: this skips the ExitMode VDM steps in the PE because it is assumed the
 * partner is not present to receive them, and the PE will no longer be running,
 * or we've forced an abrupt mode exit through a hard reset.
 */
static void tc_set_modes_exit(int port)
{
	if (IS_ENABLED(CONFIG_USB_PE_SM) &&
	    IS_ENABLED(CONFIG_USB_PD_ALT_MODE_DFP)) {
		pd_dfp_exit_mode(port, TCPCI_MSG_SOP, 0, 0);
		pd_dfp_exit_mode(port, TCPCI_MSG_SOP_PRIME, 0, 0);
		pd_dfp_exit_mode(port, TCPCI_MSG_SOP_PRIME_PRIME, 0, 0);
	}
}

static void tc_detached(int port)
{
	TC_CLR_FLAG(port, TC_FLAGS_TS_DTS_PARTNER);
	hook_notify(HOOK_USB_PD_DISCONNECT);
	tc_enable_pd(port, 0);
	tc_pd_connection(port, 0);
	tcpm_debug_accessory(port, 0);
	set_ccd_mode(port, 0);
	tc_set_modes_exit(port);
	if (IS_ENABLED(CONFIG_USB_PRL_SM))
		prl_set_default_pd_revision(port);

	/* Clear any mux connection on detach */
	if (IS_ENABLED(CONFIG_USBC_SS_MUX))
		usb_mux_set(port, USB_PD_MUX_NONE, USB_SWITCH_DISCONNECT,
			    tc[port].polarity);
}

static inline void pd_set_dual_role_and_event(int port,
					      enum pd_dual_role_states state,
					      uint32_t event)
{
	drp_state[port] = state;

	if (IS_ENABLED(CONFIG_USB_PD_TRY_SRC))
		pd_update_try_source();

	if (event != 0)
		task_set_event(PD_PORT_TO_TASK_ID(port), event);
}

void pd_set_dual_role(int port, enum pd_dual_role_states state)
{
	pd_set_dual_role_and_event(port, state, PD_EVENT_UPDATE_DUAL_ROLE);
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

/* Return true if partner port is known to be PD capable. */
bool pd_capable(int port)
{
	return !!TC_CHK_FLAG(port, TC_FLAGS_PARTNER_PD_CAPABLE);
}

enum pd_dual_role_states pd_get_dual_role(int port)
{
	return drp_state[port];
}

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

test_mockable int tc_is_attached_src(int port)
{
	return IS_ATTACHED_SRC(port);
}

int tc_is_attached_snk(int port)
{
	return IS_ATTACHED_SNK(port);
}

__overridable void tc_update_pd_sleep_mask(int port)
{
}

void tc_pd_connection(int port, int en)
{
	if (en) {
		bool new_pd_capable = false;

		if (!TC_CHK_FLAG(port, TC_FLAGS_PARTNER_PD_CAPABLE) &&
		    !in_ct_state(port))
			new_pd_capable = true;

		TC_SET_FLAG(port, TC_FLAGS_PARTNER_PD_CAPABLE);
		/* If a PD device is attached then disable deep sleep */
		if (IS_ENABLED(CONFIG_LOW_POWER_IDLE) &&
		    IS_ENABLED(CONFIG_USB_PD_TCPC_ON_CHIP))
			tc_update_pd_sleep_mask(port);
		else if (IS_ENABLED(CONFIG_LOW_POWER_IDLE))
			disable_sleep(SLEEP_MASK_USB_PD);

		/*
		 * Update the mux state, only when the PD capable flag
		 * transitions from 0 to 1. This ensures that PD charger
		 * devices, without data capability are not marked as having
		 * USB.
		 */
		if (new_pd_capable)
			set_usb_mux_with_current_data_role(port);
	} else {
		TC_CLR_FLAG(port, TC_FLAGS_PARTNER_PD_CAPABLE);
		/* If a PD device isn't attached then enable deep sleep */
		if (IS_ENABLED(CONFIG_LOW_POWER_IDLE) &&
		    IS_ENABLED(CONFIG_USB_PD_TCPC_ON_CHIP))
			tc_update_pd_sleep_mask(port);
		else if (IS_ENABLED(CONFIG_LOW_POWER_IDLE)) {
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
	if (IS_ENABLED(CONFIG_USBC_VCONN)) {
		if (TC_CHK_FLAG(port, TC_FLAGS_REJECT_VCONN_SWAP))
			return 0;

		return pd_check_vconn_swap(port);
	} else
		return 0;
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
		pd_timer_enable(port, TC_TIMER_VBUS_DEBOUNCE, PD_T_DEBOUNCE);
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

void tc_try_src_override(enum try_src_override_t ov)
{
	if (!IS_ENABLED(CONFIG_USB_PD_TRY_SRC))
		assert(0);

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
	if (!IS_ENABLED(CONFIG_USB_PD_TRY_SRC))
		assert(0);

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
	/*
	 * Check our OC event counter.  If we've exceeded our threshold, then
	 * let's latch our source path off to prevent continuous cycling.  When
	 * the PD state machine detects a disconnection on the CC lines, we will
	 * reset our OC event counter.
	 */
	if (IS_ENABLED(CONFIG_USBC_OCP) && usbc_ocp_is_port_latched_off(port))
		return EC_ERROR_ACCESS_DENIED;

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

enum ocp_action {
	OCP_CLEAR,
	OCP_NO_ACTION,
};

/* Set what role the partner is right now, for the PPC and OCP module */
static void tc_set_partner_role(int port, enum ppc_device_role role,
				enum ocp_action ocp_command)
{
	if (IS_ENABLED(CONFIG_USBC_PPC))
		ppc_dev_is_connected(port, role);

	if (IS_ENABLED(CONFIG_USBC_OCP)) {
		usbc_ocp_snk_is_connected(port, role == PPC_DEV_SNK);
		/*
		 * Clear the overcurrent event counter
		 * if we're not in ErrorRecovery due to OCP
		 */
		if (ocp_command == OCP_CLEAR)
			usbc_ocp_clear_event_counter(port);
	}
}

/*
 * Depending on the load on the processor and the tasks running
 * it can take a while for the task associated with this port
 * to run.  So build in 1ms delays, for up to 300ms, to wait for
 * the suspend to actually happen.
 */
#define SUSPEND_SLEEP_DELAY 1
#define SUSPEND_SLEEP_RETRIES 300

void pd_set_suspend(int port, int suspend)
{
	if (pd_is_port_enabled(port) == !suspend)
		return;

	/* Track if we are suspended or not */
	if (suspend) {
		int wait = 0;

		TC_SET_FLAG(port, TC_FLAGS_REQUEST_SUSPEND);

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
				CPRINTS("C%d: NOT SUSPENDED after %dms", port,
					wait * SUSPEND_SLEEP_DELAY);
				return;
			}
			crec_msleep(SUSPEND_SLEEP_DELAY);
		}
	} else {
		TC_CLR_FLAG(port, TC_FLAGS_REQUEST_SUSPEND);
		task_wake(PD_PORT_TO_TASK_ID(port));
	}
}

void pd_set_error_recovery(int port)
{
	TC_SET_FLAG(port, TC_FLAGS_REQUEST_ERROR_RECOVERY);
}

int pd_is_port_enabled(int port)
{
	/*
	 * Checking get_state_tc(port) from another task isn't safe since it
	 * can return TC_DISABLED before tc_cc_open_entry and tc_disabled_entry
	 * are complete. So check TC_FLAGS_SUSPENDED instead.
	 */
	return !TC_CHK_FLAG(port, TC_FLAGS_SUSPENDED);
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
		(IS_ENABLED(CONFIG_USB_PE_SM) &&
		 ((get_state_tc(port) == TC_CT_UNATTACHED_SNK) ||
		  (get_state_tc(port) == TC_CT_ATTACHED_SNK))) ||
		IS_ATTACHED_SNK(port));
}

bool pd_is_disconnected(int port)
{
	return !pd_is_connected(port);
}

/*
 * PD functions which query our fixed PDO flags.  Both the source and sink
 * capabilities can present these values, and they should match between the two
 * for compliant partners.
 */
static bool pd_check_fixed_flag(int port, uint32_t flag)
{
	uint32_t fixed_pdo;

	if (pd_get_src_cap_cnt(port) != 0)
		fixed_pdo = *pd_get_src_caps(port);
	else if (pd_get_snk_cap_cnt(port) != 0)
		fixed_pdo = *pd_get_snk_caps(port);
	else
		return false;

	/*
	 * Error check that first PDO is fixed, as 6.4.1 Capabilities requires
	 * in the Power Delivery Specification.
	 * "The vSafe5V Fixed Supply Object Shall always be the first object"
	 */
	if ((fixed_pdo & PDO_TYPE_MASK) != PDO_TYPE_FIXED)
		return false;

	return fixed_pdo & flag;
}

bool pd_get_partner_data_swap_capable(int port)
{
	return pd_check_fixed_flag(port, PDO_FIXED_DATA_SWAP);
}

bool pd_get_partner_usb_comm_capable(int port)
{
	return pd_check_fixed_flag(port, PDO_FIXED_COMM_CAP);
}

bool pd_get_partner_dual_role_power(int port)
{
	return pd_check_fixed_flag(port, PDO_FIXED_DUAL_ROLE);
}

bool pd_get_partner_unconstr_power(int port)
{
	return pd_check_fixed_flag(port, PDO_FIXED_UNCONSTRAINED);
}

static void bc12_role_change_handler(int port, enum pd_data_role prev_data_role,
				     enum pd_data_role data_role)
{
	int event = 0;
	bool role_changed = (data_role != prev_data_role);

	if (!IS_ENABLED(CONFIG_BC12_DETECT_DATA_ROLE_TRIGGER))
		return;

	/* Get the data role of our device */
	switch (data_role) {
	case PD_ROLE_UFP:
		/* Only trigger BC12 detection on a role change */
		if (role_changed)
			event = USB_CHG_EVENT_DR_UFP;
		break;
	case PD_ROLE_DFP:
		/* Only trigger BC12 host mode on a role change */
		if (role_changed)
			event = USB_CHG_EVENT_DR_DFP;
		break;
	case PD_ROLE_DISCONNECTED:
		event = USB_CHG_EVENT_CC_OPEN;
		break;
	default:
		return;
	}

	if (event)
		usb_charger_task_set_event(port, event);
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
	if (IS_ATTACHED_SRC(port))
		TC_SET_FLAG(port, TC_FLAGS_UPDATE_CURRENT);
}
__overridable int typec_get_default_current_limit_rp(int port)
{
	int rp = CONFIG_USB_PD_PULLUP;

	if (pd_get_bist_share_mode())
		rp = TYPEC_RP_3A0;

	return rp;
}
void typec_select_src_collision_rp(int port, enum tcpc_rp_value rp)
{
	tc[port].select_collision_rp = rp;
}
static enum tcpc_rp_value typec_get_active_select_rp(int port)
{
	/* Explicit contract will use the collision Rp */
	if (IS_ENABLED(CONFIG_USB_PD_REV30) && pe_is_explicit_contract(port))
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

		/*
		 * USB PD Rev 3.0 Ver 2.0 6.8.3.2: "A Hard Reset Shall cause
		 * all Active Modes to be exited by both Port Partners and any
		 * Cable Plugs"
		 */
		tc_set_modes_exit(port);

		tc[port].ps_reset_state = PS_STATE1;
		pd_timer_enable(port, TC_TIMER_TIMEOUT, PD_T_SRC_RECOVER);
		return false;
	case PS_STATE1:
		/* Enable VBUS */
		tc_src_power_on(port);

		/* Update the Rp Value */
		typec_update_cc(port);

		/* Turn off VCONN */
		set_vconn(port, 1);

		tc[port].ps_reset_state = PS_STATE2;
		pd_timer_enable(port, TC_TIMER_TIMEOUT,
				PD_POWER_SUPPLY_TURN_ON_DELAY);
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
		/* Hard reset sets us back to default data role */
		tc_set_data_role(port, PD_ROLE_UFP);

		/*
		 * USB PD Rev 3.0 Ver 2.0 6.8.3.2: "A Hard Reset Shall cause
		 * all Active Modes to be exited by both Port Partners and any
		 * Cable Plugs"
		 */
		tc_set_modes_exit(port);

		/*
		 * When VCONN is supported, the Hard Reset Shall cause
		 * the Port with the Rd resistor asserted to turn off
		 * VCONN.
		 */
		if (IS_ENABLED(CONFIG_USBC_VCONN) &&
		    TC_CHK_FLAG(port, TC_FLAGS_VCONN_ON))
			set_vconn(port, 0);

		/* Wait up to tVSafe0V for Vbus to disappear */
		tc[port].ps_reset_state = PS_STATE1;
		pd_timer_enable(port, TC_TIMER_TIMEOUT, PD_T_SAFE_0V);
		return false;
	case PS_STATE1:
		if (pd_check_vbus_level(port, VBUS_SAFE0V)) {
			/*
			 * Partner dropped Vbus, reduce our current consumption
			 * and await its return.
			 */
			sink_stop_drawing_current(port);

			tcpm_enable_auto_discharge_disconnect(port, 0);

			/* Move on to waiting for the return of Vbus */
			tc[port].ps_reset_state = PS_STATE2;
			pd_timer_enable(port, TC_TIMER_TIMEOUT,
					PD_T_SRC_RECOVER_MAX +
						PD_T_SRC_TURN_ON);
		}

		if (pd_timer_is_expired(port, TC_TIMER_TIMEOUT)) {
			/*
			 * No Vbus drop likely indicates a non-PD port partner,
			 * move to the next stage anyway.
			 */
			tc[port].ps_reset_state = PS_STATE2;
			pd_timer_enable(port, TC_TIMER_TIMEOUT,
					PD_T_SRC_RECOVER_MAX +
						PD_T_SRC_TURN_ON);
		}
		return false;
	case PS_STATE2:
		/*
		 * Look for the voltage to be above disconnect.  Since we didn't
		 * drop our draw on non-PD partners, they may have dipped below
		 * vSafe5V but still be in a valid connected voltage.
		 */
		if (!pd_check_vbus_level(port, VBUS_REMOVED)) {
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
			pd_timer_enable(port, TC_TIMER_CC_DEBOUNCE, 0);
			sink_power_sub_states(port);

			/* Power is back, Enable AutoDischargeDisconnect */
			tcpm_enable_auto_discharge_disconnect(port, 1);
			return true;
		}
		/*
		 * If Vbus isn't back after wait + tSrcTurnOn, go unattached
		 */
		if (pd_timer_is_expired(port, TC_TIMER_TIMEOUT)) {
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
	typec_select_src_current_limit_rp(
		port, typec_get_default_current_limit_rp(port));

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
		/*
		 * Only initialize PD supplier current limit to 0.
		 * Defer initializing type-C supplier current limit
		 * to Unattached.SNK or Attached.SNK.
		 */
		pd_set_input_current_limit(port, 0, 0);
		charge_manager_update_dualrole(port, CAP_UNKNOWN);
	}

	/*
	 * PD r3.0 v2.0, ss6.2.1.1.5:
	 * After a physical or logical (USB Type-C Error Recovery) Attach, a
	 * Port discovers the common Specification Revision level between itself
	 * and its Port Partner and/or the Cable Plug(s), and uses this
	 * Specification Revision level until a Detach, Hard Reset or Error
	 * Recovery happens.
	 *
	 * This covers the Error Recovery case, because TC_ERROR_RECOVERY
	 * reinitializes the TC state machine. This also covers the implicit
	 * case when PD is suspended and resumed or when the state machine is
	 * first initialized.
	 */
	if (IS_ENABLED(CONFIG_USB_PRL_SM))
		prl_set_default_pd_revision(port);

#ifdef CONFIG_USB_PE_SM
	tc_enable_pd(port, 0);
	tc[port].ps_reset_state = PS_STATE0;
#endif
}

void tc_state_init(int port)
{
	enum usb_tc_state first_state;

	if (port >= CONFIG_USB_PD_PORT_MAX_COUNT)
		return;

	/* For test builds, replicate static initialization */
	if (IS_ENABLED(TEST_BUILD)) {
		memset(&tc[port], 0, sizeof(tc[port]));
		drp_state[port] = CONFIG_USB_PD_INITIAL_DRP_STATE;
	}

	/* If port is not available, there is nothing to initialize */
	if (port >= board_get_usb_pd_port_count()) {
		tc_enable_pd(port, 0);
		TC_SET_FLAG(port, TC_FLAGS_REQUEST_SUSPEND);
		return;
	}

	/* Allow system to set try src enable */
	if (IS_ENABLED(CONFIG_USB_PD_TRY_SRC))
		tc_try_src_override(TRY_SRC_NO_OVERRIDE);

	/*
	 * Set initial PD communication policy.
	 */
	tc_policy_pd_enable(port, pd_comm_allowed_by_policy());

#ifdef CONFIG_AP_POWER_CONTROL
	/* Set dual-role state based on chipset power state */
	if (chipset_in_state(CHIPSET_STATE_ANY_OFF))
		pd_set_dual_role_and_event(port, PD_DRP_FORCE_SINK, 0);
	else if (chipset_in_state(CHIPSET_STATE_ANY_SUSPEND))
		pd_set_dual_role_and_event(port, pd_get_drp_state_in_suspend(),
					   0);
	else /* CHIPSET_STATE_ON */
		pd_set_dual_role_and_event(port, pd_get_drp_state_in_s0(), 0);
#else
	pd_set_dual_role_and_event(port, board_tc_get_initial_drp_mode(port),
				   0);
#endif

	/*
	 * We are going to apply CC open (start with ErrorRecovery state)
	 * unless there is something which forbids us to do that (one of
	 * conditions below is true)
	 */
	first_state = TC_ERROR_RECOVERY;

	/*
	 * If we just lost power, don't apply CC open. Otherwise we would boot
	 * loop, and if this is a fresh power on, then we know there isn't any
	 * stale PD state as well.
	 */
	if (system_get_reset_flags() &
	    (EC_RESET_FLAG_BROWNOUT | EC_RESET_FLAG_POWER_ON)) {
		first_state = TC_UNATTACHED_SNK;
	}

	/*
	 * If this is non-EFS2 device, battery is not present or at some minimum
	 * voltage and EC RO doesn't keep power-on reset flag after reset caused
	 * by H1, then don't apply CC open because it will cause brown out.
	 *
	 * Please note that we are checking if CONFIG_BOARD_RESET_AFTER_POWER_ON
	 * is defined now, but actually we need to know if it was enabled in
	 * EC RO! It was assumed that if CONFIG_BOARD_RESET_AFTER_POWER_ON is
	 * defined now it was defined in EC RO too.
	 */
	if (!IS_ENABLED(CONFIG_BOARD_RESET_AFTER_POWER_ON) &&
	    !IS_ENABLED(CONFIG_VBOOT_EFS2) && IS_ENABLED(CONFIG_BATTERY) &&
	    !pd_is_battery_capable()) {
		first_state = TC_UNATTACHED_SNK;
	}

	if (first_state == TC_UNATTACHED_SNK) {
		/* Turn off any previous sourcing */
		tc_src_power_off(port);
		set_vconn(port, 0);
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

/* Set GPIO_CCD_MODE_ODL gpio */
static void set_ccd_mode(const int port, const bool enable)
{
	if (IS_ENABLED(CONFIG_ASSERT_CCD_MODE_ON_DTS_CONNECT) &&
	    port == CONFIG_CCD_USBC_PORT_NUMBER) {
		if (enable)
			CPRINTS("Asserting GPIO_CCD_MODE_ODL");
		gpio_set_level(_GPIO_CCD_MODE_ODL, !enable);
	}
}

/* Set the TypeC state machine to a new state. */
static void set_state_tc(const int port, const enum usb_tc_state new_state)
{
	assert(port == TASK_ID_TO_PD_PORT(task_get_current()));

	set_state(port, &tc[port].ctx, &tc_states[new_state]);
}

/* Get the current TypeC state. */
test_export_static enum usb_tc_state get_state_tc(const int port)
{
	/* Default to returning TC_STATE_COUNT if no state has been set */
	if (tc[port].ctx.current == NULL)
		return TC_STATE_COUNT;
	else
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
		CPRINTS_L1("C%d: tc-st%d", port, get_state_tc(port));
}

static void handle_device_access(int port)
{
	if (IS_ENABLED(CONFIG_USB_PD_TCPC_LOW_POWER) &&
	    get_state_tc(port) == TC_LOW_POWER_MODE) {
		tc_start_event_loop(port);
		pd_timer_enable(port, TC_TIMER_LOW_POWER_TIME,
				PD_LPM_DEBOUNCE_US);
	}
}

static bool in_ct_state(int port)
{
	return IS_ENABLED(CONFIG_USB_PE_SM) &&
	       ((get_state_tc(port) == TC_CT_UNATTACHED_SNK) ||
		(get_state_tc(port) == TC_CT_ATTACHED_SNK));
}

void tc_event_check(int port, int evt)
{
#ifdef DEBUG_PRINT_FLAG_AND_EVENT_NAMES
	if (evt != TASK_EVENT_TIMER)
		print_bits(port, "Event", evt, event_bit_names,
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

	if (evt & PD_EVENT_SEND_HARD_RESET) {
		/* Pass Hard Reset request to PE layer if available */
		if (IS_ENABLED(CONFIG_USB_PE_SM) && tc_get_pd_enabled(port))
			pd_dpm_request(port, DPM_REQUEST_HARD_RESET_SEND);
	}

	if (IS_ENABLED(CONFIG_AP_POWER_CONTROL)) {
		if (evt & PD_EVENT_POWER_STATE_CHANGE)
			handle_new_power_state(port);
	}

	if (IS_ENABLED(CONFIG_USB_PD_ALT_MODE_DFP)) {
		int i;

		/*
		 * Notify all ports of sysjump
		 */
		if (evt & PD_EVENT_SYSJUMP) {
			for (i = 0; i < CONFIG_USB_PD_PORT_MAX_COUNT; i++)
				dpm_set_mode_exit_request(i);
			notify_sysjump_ready();
		}
	}

	if (evt & PD_EVENT_UPDATE_DUAL_ROLE) {
		/* If TCPC is idle, start the wake process */
		if (IS_ENABLED(CONFIG_USB_PD_TCPC_LOW_POWER) &&
		    get_state_tc(port) == TC_LOW_POWER_MODE)
			tcpm_wake_low_power_mode(port);

		pd_update_dual_role_config(port);
	}
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
	enum pd_data_role prev_data_role;

	prev_data_role = tc[port].data_role;
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
	bc12_role_change_handler(port, prev_data_role, tc[port].data_role);

	/* Notify TCPC of role update */
	tcpm_set_msg_header(port, tc[port].power_role, tc[port].data_role);
}

static void sink_stop_drawing_current(int port)
{
	pd_set_input_current_limit(port, 0, 0);

	if (IS_ENABLED(CONFIG_CHARGE_MANAGER)) {
		typec_set_input_current_limit(port, 0, 0);
		charge_manager_set_ceil(port, CEIL_REQUESTOR_PD,
					CHARGE_CEIL_NONE);
	}
}

static void pd_update_try_source(void)
{
#ifdef CONFIG_USB_PD_TRY_SRC
	tc_enable_try_src(pd_is_try_source_capable());
#endif
}
DECLARE_HOOK(HOOK_BATTERY_SOC_CHANGE, pd_update_try_source, HOOK_PRIO_DEFAULT);

static void set_vconn(int port, int enable)
{
	if (enable)
		TC_SET_FLAG(port, TC_FLAGS_VCONN_ON);
	else
		TC_CLR_FLAG(port, TC_FLAGS_VCONN_ON);

	typec_set_vconn(port, enable);
}

/* This must only be called from the PD task */
static void pd_update_dual_role_config(int port)
{
	if (tc[port].power_role == PD_ROLE_SOURCE &&
	    (drp_state[port] == PD_DRP_FORCE_SINK ||
	     (drp_state[port] == PD_DRP_TOGGLE_OFF &&
	      get_state_tc(port) == TC_UNATTACHED_SRC))) {
		/*
		 * Change to sink if port is currently a source AND (new DRP
		 * state is force sink OR new DRP state is toggle off and we are
		 * in the source disconnected state).
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

__maybe_unused static void handle_new_power_state(int port)
{
	if (!IS_ENABLED(CONFIG_AP_POWER_CONTROL))
		assert(0);

	if (IS_ENABLED(CONFIG_AP_POWER_CONTROL) &&
	    IS_ENABLED(CONFIG_USB_PE_SM)) {
		if (chipset_in_or_transitioning_to_state(
			    CHIPSET_STATE_ANY_OFF)) {
			/*
			 * The SoC will negotiate alternate mode again when it
			 * boots up
			 */
			dpm_set_mode_exit_request(port);
		}
	}

	/*
	 * If the sink port was sourcing Vconn, and can no longer, request a
	 * hard reset on this port to restore Vconn to the source.  If we do not
	 * have sufficient battery to withstand Vbus loss, then continue with
	 * the inconsistent Vconn state in order to keep the board powered.
	 */
	if (IS_ENABLED(CONFIG_USB_PE_SM)) {
		if (tc_is_vconn_src(port) && tc_is_attached_snk(port) &&
		    !pd_check_vconn_swap(port) && pd_is_battery_capable())
			pd_dpm_request(port, DPM_REQUEST_HARD_RESET_SEND);
	}

	/*
	 * TC_FLAGS_UPDATE_USB_MUX is set on chipset startup and shutdown.
	 * Set the USB mux according to the new power state.  If the chipset
	 * is transitioning to OFF, this disconnects USB and DP mux.
	 *
	 * Transitions to and from suspend states do not change the USB mux
	 * or the alternate mode configuration.
	 */
	if (TC_CHK_FLAG(port, TC_FLAGS_UPDATE_USB_MUX)) {
		TC_CLR_FLAG(port, TC_FLAGS_UPDATE_USB_MUX);
		set_usb_mux_with_current_data_role(port);
	}
}

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

	CPRINTS("C%d: TCPC init %s", port, rv ? "failed!" : "ready");

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
	atomic_clear_bits(task_get_event_bitmap(task_get_current()),
			  PD_EVENT_TCPC_RESET);

	waiting_tasks = atomic_clear(&tc[port].tasks_waiting_on_reset);

	/* Wake up all waiting tasks. */
	while (waiting_tasks) {
		task = __fls(waiting_tasks);
		waiting_tasks &= ~BIT(task);
		task_set_event(task, TASK_EVENT_PD_AWAKE);
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
		task_set_event(PD_PORT_TO_TASK_ID(port), PD_EVENT_TCPC_RESET);
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
			       PD_EVENT_DEVICE_ACCESSED);
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
		atomic_clear_bits(&tc[port].tasks_preventing_lpm,
				  current_task_mask);
}
#endif /* CONFIG_USB_PD_TCPC_LOW_POWER */

static void sink_power_sub_states(int port)
{
	enum tcpc_cc_voltage_status cc1, cc2, cc;
	enum tcpc_cc_voltage_status new_cc_voltage;

	tcpm_get_cc(port, &cc1, &cc2);

	cc = polarity_rm_dts(tc[port].polarity) ? cc2 : cc1;

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
		pd_timer_enable(port, TC_TIMER_CC_DEBOUNCE,
				PD_T_RP_VALUE_CHANGE);
		return;
	}

	if (!pd_timer_is_disabled(port, TC_TIMER_CC_DEBOUNCE)) {
		if (!pd_timer_is_expired(port, TC_TIMER_CC_DEBOUNCE))
			return;

		pd_timer_disable(port, TC_TIMER_CC_DEBOUNCE);

		if (IS_ENABLED(CONFIG_CHARGE_MANAGER)) {
			tc[port].typec_curr = usb_get_typec_current_limit(
				tc[port].polarity, cc1, cc2);

			typec_set_input_current_limit(port, tc[port].typec_curr,
						      TYPE_C_VOLTAGE);
			charge_manager_update_dualrole(port, CAP_DEDICATED);
		}
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
 *   Set VBUS and VCONN off
 */
static void tc_disabled_entry(const int port)
{
	print_current_state(port);
	/*
	 * We have completed tc_cc_open_entry (our super state), so set flag
	 * to indicate to pd_is_port_enabled that we are now suspended.
	 */
	TC_SET_FLAG(port, TC_FLAGS_SUSPENDED);
	tcpm_release(port);
}

static void tc_disabled_run(const int port)
{
	/* If pd_set_suspend clears the request, go to TC_UNATTACHED_SNK/SRC. */
	if (!TC_CHK_FLAG(port, TC_FLAGS_REQUEST_SUSPEND)) {
		set_state_tc(port, drp_state[port] == PD_DRP_FORCE_SOURCE ?
					   TC_UNATTACHED_SRC :
					   TC_UNATTACHED_SNK);
	} else {
		if (IS_ENABLED(CONFIG_USBC_RETIMER_FW_UPDATE)) {
			if (TC_CHK_FLAG(
				    port,
				    TC_FLAGS_USB_RETIMER_FW_UPDATE_LTD_RUN)) {
				TC_CLR_FLAG(
					port,
					TC_FLAGS_USB_RETIMER_FW_UPDATE_LTD_RUN);
				usb_retimer_fw_update_process_op_cb(port);
			}
		}
		tc_pause_event_loop(port);
	}
}

static void tc_disabled_exit(const int port)
{
	int rv;

	tc_start_event_loop(port);
	TC_CLR_FLAG(port, TC_FLAGS_SUSPENDED);

	rv = tcpm_init(port);
	CPRINTS("C%d: TCPC init %s", port, rv ? "failed!" : "ready");
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

	pd_timer_enable(port, TC_TIMER_TIMEOUT, PD_T_ERROR_RECOVERY);

	TC_CLR_FLAG(port, TC_FLAGS_REQUEST_ERROR_RECOVERY);
}

static void tc_error_recovery_run(const int port)
{
	enum usb_tc_state start_state;

	if (!pd_timer_is_expired(port, TC_TIMER_TIMEOUT))
		return;

	/*
	 * If we transitioned to error recovery as the first state and we
	 * didn't brown out, we don't need to reinitialized the tc statemachine
	 * because we just did that. So transition to the state directly.
	 */
	if (tc[port].ctx.previous == NULL) {
		set_state_tc(port, drp_state[port] == PD_DRP_FORCE_SOURCE ?
					   TC_UNATTACHED_SRC :
					   TC_UNATTACHED_SNK);
		return;
	}

	/*
	 * If try src support is active (e.g. in S0). Then try to become the
	 * SRC, otherwise we should try to be the sink.
	 */
	start_state = TC_UNATTACHED_SNK;
	if (IS_ENABLED(CONFIG_USB_PD_TRY_SRC))
		if (is_try_src_enabled(port) ||
		    drp_state[port] == PD_DRP_FORCE_SOURCE)
			start_state = TC_UNATTACHED_SRC;

	restart_tc_sm(port, start_state);
}

static void tc_error_recovery_exit(const int port)
{
	pd_timer_disable(port, TC_TIMER_TIMEOUT);
}

/**
 * Unattached.SNK
 */
static void tc_unattached_snk_entry(const int port)
{
	enum pd_data_role prev_data_role;

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
	 *
	 * Restore default current limit Rp in case we swap to source
	 *
	 * Run any debug detaches needed before setting CC, as some TCPCs may
	 * require we set CC Open before changing power roles with a debug
	 * accessory.
	 */
	tcpm_debug_detach(port);
	typec_select_pull(port, TYPEC_CC_RD);
	typec_select_src_current_limit_rp(
		port, typec_get_default_current_limit_rp(port));
	typec_update_cc(port);

	prev_data_role = tc[port].data_role;
	tc[port].data_role = PD_ROLE_DISCONNECTED;
	/*
	 * When data role set events are used to enable BC1.2, then CC
	 * detach events are used to notify BC1.2 that it can be powered
	 * down.
	 */
	bc12_role_change_handler(port, prev_data_role, tc[port].data_role);

	if (IS_ENABLED(CONFIG_CHARGE_MANAGER))
		charge_manager_update_dualrole(port, CAP_UNKNOWN);

	tc_set_partner_role(port, PPC_DEV_DISCONNECTED, OCP_CLEAR);

	/*
	 * Indicate that the port is disconnected so the board
	 * can restore state from any previous data swap.
	 */
	pd_execute_data_swap(port, PD_ROLE_DISCONNECTED);
	pd_timer_enable(port, TC_TIMER_NEXT_ROLE_SWAP, PD_T_DRP_SNK);

#ifdef CONFIG_USB_PE_SM
	CLR_FLAGS_ON_DISCONNECT(port);
	tc[port].ps_reset_state = PS_STATE0;
#endif
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
		return;
	}

	/*
	 * Debounce the CC open status. Some TCPC needs time to get the CC
	 * status valid. Before that, CC open is reported by default. Wait
	 * to make sure the CC is really open. Reuse the role toggle timer.
	 */
	if (!pd_timer_is_expired(port, TC_TIMER_NEXT_ROLE_SWAP))
		return;

	/*
	 * Initialize type-C supplier current limits to 0. The charge
	 * manage is now seeded if it was not.
	 */
	if (IS_ENABLED(CONFIG_CHARGE_MANAGER))
		typec_set_input_current_limit(port, 0, 0);

	/*
	 * Attempt TCPC auto DRP toggle if it is
	 * not already auto toggling.
	 */
	if (IS_ENABLED(CONFIG_USB_PD_DUAL_ROLE_AUTO_TOGGLE) &&
	    drp_state[port] == PD_DRP_TOGGLE_ON &&
	    tcpm_auto_toggle_supported(port)) {
		set_state_tc(port, TC_DRP_AUTO_TOGGLE);
	} else if (drp_state[port] == PD_DRP_TOGGLE_ON) {
		/* DRP Toggle. The timer was checked above. */
		set_state_tc(port, TC_UNATTACHED_SRC);
	} else if (IS_ENABLED(CONFIG_USB_PD_TCPC_LOW_POWER) &&
		   (drp_state[port] == PD_DRP_FORCE_SINK ||
		    drp_state[port] == PD_DRP_TOGGLE_OFF)) {
		set_state_tc(port, TC_LOW_POWER_MODE);
	}
}

static void tc_unattached_snk_exit(const int port)
{
	pd_timer_disable(port, TC_TIMER_NEXT_ROLE_SWAP);
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

	if (cc_is_rp(cc1) && cc_is_rp(cc2) && board_is_dts_port(port))
		new_cc_state = PD_CC_DFP_DEBUG_ACC;
	else if (cc_is_rp(cc1) || cc_is_rp(cc2))
		new_cc_state = PD_CC_DFP_ATTACHED;
	else
		new_cc_state = PD_CC_NONE;

	/* Debounce the cc state */
	if (new_cc_state != tc[port].cc_state) {
		pd_timer_enable(port, TC_TIMER_CC_DEBOUNCE, PD_T_CC_DEBOUNCE);
		pd_timer_enable(port, TC_TIMER_PD_DEBOUNCE, PD_T_PD_DEBOUNCE);
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
	    pd_timer_is_expired(port, TC_TIMER_PD_DEBOUNCE)) {
		/* We are detached */
		if (drp_state[port] == PD_DRP_TOGGLE_OFF ||
		    drp_state[port] == PD_DRP_FREEZE ||
		    drp_state[port] == PD_DRP_FORCE_SINK)
			set_state_tc(port, TC_UNATTACHED_SNK);
		else
			set_state_tc(port, TC_UNATTACHED_SRC);
		return;
	}

	/* Wait for CC debounce */
	if (!pd_timer_is_expired(port, TC_TIMER_CC_DEBOUNCE))
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
			if (IS_ENABLED(CONFIG_USB_PD_TRY_SRC) &&
			    is_try_src_enabled(port))
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

static void tc_attach_wait_snk_exit(const int port)
{
	pd_timer_disable(port, TC_TIMER_CC_DEBOUNCE);
	pd_timer_disable(port, TC_TIMER_PD_DEBOUNCE);
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

	/* Inform the PPC and OCP module that a source is connected */
	tc_set_partner_role(port, PPC_DEV_SRC, OCP_NO_ACTION);

	if (IS_ENABLED(CONFIG_USB_PE_SM) &&
	    TC_CHK_FLAG(port, TC_FLAGS_PR_SWAP_IN_PROGRESS)) {
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
	} else {
		/* Get connector orientation */
		tcpm_get_cc(port, &cc1, &cc2);
		tc[port].polarity = get_snk_polarity(cc1, cc2);
		typec_set_polarity(port, tc[port].polarity);

		/*
		 * TODO(b/300694918): SuperSpeed mux will be set as part of
		 * setting the data role, which will be redundant as
		 * the mux is explicitly set below. The following mux set should
		 * take priority.
		 */
		tc_set_data_role(port, PD_ROLE_UFP);

		/*
		 * Attached.SNK requirements from the
		 * "Universal Serial Bus Type-C Cable and Connector
		 * Specification" Release 2.2 paragraph 4.5.2.2.5.1:
		 *
		 * "If the port supports signaling on USB TX/RX pairs,
		 * it shall functionally connect the USB TX/RX pairs and
		 * maintain the connection during and after a USB PD PR_Swap."
		 *
		 * This allows for support of the Android Debug Bridge.
		 */
		if (IS_ENABLED(CONFIG_USBC_SS_MUX))
			usb_mux_set(port, USB_PD_MUX_USB_ENABLED,
				    USB_SWITCH_CONNECT, tc[port].polarity);

		hook_notify(HOOK_USB_PD_CONNECT);

		if (IS_ENABLED(CONFIG_CHARGE_MANAGER)) {
			tc[port].typec_curr = usb_get_typec_current_limit(
				tc[port].polarity, cc1, cc2);
			typec_set_input_current_limit(port, tc[port].typec_curr,
						      TYPE_C_VOLTAGE);
			/*
			 * Start new connections as dedicated until source caps
			 * are received, at which point the PE will update the
			 * flag.
			 */
			charge_manager_update_dualrole(port, CAP_DEDICATED);
		}

		/* Apply Rd */
		typec_update_cc(port);

		/*
		 * Attached.SNK - enable AutoDischargeDisconnect
		 * Do this after applying Rd to CC lines to avoid
		 * TCPC_REG_FAULT_STATUS_AUTO_DISCHARGE_FAIL (b/171567398)
		 */
		tcpm_enable_auto_discharge_disconnect(port, 1);
	}

	pd_timer_disable(port, TC_TIMER_CC_DEBOUNCE);

	/* Enable PD */
	if (IS_ENABLED(CONFIG_USB_PE_SM))
		tc_enable_pd(port, 1);

	if (TC_CHK_FLAG(port, TC_FLAGS_TS_DTS_PARTNER)) {
		tcpm_debug_accessory(port, 1);
		set_ccd_mode(port, 1);
	}
}

/*
 * Check whether Vbus has been removed on this port, accounting for some Vbus
 * debounce if FRS is enabled.
 *
 * Returns true if a new state was set and the calling run should exit.
 */
static bool tc_snk_check_vbus_removed(const int port)
{
	if (IS_ENABLED(CONFIG_USB_PD_FRS)) {
		/*
		 * Debounce Vbus presence when FRS is enabled. Note that we may
		 * lose Vbus before the FRS signal comes in to let us know
		 * we're PR swapping, but we must still transition to unattached
		 * within tSinkDisconnect.
		 *
		 * We may safely re-use the Vbus debounce timer here
		 * since a PR swap would no longer be in progress when Vbus
		 * removal is checked.
		 */
		if (pd_check_vbus_level(port, VBUS_REMOVED)) {
			if (pd_timer_is_disabled(port,
						 TC_TIMER_VBUS_DEBOUNCE)) {
				pd_timer_enable(port, TC_TIMER_VBUS_DEBOUNCE,
						PD_T_FRS_VBUS_DEBOUNCE);
			} else if (pd_timer_is_expired(
					   port, TC_TIMER_VBUS_DEBOUNCE)) {
				set_state_tc(port, TC_UNATTACHED_SNK);
				return true;
			}
		} else {
			pd_timer_disable(port, TC_TIMER_VBUS_DEBOUNCE);
		}
	} else if (pd_check_vbus_level(port, VBUS_REMOVED)) {
		set_state_tc(port, TC_UNATTACHED_SNK);
		return true;
	}

	return false;
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
	    pd_timer_is_expired(port, TC_TIMER_VBUS_DEBOUNCE)) {
		/* PR Swap is no longer in progress */
		TC_CLR_FLAG(port, TC_FLAGS_PR_SWAP_IN_PROGRESS);
		pd_timer_disable(port, TC_TIMER_VBUS_DEBOUNCE);

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
		if (tc_snk_check_vbus_removed(port))
			return;

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
						 PD_ROLE_DFP :
						 PD_ROLE_UFP);
		}

		/*
		 * VCONN Swap
		 * UnorientedDebugAccessory.SRC shall not drive Vconn
		 */
		if (IS_ENABLED(CONFIG_USBC_VCONN) &&
		    !TC_CHK_FLAG(port, TC_FLAGS_TS_DTS_PARTNER)) {
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
	if (tc_snk_check_vbus_removed(port))
		return;

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
		if (!TC_CHK_FLAG(port, TC_FLAGS_REQUEST_SUSPEND))
			tcpm_enable_auto_discharge_disconnect(port, 0);
	}

	/* Clear flags after checking Vconn status */
	TC_CLR_FLAG(port, TC_FLAGS_REQUEST_PR_SWAP | TC_FLAGS_POWER_OFF_SNK);

	/* Stop drawing power */
	sink_stop_drawing_current(port);

	if (TC_CHK_FLAG(port, TC_FLAGS_TS_DTS_PARTNER))
		tcpm_debug_detach(port);

	pd_timer_disable(port, TC_TIMER_CC_DEBOUNCE);
	pd_timer_disable(port, TC_TIMER_TIMEOUT);
	pd_timer_disable(port, TC_TIMER_VBUS_DEBOUNCE);
}

/**
 * Unattached.SRC
 */
static void tc_unattached_src_entry(const int port)
{
	enum pd_data_role prev_data_role;

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
	 *
	 * Restore default current limit Rp.
	 *
	 * Run any debug detaches needed before setting CC, as some TCPCs may
	 * require we set CC Open before changing power roles with a debug
	 * accessory.
	 */
	tcpm_debug_detach(port);
	typec_select_pull(port, TYPEC_CC_RP);
	typec_select_src_current_limit_rp(
		port, typec_get_default_current_limit_rp(port));
	typec_update_cc(port);

	prev_data_role = tc[port].data_role;
	tc[port].data_role = PD_ROLE_DISCONNECTED;

	/*
	 * When data role set events are used to enable BC1.2, then CC
	 * detach events are used to notify BC1.2 that it can be powered
	 * down.
	 */
	bc12_role_change_handler(port, prev_data_role, tc[port].data_role);

	tc_set_partner_role(port, PPC_DEV_DISCONNECTED, OCP_CLEAR);

	if (IS_ENABLED(CONFIG_CHARGE_MANAGER))
		charge_manager_update_dualrole(port, CAP_UNKNOWN);

#ifdef CONFIG_USB_PE_SM
	CLR_FLAGS_ON_DISCONNECT(port);
	tc[port].ps_reset_state = PS_STATE0;
#endif

	pd_timer_enable(port, TC_TIMER_NEXT_ROLE_SWAP, PD_T_DRP_SRC);
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

	if (IS_ENABLED(CONFIG_USBC_OCP)) {
		/*
		 * If the port is latched off, just continue to
		 * monitor for a detach.
		 */
		if (usbc_ocp_is_port_latched_off(port))
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
	else if (pd_timer_is_expired(port, TC_TIMER_NEXT_ROLE_SWAP) &&
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

static void tc_unattached_src_exit(const int port)
{
	pd_timer_disable(port, TC_TIMER_NEXT_ROLE_SWAP);
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

	if (cc_is_snk_dbg_acc(cc1, cc2) && board_is_dts_port(port)) {
		/*
		 * Debug accessory.
		 * A debug accessory in a non-DTS port will be
		 * recognized by at_least_one_rd as UFP attached.
		 */
		new_cc_state = PD_CC_UFP_DEBUG_ACC;
	} else if (cc_is_at_least_one_rd(cc1, cc2)) {
		/* UFP attached */
		new_cc_state = PD_CC_UFP_ATTACHED;
	} else if (cc_is_audio_acc(cc1, cc2)) {
		/* AUDIO Accessory not supported. Just ignore */
		new_cc_state = PD_CC_UFP_AUDIO_ACC;
	} else {
		/* No UFP */
		if (drp_state[port] == PD_DRP_FORCE_SOURCE)
			set_state_tc(port, TC_UNATTACHED_SRC);
		else
			set_state_tc(port, TC_UNATTACHED_SNK);
		return;
	}

	/* Debounce the cc state */
	if (new_cc_state != tc[port].cc_state) {
		pd_timer_enable(port, TC_TIMER_CC_DEBOUNCE, PD_T_CC_DEBOUNCE);
		tc[port].cc_state = new_cc_state;
		return;
	}

	/* Wait for CC debounce */
	if (!pd_timer_is_expired(port, TC_TIMER_CC_DEBOUNCE))
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

static void tc_attach_wait_src_exit(const int port)
{
	pd_timer_disable(port, TC_TIMER_CC_DEBOUNCE);
}

/**
 * Attached.SRC, shared with UnorientedDebugAccessory.SRC
 */
static void tc_attached_src_entry(const int port)
{
	enum tcpc_cc_voltage_status cc1, cc2;

	print_current_state(port);

	pd_timer_disable(port, TC_TIMER_TIMEOUT);

	/*
	 * Known state of attach is SRC.  We need to apply this pull value
	 * to make it set in hardware at the correct time but set the common
	 * pull here.
	 *
	 * Both CC1 and CC2 pins shall be independently terminated to
	 * pulled up through Rp.
	 *
	 * Set selected current limit in the hardware.
	 */
	typec_select_pull(port, TYPEC_CC_RP);
	typec_set_source_current_limit(port, tc[port].select_current_limit_rp);

	if (IS_ENABLED(CONFIG_USB_PE_SM)) {
		if (TC_CHK_FLAG(port, TC_FLAGS_PR_SWAP_IN_PROGRESS)) {
			/* Change role to source */
			tc_set_power_role(port, PD_ROLE_SOURCE);
			tcpm_set_msg_header(port, tc[port].power_role,
					    tc[port].data_role);

			/* Enable VBUS */
			tc_src_power_on(port);

			/* Apply Rp */
			typec_update_cc(port);

			/*
			 * Maintain VCONN supply state, whether ON or OFF, and
			 * its data role / usb mux connections. Do not
			 * re-enable AutoDischargeDisconnect until the swap is
			 * completed and tc_pr_swap_complete is called.
			 */
		} else {
			/*
			 * Set up CC's, Vconn, and ADD before Vbus, as per
			 * Figure 4-24. DRP Initialization and Connection
			 * Detection in TCPCI r2 v1.2 specification.
			 */

			/* Get connector orientation */
			tcpm_get_cc(port, &cc1, &cc2);
			tc[port].polarity = get_src_polarity(cc1, cc2);
			typec_set_polarity(port, tc[port].polarity);

			/* Attached.SRC - enable AutoDischargeDisconnect */
			tcpm_enable_auto_discharge_disconnect(port, 1);

			/* Apply Rp */
			typec_update_cc(port);

			/*
			 * Initial data role for sink is DFP
			 * This also sets the usb mux, which will be overridden
			 * by the following usb_mux_set call: TODO(b/300694918)
			 */
			tc_set_data_role(port, PD_ROLE_DFP);

			/*
			 * Attached.SRC requirements from the
			 * "Universal Serial Bus Type-C Cable and Connector
			 * Specification" Release 2.2 paragraph 4.5.2.2.9.1:
			 *
			 * "If the port supports signaling on USB TX/RX pairs,
			 * it shall:" with supplying Vconn, "Functionally
			 * connect the USB TX/RX pairs"
			 */
			if (IS_ENABLED(CONFIG_USBC_SS_MUX))
				usb_mux_set(port, USB_PD_MUX_USB_ENABLED,
					    USB_SWITCH_CONNECT,
					    tc[port].polarity);

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
				/* Stop sourcing Vconn if Vbus failed
				 * TODO(b/300691956): Take action on failure
				 */
				if (IS_ENABLED(CONFIG_USBC_VCONN))
					set_vconn(port, 0);

				if (IS_ENABLED(CONFIG_USBC_SS_MUX))
					usb_mux_set(port, USB_PD_MUX_NONE,
						    USB_SWITCH_DISCONNECT,
						    tc[port].polarity);
			}

			tc_enable_pd(port, 0);
			pd_timer_enable(port, TC_TIMER_TIMEOUT,
					MAX(PD_POWER_SUPPLY_TURN_ON_DELAY,
					    PD_T_VCONN_STABLE));
		}
	} else {
		/*
		 * Set up CC's, Vconn, and ADD before Vbus, as per
		 * Figure 4-24. DRP Initialization and Connection
		 * Detection in TCPCI r2 v1.2 specification.
		 */

		/* Get connector orientation */
		tcpm_get_cc(port, &cc1, &cc2);
		tc[port].polarity = get_src_polarity(cc1, cc2);
		typec_set_polarity(port, tc[port].polarity);

		/* Attached.SRC - enable AutoDischargeDisconnect */
		tcpm_enable_auto_discharge_disconnect(port, 1);

		/* Apply Rp */
		typec_update_cc(port);

		/*
		 * Initial data role for sink is DFP
		 * This also sets the usb mux, which will be overridden
		 * by the following usb_mux_set call: TODO(b/300694918)
		 */
		tc_set_data_role(port, PD_ROLE_DFP);

		/*
		 * Attached.SRC requirements from the
		 * "Universal Serial Bus Type-C Cable and Connector
		 * Specification" Release 2.2 paragraph 4.5.2.2.9.1:
		 *
		 * "If the port supports signaling on USB TX/RX pairs, it
		 * shall:" along with supplying Vconn, "Functionally connect
		 * the USB TX/RX pairs"
		 */
		if (IS_ENABLED(CONFIG_USBC_SS_MUX))
			usb_mux_set(port, USB_PD_MUX_USB_ENABLED,
				    USB_SWITCH_CONNECT, tc[port].polarity);

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
			/* Stop sourcing Vconn if Vbus failed
			 * TODO(b/300691956): Take action on failure
			 */
			if (IS_ENABLED(CONFIG_USBC_VCONN))
				set_vconn(port, 0);

			if (IS_ENABLED(CONFIG_USBC_SS_MUX))
				usb_mux_set(port, USB_PD_MUX_NONE,
					    USB_SWITCH_DISCONNECT,
					    tc[port].polarity);
		}
	}

	/* Inform PPC and OCP module that a sink is connected. */
	tc_set_partner_role(port, PPC_DEV_SNK, OCP_NO_ACTION);

	/* Initialize type-C supplier to seed the charge manger */
	if (IS_ENABLED(CONFIG_CHARGE_MANAGER))
		typec_set_input_current_limit(port, 0, 0);

	/*
	 * Only notify if we're not performing a power role swap.  During a
	 * power role swap, the port partner is not disconnecting/connecting.
	 */
	if (!TC_CHK_FLAG(port, TC_FLAGS_PR_SWAP_IN_PROGRESS)) {
		hook_notify(HOOK_USB_PD_CONNECT);
	}

	if (TC_CHK_FLAG(port, TC_FLAGS_TS_DTS_PARTNER)) {
		tcpm_debug_accessory(port, 1);
		set_ccd_mode(port, 1);
	}

	/*
	 * Some TCPCs require time to correctly return CC status after
	 * changing the ROLE_CONTROL register. Due to that, we have to ignore
	 * CC_NONE state until PD_T_SRC_DISCONNECT delay has elapsed.
	 * From the "Universal Serial Bus Type-C Cable and Connector
	 * Specification" Release 2.0 paragraph 4.5.2.2.9.2:
	 * The Source shall detect the SRC.Open state within tSRCDisconnect,
	 * but should detect it as quickly as possible
	 */
	pd_timer_enable(port, TC_TIMER_CC_DEBOUNCE, PD_T_SRC_DISCONNECT);
}

static void tc_attached_src_run(const int port)
{
	enum tcpc_cc_voltage_status cc1, cc2;

	/* Check for connection */
	tcpm_get_cc(port, &cc1, &cc2);

	if (polarity_rm_dts(tc[port].polarity))
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
	    pd_timer_is_expired(port, TC_TIMER_CC_DEBOUNCE)) {
		bool tryWait;
		enum usb_tc_state new_tc_state = TC_UNATTACHED_SNK;

		if (IS_ENABLED(CONFIG_USB_PD_TRY_SRC))
			tryWait = is_try_src_enabled(port) &&
				  !TC_CHK_FLAG(port, TC_FLAGS_TS_DTS_PARTNER);

		if (drp_state[port] == PD_DRP_FORCE_SOURCE)
			new_tc_state = TC_UNATTACHED_SRC;
		else if (IS_ENABLED(CONFIG_USB_PD_TRY_SRC))
			new_tc_state = tryWait ? TC_TRY_WAIT_SNK :
						 TC_UNATTACHED_SNK;

		set_state_tc(port, new_tc_state);
		return;
	}

#ifdef CONFIG_USB_PE_SM
	/*
	 * Enable PD communications after power supply has fully
	 * turned on
	 */
	if (pd_timer_is_expired(port, TC_TIMER_TIMEOUT)) {
		tc_enable_pd(port, 1);
		pd_timer_disable(port, TC_TIMER_TIMEOUT);
	}

	if (!tc_get_pd_enabled(port))
		return;

	/*
	 * Handle Hard Reset from Policy Engine
	 */
	if (TC_CHK_FLAG(port, TC_FLAGS_HARD_RESET_REQUESTED)) {
		/* Ignoring Hard Resets while the power supply is resetting.*/
		if (!pd_timer_is_disabled(port, TC_TIMER_TIMEOUT) &&
		    !pd_timer_is_expired(port, TC_TIMER_TIMEOUT))
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
						 PD_ROLE_UFP :
						 PD_ROLE_DFP);
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
			set_state_tc(port, TC_CT_UNATTACHED_SNK);
		}
	}
#endif

	if (TC_CHK_FLAG(port, TC_FLAGS_UPDATE_CURRENT)) {
		TC_CLR_FLAG(port, TC_FLAGS_UPDATE_CURRENT);
		typec_set_source_current_limit(
			port, tc[port].select_current_limit_rp);
		pd_update_contract(port);

		/* Update Rp if no contract is present */
		if (!IS_ENABLED(CONFIG_USB_PE_SM) ||
		    !pe_is_explicit_contract(port))
			typec_update_cc(port);
	}
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

		/*
		 * Disable VCONN if not power role swapping and
		 * a CTVPD was not detected
		 */
		if (TC_CHK_FLAG(port, TC_FLAGS_VCONN_ON) &&
		    !TC_CHK_FLAG(port, TC_FLAGS_CTVPD_DETECTED))
			set_vconn(port, 0);
	}

	/* Clear CTVPD detected after checking for Vconn */
	TC_CLR_FLAG(port, TC_FLAGS_CTVPD_DETECTED);

	/* Clear PR swap flag after checking for Vconn */
	TC_CLR_FLAG(port, TC_FLAGS_REQUEST_PR_SWAP);

	if (TC_CHK_FLAG(port, TC_FLAGS_TS_DTS_PARTNER))
		tcpm_debug_detach(port);

	pd_timer_disable(port, TC_TIMER_CC_DEBOUNCE);
	pd_timer_disable(port, TC_TIMER_TIMEOUT);
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
	next_state = drp_auto_toggle_next_state(
		&tc[port].drp_sink_time, tc[port].power_role, drp_state[port],
		cc1, cc2, tcpm_auto_toggle_supported(port));

	if (next_state == DRP_TC_DEFAULT)
		next_state = (PD_ROLE_DEFAULT(port) == PD_ROLE_SOURCE) ?
				     DRP_TC_UNATTACHED_SRC :
				     DRP_TC_UNATTACHED_SNK;

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

/**
 * DrpAutoToggle
 */
__maybe_unused static void tc_drp_auto_toggle_entry(const int port)
{
	if (!IS_ENABLED(CONFIG_USB_PD_DUAL_ROLE_AUTO_TOGGLE))
		assert(0);

	print_current_state(port);

	/*
	 * We need to ensure that we are waiting in the previous Rd or Rp state
	 * for the minimum of DRP SNK or SRC so the first toggle cause by
	 * transition into auto toggle doesn't violate spec timing.
	 */
	pd_timer_enable(port, TC_TIMER_TIMEOUT,
			MAX(PD_T_DRP_SNK, PD_T_DRP_SRC));
}

__maybe_unused static void tc_drp_auto_toggle_run(const int port)
{
	if (!IS_ENABLED(CONFIG_USB_PD_DUAL_ROLE_AUTO_TOGGLE))
		assert(0);

	/*
	 * A timer is running, but if a connection comes in while waiting
	 * then allow that to take higher priority.
	 */
	if (TC_CHK_FLAG(port, TC_FLAGS_CHECK_CONNECTION))
		check_drp_connection(port);

	else if (!pd_timer_is_disabled(port, TC_TIMER_TIMEOUT)) {
		if (!pd_timer_is_expired(port, TC_TIMER_TIMEOUT))
			return;

		pd_timer_disable(port, TC_TIMER_TIMEOUT);
		tcpm_enable_drp_toggle(port);

		if (IS_ENABLED(CONFIG_USB_PD_TCPC_LOW_POWER)) {
			set_state_tc(port, TC_LOW_POWER_MODE);
		}
	}
}

__maybe_unused static void tc_drp_auto_toggle_exit(const int port)
{
	pd_timer_disable(port, TC_TIMER_TIMEOUT);
}

__maybe_unused static void tc_low_power_mode_entry(const int port)
{
	if (!IS_ENABLED(CONFIG_USB_PD_TCPC_LOW_POWER))
		assert(0);

	print_current_state(port);
	pd_timer_enable(port, TC_TIMER_LOW_POWER_TIME, PD_LPM_DEBOUNCE_US);
}

__maybe_unused static void tc_low_power_mode_run(const int port)
{
	if (!IS_ENABLED(CONFIG_USB_PD_TCPC_LOW_POWER))
		assert(0);

	if (TC_CHK_FLAG(port, TC_FLAGS_CHECK_CONNECTION)) {
		tc_start_event_loop(port);
		if (pd_timer_is_disabled(port, TC_TIMER_LOW_POWER_EXIT_TIME)) {
			pd_timer_enable(port, TC_TIMER_LOW_POWER_EXIT_TIME,
					PD_LPM_EXIT_DEBOUNCE_US);
		} else if (pd_timer_is_expired(port,
					       TC_TIMER_LOW_POWER_EXIT_TIME)) {
			CPRINTS("C%d: Exit Low Power Mode", port);
			check_drp_connection(port);
		}
		return;
	}

	if (tc[port].tasks_preventing_lpm)
		pd_timer_enable(port, TC_TIMER_LOW_POWER_TIME,
				PD_LPM_DEBOUNCE_US);

	if (pd_timer_is_expired(port, TC_TIMER_LOW_POWER_TIME)) {
		CPRINTS("C%d: TCPC Enter Low Power Mode", port);
		TC_SET_FLAG(port, TC_FLAGS_LPM_ENGAGED);
		TC_SET_FLAG(port, TC_FLAGS_LPM_TRANSITION);
		tcpm_enter_low_power_mode(port);
		TC_CLR_FLAG(port, TC_FLAGS_LPM_TRANSITION);
		tc_pause_event_loop(port);

		pd_timer_disable(port, TC_TIMER_LOW_POWER_EXIT_TIME);
	}
}

__maybe_unused static void tc_low_power_mode_exit(const int port)
{
	pd_timer_disable(port, TC_TIMER_LOW_POWER_TIME);
	pd_timer_disable(port, TC_TIMER_LOW_POWER_EXIT_TIME);
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

	tc[port].cc_state = PD_CC_UNSET;
	pd_timer_enable(port, TC_TIMER_TRY_WAIT_DEBOUNCE, PD_T_DRP_TRY);
	pd_timer_enable(port, TC_TIMER_TIMEOUT, PD_T_TRY_TIMEOUT);

	/*
	 * We are a SNK but would prefer to be a SRC.  Set the pull to
	 * indicate we want to be a SRC and looking for a SNK.
	 *
	 * Both CC1 and CC2 pins shall be independently terminated to
	 * ground through Rp.
	 */
	typec_select_pull(port, TYPEC_CC_RP);

	typec_select_src_current_limit_rp(
		port, typec_get_default_current_limit_rp(port));

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
		pd_timer_enable(port, TC_TIMER_CC_DEBOUNCE, PD_T_CC_DEBOUNCE);
	}

	/*
	 * The port shall transition to Attached.SRC when the SRC.Rd state is
	 * detected on exactly one of the CC1 or CC2 pins for at least
	 * tTryCCDebounce.
	 */
	if (new_cc_state == PD_CC_UFP_ATTACHED &&
	    pd_timer_is_expired(port, TC_TIMER_CC_DEBOUNCE))
		set_state_tc(port, TC_ATTACHED_SRC);

	/*
	 * The port shall transition to TryWait.SNK after tDRPTry and the
	 * SRC.Rd state has not been detected and VBUS is within vSafe0V,
	 * or after tTryTimeout and the SRC.Rd state has not been detected.
	 */
	if (new_cc_state == PD_CC_NONE) {
		if ((pd_timer_is_expired(port, TC_TIMER_TRY_WAIT_DEBOUNCE) &&
		     pd_check_vbus_level(port, VBUS_SAFE0V)) ||
		    pd_timer_is_expired(port, TC_TIMER_TIMEOUT)) {
			set_state_tc(port, TC_TRY_WAIT_SNK);
		}
	}
}

static void tc_try_src_exit(const int port)
{
	pd_timer_disable(port, TC_TIMER_CC_DEBOUNCE);
	pd_timer_disable(port, TC_TIMER_TIMEOUT);
	pd_timer_disable(port, TC_TIMER_TRY_WAIT_DEBOUNCE);
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
	pd_timer_enable(port, TC_TIMER_TRY_WAIT_DEBOUNCE, PD_T_CC_DEBOUNCE);

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
		pd_timer_enable(port, TC_TIMER_PD_DEBOUNCE, PD_T_PD_DEBOUNCE);
	}

	/*
	 * The port shall transition to Unattached.SNK when the state of both
	 * of the CC1 and CC2 pins is SNK.Open for at least tPDDebounce.
	 */
	if (new_cc_state == PD_CC_NONE &&
	    pd_timer_is_expired(port, TC_TIMER_PD_DEBOUNCE)) {
		set_state_tc(port, TC_UNATTACHED_SNK);
		return;
	}

	/*
	 * The port shall transition to Attached.SNK after tCCDebounce if or
	 * when VBUS is detected.
	 */
	if (pd_timer_is_expired(port, TC_TIMER_TRY_WAIT_DEBOUNCE) &&
	    pd_is_vbus_present(port))
		set_state_tc(port, TC_ATTACHED_SNK);
}

static void tc_try_wait_snk_exit(const int port)
{
	pd_timer_disable(port, TC_TIMER_PD_DEBOUNCE);
	pd_timer_disable(port, TC_TIMER_TRY_WAIT_DEBOUNCE);
}
#endif

/*
 * CTUnattached.SNK
 */
__maybe_unused static void tc_ct_unattached_snk_entry(int port)
{
	if (!IS_ENABLED(CONFIG_USB_PE_SM))
		assert(0);

	print_current_state(port);

	/*
	 * Both CC1 and CC2 pins shall be independently terminated to
	 * ground through Rd.
	 */
	typec_select_pull(port, TYPEC_CC_RD);
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

	pd_timer_enable(port, TC_TIMER_TIMEOUT, PD_POWER_SUPPLY_TURN_ON_DELAY);
}

__maybe_unused static void tc_ct_unattached_snk_run(int port)
{
	enum tcpc_cc_voltage_status cc1;
	enum tcpc_cc_voltage_status cc2;
	enum pd_cc_states new_cc_state;

	if (!IS_ENABLED(CONFIG_USB_PE_SM))
		assert(0);

	if (!pd_timer_is_disabled(port, TC_TIMER_TIMEOUT)) {
		if (pd_timer_is_expired(port, TC_TIMER_TIMEOUT)) {
			tc_enable_pd(port, 1);
			pd_timer_disable(port, TC_TIMER_TIMEOUT);
		} else {
			return;
		}
	}

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
		pd_timer_enable(port, TC_TIMER_CC_DEBOUNCE, PD_T_VPDDETACH);
	}

	/*
	 * The port shall transition to Unattached.SNK if the state of
	 * the CC pin is SNK.Open for tVPDDetach after VBUS is vSafe0V.
	 */
	else if (pd_timer_is_expired(port, TC_TIMER_CC_DEBOUNCE)) {
		if (new_cc_state == PD_CC_NONE &&
		    pd_check_vbus_level(port, VBUS_SAFE0V)) {
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

__maybe_unused static void tc_ct_unattached_snk_exit(int port)
{
	pd_timer_disable(port, TC_TIMER_CC_DEBOUNCE);
	pd_timer_disable(port, TC_TIMER_TIMEOUT);
}

/**
 * CTAttached.SNK
 */
__maybe_unused static void tc_ct_attached_snk_entry(int port)
{
	if (!IS_ENABLED(CONFIG_USB_PE_SM))
		assert(0);

	print_current_state(port);

	/* The port shall reject a VCONN swap request. */
	TC_SET_FLAG(port, TC_FLAGS_REJECT_VCONN_SWAP);

	/*
	 * Type-C r 2.2: The Host shall not advertise dual-role data or
	 * dual-role power in its SourceCapability or SinkCapability messages -
	 * Host changes its advertised capabilities to UFP role/sink only role.
	 */
	tc_set_data_role(port, PD_ROLE_UFP);
}

__maybe_unused static void tc_ct_attached_snk_run(int port)
{
	if (!IS_ENABLED(CONFIG_USB_PE_SM))
		assert(0);

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

__maybe_unused static void tc_ct_attached_snk_exit(int port)
{
	if (!IS_ENABLED(CONFIG_USB_PE_SM))
		assert(0);

	/* Stop drawing power */
	sink_stop_drawing_current(port);

	TC_CLR_FLAG(port, TC_FLAGS_REJECT_VCONN_SWAP);
}

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

	/*
	 * We may brown out after applying CC open, so flush console first.
	 * Console flush can take a long time, so if we aren't in danger of
	 * browning out, don't do it so we can meet certain compliance timing
	 * requirements.
	 */
	CPRINTS("C%d: Applying CC Open!", port);
	if (!battery_is_present())
		cflush();

	/* Remove terminations from CC */
	typec_select_pull(port, TYPEC_CC_OPEN);
	typec_update_cc(port);

	/*
	 * While we've disconnected the partner, leave any OCP counts in place
	 * to persist over ErrorRecovery
	 */
	tc_set_partner_role(port, PPC_DEV_DISCONNECTED, OCP_NO_ACTION);
	tc_detached(port);
}

void tc_set_debug_level(enum debug_level debug_level)
{
#ifndef CONFIG_USB_PD_DEBUG_LEVEL
	tc_debug_level = debug_level;
#endif
}

void tc_usb_firmware_fw_update_limited_run(int port)
{
	TC_SET_FLAG(port, TC_FLAGS_USB_RETIMER_FW_UPDATE_LTD_RUN);
	task_wake(PD_PORT_TO_TASK_ID(port));
}

void tc_usb_firmware_fw_update_run(int port)
{
	TC_SET_FLAG(port, TC_FLAGS_USB_RETIMER_FW_UPDATE_RUN);
	task_wake(PD_PORT_TO_TASK_ID(port));
}

void tc_run(const int port)
{
	/*
	 * If pd_set_suspend set TC_FLAGS_REQUEST_SUSPEND, go directly to
	 * TC_DISABLED.
	 */
	if (get_state_tc(port) != TC_DISABLED &&
	    TC_CHK_FLAG(port, TC_FLAGS_REQUEST_SUSPEND)) {
		/* Invalidate a contract, if there is one */
		if (IS_ENABLED(CONFIG_USB_PE_SM))
			pe_invalidate_explicit_contract(port);

		set_state_tc(port, TC_DISABLED);
	}

	/* If error recovery has been requested, transition now */
	if (TC_CHK_FLAG(port, TC_FLAGS_REQUEST_ERROR_RECOVERY)) {
		if (IS_ENABLED(CONFIG_USB_PE_SM))
			pe_invalidate_explicit_contract(port);
		set_state_tc(port, TC_ERROR_RECOVERY);
	}

	if (IS_ENABLED(CONFIG_USBC_RETIMER_FW_UPDATE)) {
		if (TC_CHK_FLAG(port, TC_FLAGS_USB_RETIMER_FW_UPDATE_RUN)) {
			TC_CLR_FLAG(port, TC_FLAGS_USB_RETIMER_FW_UPDATE_RUN);
			usb_retimer_fw_update_process_op_cb(port);
		}
	}

	run_state(port, &tc[port].ctx);
}

static void pd_chipset_resume(void)
{
	int i;

	for (i = 0; i < CONFIG_USB_PD_PORT_MAX_COUNT; i++) {
		if (IS_ENABLED(CONFIG_USB_PE_SM))
			pd_resume_check_pr_swap_needed(i);

		pd_set_dual_role_and_event(i, pd_get_drp_state_in_s0(),
					   PD_EVENT_UPDATE_DUAL_ROLE |
						   PD_EVENT_POWER_STATE_CHANGE);

		if (tc[i].data_role == PD_ROLE_DFP) {
			pd_send_alert_msg(i, ADO_EXTENDED_ALERT_EVENT |
						     ADO_POWER_STATE_CHANGE);
		}
	}

	CPRINTS("PD:S3->S0");
}
DECLARE_HOOK(HOOK_CHIPSET_RESUME, pd_chipset_resume, HOOK_PRIO_DEFAULT);

static void pd_chipset_suspend(void)
{
	int i;

	for (i = 0; i < CONFIG_USB_PD_PORT_MAX_COUNT; i++) {
		pd_set_dual_role_and_event(i, pd_get_drp_state_in_suspend(),
					   PD_EVENT_UPDATE_DUAL_ROLE |
						   PD_EVENT_POWER_STATE_CHANGE);

		if (tc[i].data_role == PD_ROLE_DFP) {
			pd_send_alert_msg(i, ADO_EXTENDED_ALERT_EVENT |
						     ADO_POWER_STATE_CHANGE);
		}
	}

	CPRINTS("PD:S0->S3");
}
DECLARE_HOOK(HOOK_CHIPSET_SUSPEND, pd_chipset_suspend, HOOK_PRIO_DEFAULT);

static void pd_chipset_reset(void)
{
	int i;

	if (!IS_ENABLED(CONFIG_USB_PE_SM))
		return;

	for (i = 0; i < board_get_usb_pd_port_count(); i++) {
		enum tcpci_msg_type tx;

		/* Do not notify the AP of irrelevant past Hard Resets. */
		pd_clear_events(i, PD_STATUS_EVENT_HARD_RESET);

		/*
		 * Re-set events for SOP and SOP' discovery complete so the
		 * kernel knows to consume discovery information for them.
		 */
		for (tx = TCPCI_MSG_SOP; tx <= TCPCI_MSG_SOP_PRIME; tx++) {
			if (pd_get_identity_discovery(i, tx) !=
				    PD_DISC_NEEDED &&
			    pd_get_svids_discovery(i, tx) != PD_DISC_NEEDED &&
			    pd_get_modes_discovery(i, tx) != PD_DISC_NEEDED)
				pd_notify_event(
					i,
					tx == TCPCI_MSG_SOP ?
						PD_STATUS_EVENT_SOP_DISC_DONE :
						PD_STATUS_EVENT_SOP_PRIME_DISC_DONE);
		}

		/* Exit mode so AP can enter mode again after reset */
		if (IS_ENABLED(CONFIG_USB_PD_REQUIRE_AP_MODE_ENTRY))
			dpm_set_mode_exit_request(i);
	}
}
DECLARE_HOOK(HOOK_CHIPSET_RESET, pd_chipset_reset, HOOK_PRIO_DEFAULT);

static void pd_chipset_startup(void)
{
	int i;

	for (i = 0; i < CONFIG_USB_PD_PORT_MAX_COUNT; i++) {
		TC_SET_FLAG(i, TC_FLAGS_UPDATE_USB_MUX);
		pd_set_dual_role_and_event(i, pd_get_drp_state_in_suspend(),
					   PD_EVENT_UPDATE_DUAL_ROLE |
						   PD_EVENT_POWER_STATE_CHANGE);
		/*
		 * Request port discovery to restore any
		 * alt modes.
		 * TODO(b/158042116): Do not start port discovery if there
		 * is an existing connection.
		 */
		if (IS_ENABLED(CONFIG_USB_PE_SM))
			pd_dpm_request(i, DPM_REQUEST_PORT_DISCOVERY);

		if (tc[i].data_role == PD_ROLE_DFP) {
			pd_send_alert_msg(i, ADO_EXTENDED_ALERT_EVENT |
						     ADO_POWER_STATE_CHANGE);
		}
	}

	CPRINTS("PD:S5->S3");
}
DECLARE_HOOK(HOOK_CHIPSET_STARTUP, pd_chipset_startup, HOOK_PRIO_DEFAULT);

static void pd_chipset_shutdown(void)
{
	int i;

	for (i = 0; i < CONFIG_USB_PD_PORT_MAX_COUNT; i++) {
		TC_SET_FLAG(i, TC_FLAGS_UPDATE_USB_MUX);
		pd_set_dual_role_and_event(i, PD_DRP_FORCE_SINK,
					   PD_EVENT_UPDATE_DUAL_ROLE |
						   PD_EVENT_POWER_STATE_CHANGE);

		if (tc[i].data_role == PD_ROLE_DFP) {
			pd_send_alert_msg(i, ADO_EXTENDED_ALERT_EVENT |
						     ADO_POWER_STATE_CHANGE);
		}
	}

	CPRINTS("PD:S3->S5");
}
DECLARE_HOOK(HOOK_CHIPSET_SHUTDOWN, pd_chipset_shutdown, HOOK_PRIO_DEFAULT);

static void pd_set_power_change(void)
{
	int i;

	for (i = 0; i < CONFIG_USB_PD_PORT_MAX_COUNT; i++) {
		task_set_event(PD_PORT_TO_TASK_ID(i),
			       PD_EVENT_POWER_STATE_CHANGE);
	}
}
DECLARE_DEFERRED(pd_set_power_change);

static void pd_chipset_hard_off(void)
{
	/*
	 * Wait 1 second to check our Vconn sourcing status, as the power rails
	 * which were supporting it may take some time to change after entering
	 * G3.
	 */
	hook_call_deferred(&pd_set_power_change_data, 1 * SECOND);
}
DECLARE_HOOK(HOOK_CHIPSET_HARD_OFF, pd_chipset_hard_off, HOOK_PRIO_DEFAULT);

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
static __const_data const struct usb_state tc_states[] = {
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
		.exit   = tc_error_recovery_exit,
		.parent = &tc_states[TC_CC_OPEN],
	},
	[TC_UNATTACHED_SNK] = {
		.entry	= tc_unattached_snk_entry,
		.run	= tc_unattached_snk_run,
		.exit	= tc_unattached_snk_exit,
		.parent = &tc_states[TC_CC_RD],
	},
	[TC_ATTACH_WAIT_SNK] = {
		.entry	= tc_attach_wait_snk_entry,
		.run	= tc_attach_wait_snk_run,
		.exit	= tc_attach_wait_snk_exit,
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
		.exit	= tc_unattached_src_exit,
		.parent = &tc_states[TC_CC_RP],
	},
	[TC_ATTACH_WAIT_SRC] = {
		.entry	= tc_attach_wait_src_entry,
		.run	= tc_attach_wait_src_run,
		.exit	= tc_attach_wait_src_exit,
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
		.exit	= tc_try_src_exit,
		.parent = &tc_states[TC_CC_RP],
	},
	[TC_TRY_WAIT_SNK] = {
		.entry	= tc_try_wait_snk_entry,
		.run	= tc_try_wait_snk_run,
		.exit	= tc_try_wait_snk_exit,
		.parent = &tc_states[TC_CC_RD],
	},
#endif /* CONFIG_USB_PD_TRY_SRC */
#ifdef CONFIG_USB_PD_DUAL_ROLE_AUTO_TOGGLE
	[TC_DRP_AUTO_TOGGLE] = {
		.entry = tc_drp_auto_toggle_entry,
		.run   = tc_drp_auto_toggle_run,
		.exit  = tc_drp_auto_toggle_exit,
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
		.exit  = tc_ct_unattached_snk_exit,
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
const int test_tc_sm_data_size = ARRAY_SIZE(test_tc_sm_data);
#endif
