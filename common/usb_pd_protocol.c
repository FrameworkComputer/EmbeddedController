/* Copyright 2014 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* LCOV_EXCL_START - TCPMv1 is difficult to meaningfully test: b/304349098. */

#include "atomic.h"
#include "battery.h"
#include "battery_smart.h"
#include "board.h"
#include "builtin/assert.h"
#include "charge_manager.h"
#include "charge_state.h"
#include "chipset.h"
#include "common.h"
#include "console.h"
#include "cros_version.h"
#include "ec_commands.h"
#include "gpio.h"
#include "hooks.h"
#include "host_command.h"
#include "printf.h"
#include "registers.h"
#include "system.h"
#include "task.h"
#include "tcpm/tcpci.h"
#include "tcpm/tcpm.h"
#include "timer.h"
#include "typec_control.h"
#include "usb_charge.h"
#include "usb_common.h"
#include "usb_mux.h"
#include "usb_pd.h"
#include "usb_pd_flags.h"
#include "usb_pd_tcpc.h"
#include "usb_pd_tcpm.h"
#include "usbc_ocp.h"
#include "usbc_ppc.h"
#include "util.h"
#include "vboot.h"

/* Flags to clear on a disconnect */
#define PD_FLAGS_RESET_ON_DISCONNECT_MASK                                   \
	(PD_FLAGS_PARTNER_DR_POWER | PD_FLAGS_PARTNER_DR_DATA |             \
	 PD_FLAGS_CHECK_IDENTITY | PD_FLAGS_SNK_CAP_RECVD |                 \
	 PD_FLAGS_TCPC_DRP_TOGGLE | PD_FLAGS_EXPLICIT_CONTRACT |            \
	 PD_FLAGS_PREVIOUS_PD_CONN | PD_FLAGS_CHECK_PR_ROLE |               \
	 PD_FLAGS_CHECK_DR_ROLE | PD_FLAGS_PARTNER_UNCONSTR |               \
	 PD_FLAGS_VCONN_ON | PD_FLAGS_TRY_SRC | PD_FLAGS_PARTNER_USB_COMM | \
	 PD_FLAGS_UPDATE_SRC_CAPS | PD_FLAGS_TS_DTS_PARTNER |               \
	 PD_FLAGS_SNK_WAITING_BATT | PD_FLAGS_CHECK_VCONN_STATE)

#ifdef CONFIG_COMMON_RUNTIME
#define CPRINTF(format, args...) cprintf(CC_USBPD, format, ##args)
#define CPRINTS(format, args...) cprints(CC_USBPD, format, ##args)

static int tcpc_prints(const char *string, int port)
{
	return CPRINTS("TCPC p%d %s", port, string);
}

BUILD_ASSERT(CONFIG_USB_PD_PORT_MAX_COUNT <= EC_USB_PD_MAX_PORTS);

/*
 * Debug log level - higher number == more log
 *   Level 0: Log state transitions
 *   Level 1: Level 0, plus state name
 *   Level 2: Level 1, plus packet info
 *   Level 3: Level 2, plus ping packet and packet dump on error
 *
 * Note that higher log level causes timing changes and thus may affect
 * performance.
 *
 * Can be limited to constant debug_level by CONFIG_USB_PD_DEBUG_LEVEL
 */
#ifdef CONFIG_USB_PD_DEBUG_LEVEL
static const int debug_level = CONFIG_USB_PD_DEBUG_LEVEL;
#else
static int debug_level;
#endif

/*
 * PD communication enabled flag. When false, PD state machine still
 * detects source/sink connection and disconnection, and will still
 * provide VBUS, but never sends any PD communication.
 */
static uint8_t pd_comm_enabled[CONFIG_USB_PD_PORT_MAX_COUNT];
#else /* CONFIG_COMMON_RUNTIME */
#define CPRINTF(format, args...)
#define CPRINTS(format, args...)
#define tcpc_prints(string, port)
static const int debug_level;
#endif

#ifdef CONFIG_USB_PD_DUAL_ROLE
#define DUAL_ROLE_IF_ELSE(port, sink_clause, src_clause) \
	(pd[port].power_role == PD_ROLE_SINK ? (sink_clause) : (src_clause))
#else
#define DUAL_ROLE_IF_ELSE(port, sink_clause, src_clause) (src_clause)
#endif

#define READY_RETURN_STATE(port) \
	DUAL_ROLE_IF_ELSE(port, PD_STATE_SNK_READY, PD_STATE_SRC_READY)

/* Type C supply voltage (mV) */
#define TYPE_C_VOLTAGE 5000 /* mV */

/* PD counter definitions */
#define PD_MESSAGE_ID_COUNT 7
#define PD_HARD_RESET_COUNT 2
#define PD_CAPS_COUNT 50
#define PD_SNK_CAP_RETRIES 3

/*
 * The time that we allow the port partner to send any messages after an
 * explicit contract is established.  200ms was chosen somewhat arbitrarily as
 * it should be long enough for sources to decide to send a message if they were
 * going to, but not so long that a "low power charger connected" notification
 * would be shown in the chrome OS UI.
 */
#define SNK_READY_HOLD_OFF_US (200 * MSEC)
/*
 * For the same purpose as SNK_READY_HOLD_OFF_US, but this delay can be longer
 * since the concern over "low power charger" is not relevant when connected as
 * a source and the additional delay avoids a race condition where the partner
 * port sends a power role swap request close to when the VDM discover identity
 * message gets sent.
 */
#define SRC_READY_HOLD_OFF_US (400 * MSEC)

enum ams_seq {
	AMS_START,
	AMS_RESPONSE,
};

enum vdm_states {
	VDM_STATE_ERR_BUSY = -3,
	VDM_STATE_ERR_SEND = -2,
	VDM_STATE_ERR_TMOUT = -1,
	VDM_STATE_DONE = 0,
	/* Anything >0 represents an active state */
	VDM_STATE_READY = 1,
	VDM_STATE_BUSY = 2,
	VDM_STATE_WAIT_RSP_BUSY = 3,
};

#ifdef CONFIG_USB_PD_DUAL_ROLE
/* Port dual-role state */
enum pd_dual_role_states drp_state[CONFIG_USB_PD_PORT_MAX_COUNT] = {
	[0 ...(CONFIG_USB_PD_PORT_MAX_COUNT - 1)] =
		CONFIG_USB_PD_INITIAL_DRP_STATE
};

/* Enable variable for Try.SRC states */
static bool pd_try_src_enable;
#endif

#ifdef CONFIG_USB_PD_REV30
/*
 * The spec. revision is the argument for this macro.
 *  Rev 0 (PD 1.0) - return PD_CTRL_REJECT
 *  Rev 1 (PD 2.0) - return PD_CTRL_REJECT
 *  Rev 2 (PD 3.0) - return PD_CTRL_NOT_SUPPORTED
 *
 * Note: this should only be used in locations where responding on a lower
 * revision with a Reject is valid (ex. a source refusing a PR_Swap).  For
 * other uses of Not_Supported, use PD_CTRL_NOT_SUPPORTED directly.
 */
#define NOT_SUPPORTED(r) (r < 2 ? PD_CTRL_REJECT : PD_CTRL_NOT_SUPPORTED)
#else
#define NOT_SUPPORTED(r) PD_CTRL_REJECT
#endif

#ifdef CONFIG_USB_PD_REV30
/*
 * The spec. revision is used to index into this array.
 *  Rev 0 (VDO 1.0) - return SVDM_VER_1_0
 *  Rev 1 (VDO 1.0) - return SVDM_VER_1_0
 *  Rev 2 (VDO 2.0) - return SVDM_VER_2_0
 */
static const uint8_t vdo_ver[] = { SVDM_VER_1_0, SVDM_VER_1_0, SVDM_VER_2_0 };
#define VDO_VER(v) vdo_ver[v]
#else
#define VDO_VER(v) SVDM_VER_1_0
#endif

static struct pd_protocol {
	/* current port power role (SOURCE or SINK) */
	enum pd_power_role power_role;
	/* current port data role (DFP or UFP) */
	enum pd_data_role data_role;
	/* 3-bit rolling message ID counter */
	uint8_t msg_id;
	/* port polarity */
	enum tcpc_cc_polarity polarity;
	/* PD state for port */
	enum pd_states task_state;
	/* PD state when we run state handler the last time */
	enum pd_states last_state;
	/* bool: request state change to SUSPENDED */
	uint8_t req_suspend_state;
	/* The state to go to after timeout */
	enum pd_states timeout_state;
	/* port flags, see PD_FLAGS_* */
	uint32_t flags;
	/* Timeout for the current state. Set to 0 for no timeout. */
	uint64_t timeout;
	/* Time for source recovery after hard reset */
	uint64_t src_recover;
	/* Time for CC debounce end */
	uint64_t cc_debounce;
	/* The cc state */
	enum pd_cc_states cc_state;
	/* status of last transmit */
	uint8_t tx_status;

	/* Last received */
	uint8_t last_msg_id;

	/* last requested voltage PDO index */
	int requested_idx;
#ifdef CONFIG_USB_PD_DUAL_ROLE
	/* Current limit / voltage based on the last request message */
	uint32_t curr_limit;
	uint32_t supply_voltage;
	/* Signal charging update that affects the port */
	int new_power_request;
	/* Store previously requested voltage request */
	int prev_request_mv;
	/* Time for Try.SRC states */
	uint64_t try_src_marker;
	uint64_t try_timeout;
#endif

#ifdef CONFIG_USB_PD_TCPC_LOW_POWER
	/* Time to enter low power mode */
	uint64_t low_power_time;
	/* Time to debounce exit low power mode */
	uint64_t low_power_exit_time;
	/* Tasks to notify after TCPC has been reset */
	atomic_t tasks_waiting_on_reset;
	/* Tasks preventing TCPC from entering low power mode */
	atomic_t tasks_preventing_lpm;
#endif

#ifdef CONFIG_USB_PD_DUAL_ROLE_AUTO_TOGGLE
	/*
	 * Timer for handling TOGGLE_OFF/FORCE_SINK mode when auto-toggle
	 * enabled. See drp_auto_toggle_next_state() for details.
	 */
	uint64_t drp_sink_time;
#endif

	/*
	 * Time to ignore Vbus absence due to external IC debounce detection
	 * logic immediately after a power role swap.
	 */
	uint64_t vbus_debounce_time;

	/* PD state for Vendor Defined Messages */
	enum vdm_states vdm_state;
	/* Timeout for the current vdm state.  Set to 0 for no timeout. */
	timestamp_t vdm_timeout;
	/* next Vendor Defined Message to send */
	uint32_t vdo_data[VDO_MAX_SIZE];
	/* type of transmit message (SOP/SOP'/SOP'') */
	enum tcpci_msg_type xmit_type;
	uint8_t vdo_count;
	/* VDO to retry if UFP responder replied busy. */
	uint32_t vdo_retry;

	/* Attached ChromeOS device id, RW hash, and current RO / RW image */
	uint16_t dev_id;
	uint32_t dev_rw_hash[PD_RW_HASH_SIZE / 4];
	enum ec_image current_image;
#ifdef CONFIG_USB_PD_REV30
	/* protocol revision */
	uint8_t rev;
#endif
	/*
	 * Some port partners are really chatty after an explicit contract is
	 * established.  Therefore, we allow this time for the port partner to
	 * send any messages in order to avoid a collision of sending messages
	 * of our own.
	 */
	uint64_t ready_state_holdoff_timer;
	/*
	 * PD 2.0 spec, section 6.5.11.1
	 * When we can give up on a HARD_RESET transmission.
	 */
	uint64_t hard_reset_complete_timer;
} pd[CONFIG_USB_PD_PORT_MAX_COUNT];

#ifdef CONFIG_USB_PD_TCPMV1_DEBUG
static const char *const pd_state_names[] = {
	"DISABLED",
	"SUSPENDED",
	"SNK_DISCONNECTED",
	"SNK_DISCONNECTED_DEBOUNCE",
	"SNK_HARD_RESET_RECOVER",
	"SNK_DISCOVERY",
	"SNK_REQUESTED",
	"SNK_TRANSITION",
	"SNK_READY",
	"SNK_SWAP_INIT",
	"SNK_SWAP_SNK_DISABLE",
	"SNK_SWAP_SRC_DISABLE",
	"SNK_SWAP_STANDBY",
	"SNK_SWAP_COMPLETE",
	"SRC_DISCONNECTED",
	"SRC_DISCONNECTED_DEBOUNCE",
	"SRC_HARD_RESET_RECOVER",
	"SRC_STARTUP",
	"SRC_DISCOVERY",
	"SRC_NEGOCIATE",
	"SRC_ACCEPTED",
	"SRC_POWERED",
	"SRC_TRANSITION",
	"SRC_READY",
	"SRC_GET_SNK_CAP",
	"DR_SWAP",
	"SRC_SWAP_INIT",
	"SRC_SWAP_SNK_DISABLE",
	"SRC_SWAP_SRC_DISABLE",
	"SRC_SWAP_STANDBY",
	"VCONN_SWAP_SEND",
	"VCONN_SWAP_INIT",
	"VCONN_SWAP_READY",
	"SOFT_RESET",
	"HARD_RESET_SEND",
	"HARD_RESET_EXECUTE",
	"BIST_RX",
	"BIST_TX",
	"DRP_AUTO_TOGGLE",
};
BUILD_ASSERT(ARRAY_SIZE(pd_state_names) == PD_STATE_COUNT);
#endif

int pd_comm_is_enabled(int port)
{
#ifdef CONFIG_COMMON_RUNTIME
	return pd_comm_enabled[port];
#else
	return 1;
#endif
}

bool pd_alt_mode_capable(int port)
{
	/*
	 * PD is alternate mode capable only if PD communication is enabled and
	 * the port is not suspended.
	 */
	return pd_comm_is_enabled(port) &&
	       !(pd[port].task_state == PD_STATE_SUSPENDED);
}

static inline void set_state_timeout(int port, uint64_t timeout,
				     enum pd_states timeout_state)
{
	pd[port].timeout = timeout;
	pd[port].timeout_state = timeout_state;
}

int pd_get_rev(int port, enum tcpci_msg_type type)
{
#ifdef CONFIG_USB_PD_REV30
	/* TCPMv1 Only stores PD revision for SOP and SOP' types */
	ASSERT(type < NUM_SOP_STAR_TYPES - 1);

	return pd[port].rev;
#else
	return PD_REV20;
#endif
}

int pd_get_vdo_ver(int port, enum tcpci_msg_type type)
{
#ifdef CONFIG_USB_PD_REV30
	return vdo_ver[pd[port].rev];
#else
	return SVDM_VER_1_0;
#endif
}

/* Return flag for pd state is connected */
int pd_is_connected(int port)
{
	if (pd[port].task_state == PD_STATE_DISABLED)
		return 0;

#ifdef CONFIG_USB_PD_DUAL_ROLE_AUTO_TOGGLE
	if (pd[port].task_state == PD_STATE_DRP_AUTO_TOGGLE)
		return 0;
#endif

	return DUAL_ROLE_IF_ELSE(
		port,
		/* sink */
		pd[port].task_state != PD_STATE_SNK_DISCONNECTED &&
			pd[port].task_state !=
				PD_STATE_SNK_DISCONNECTED_DEBOUNCE,
		/* source */
		pd[port].task_state != PD_STATE_SRC_DISCONNECTED &&
			pd[port].task_state !=
				PD_STATE_SRC_DISCONNECTED_DEBOUNCE);
}

/* Return true if partner port is known to be PD capable. */
bool pd_capable(int port)
{
	return !!(pd[port].flags & PD_FLAGS_PREVIOUS_PD_CONN);
}

/*
 * Return true if partner port is capable of communication over USB data
 * lines.
 */
bool pd_get_partner_usb_comm_capable(int port)
{
	return !!(pd[port].flags & PD_FLAGS_PARTNER_USB_COMM);
}

#ifdef CONFIG_USB_PD_DUAL_ROLE
void pd_vbus_low(int port)
{
	pd[port].flags &= ~PD_FLAGS_VBUS_NEVER_LOW;
}
#endif

#ifdef CONFIG_USBC_VCONN
static void set_vconn(int port, int enable)
{
	/*
	 * Disable PPC Vconn first then TCPC in case the voltage feeds back
	 * to TCPC and damages.
	 */
	if (IS_ENABLED(CONFIG_USBC_PPC_VCONN) && !enable)
		ppc_set_vconn(port, 0);

	/*
	 * Some TCPCs/PPC combinations can trigger OVP if the TCPC doesn't
	 * source VCONN. This happens if the TCPC will trip OVP with 5V, and the
	 * PPC doesn't isolate the TCPC from VCONN when sourcing. But, some PPCs
	 * which do isolate the TCPC can't handle 5V on its host-side CC pins,
	 * so the TCPC shouldn't source VCONN in those cases.
	 *
	 * In the first case, both TCPC and PPC will potentially source Vconn,
	 * but that should be okay since Vconn has "make before break"
	 * electrical requirements when swapping anyway.
	 *
	 * See b/72961003 and b/180973460
	 */
	tcpm_set_vconn(port, enable);

	if (IS_ENABLED(CONFIG_USBC_PPC_VCONN) && enable)
		ppc_set_vconn(port, 1);
}
#endif /* defined(CONFIG_USBC_VCONN) */

#ifdef CONFIG_USB_PD_REV30
/* Note: rp should be set to either SINK_TX_OK or SINK_TX_NG */
static void sink_can_xmit(int port, int rp)
{
	tcpm_select_rp_value(port, rp);
	tcpm_set_cc(port, TYPEC_CC_RP);

	/* We must wait tSinkTx before sending a message */
	if (rp == SINK_TX_NG)
		crec_usleep(PD_T_SINK_TX);
}
#endif

#ifdef CONFIG_USB_PD_TCPC_LOW_POWER

/* 10 ms is enough time for any TCPC transaction to complete. */
#define PD_LPM_DEBOUNCE_US (10 * MSEC)
/* 25 ms on LPM exit to ensure TCPC is settled */
#define PD_LPM_EXIT_DEBOUNCE_US (25 * MSEC)

/* This is only called from the PD tasks that owns the port. */
static void handle_device_access(int port)
{
	/* This should only be called from the PD task */
	assert(port == TASK_ID_TO_PD_PORT(task_get_current()));

	pd[port].low_power_time = get_time().val + PD_LPM_DEBOUNCE_US;
	if (pd[port].flags & PD_FLAGS_LPM_ENGAGED) {
		tcpc_prints("Exit Low Power Mode", port);
		pd[port].flags &=
			~(PD_FLAGS_LPM_ENGAGED | PD_FLAGS_LPM_REQUESTED);
		pd[port].flags |= PD_FLAGS_LPM_EXIT;

		pd[port].low_power_exit_time =
			get_time().val + PD_LPM_EXIT_DEBOUNCE_US;
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
	    (pd[port].flags & PD_FLAGS_LPM_TRANSITION))
		return 0;

	return pd[port].flags & PD_FLAGS_LPM_ENGAGED;
}

static int reset_device_and_notify(int port)
{
	int rv;
	int task, waiting_tasks;

	/* This should only be called from the PD task */
	assert(port == TASK_ID_TO_PD_PORT(task_get_current()));

	pd[port].flags |= PD_FLAGS_LPM_TRANSITION;
	rv = tcpm_init(port);
	pd[port].flags &= ~PD_FLAGS_LPM_TRANSITION;

	if (rv == EC_SUCCESS)
		tcpc_prints("init ready", port);
	else
		tcpc_prints("init failed!", port);

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

	waiting_tasks = atomic_clear(&pd[port].tasks_waiting_on_reset);

	/*
	 * Now that we are done waking up the device, handle device access
	 * manually because we ignored it while waking up device.
	 */
	handle_device_access(port);

	/* Clear SW LPM state; the state machine will set it again if needed */
	pd[port].flags &= ~PD_FLAGS_LPM_REQUESTED;

	/* Wake up all waiting tasks. */
	while (waiting_tasks) {
		task = __fls(waiting_tasks);
		waiting_tasks &= ~BIT(task);
		task_set_event(task, TASK_EVENT_PD_AWAKE);
	}

	return rv;
}

static void pd_wait_for_wakeup(int port)
{
	if (port == TASK_ID_TO_PD_PORT(task_get_current())) {
		/* If we are in the PD task, we can directly reset */
		reset_device_and_notify(port);
	} else {
		/* Otherwise, we need to wait for the TCPC reset to complete */
		atomic_or(&pd[port].tasks_waiting_on_reset,
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

void pd_wait_exit_low_power(int port)
{
	if (pd_device_in_low_power(port))
		pd_wait_for_wakeup(port);
}

/*
 * This can be called from any task. If we are in the PD task, we can handle
 * immediately. Otherwise, we need to notify the PD task via event.
 */
void pd_device_accessed(int port)
{
	if (port == TASK_ID_TO_PD_PORT(task_get_current())) {
		/* Ignore any access to device while it is waking up */
		if (pd[port].flags & PD_FLAGS_LPM_TRANSITION)
			return;

		handle_device_access(port);
	} else {
		task_set_event(PD_PORT_TO_TASK_ID(port),
			       PD_EVENT_DEVICE_ACCESSED);
	}
}

void pd_prevent_low_power_mode(int port, int prevent)
{
	const int current_task_mask = (1 << task_get_current());

	if (prevent)
		atomic_or(&pd[port].tasks_preventing_lpm, current_task_mask);
	else
		atomic_clear_bits(&pd[port].tasks_preventing_lpm,
				  current_task_mask);
}

/* This is only called from the PD tasks that owns the port. */
static void exit_low_power_mode(int port)
{
	if (pd[port].flags & PD_FLAGS_LPM_ENGAGED)
		reset_device_and_notify(port);
	else
		pd[port].flags &= ~PD_FLAGS_LPM_REQUESTED;
}

#else /* !CONFIG_USB_PD_TCPC_LOW_POWER */

/* We don't need to notify anyone if low power mode isn't involved. */
static int reset_device_and_notify(int port)
{
	const int rv = tcpm_init(port);

	if (rv == EC_SUCCESS)
		tcpc_prints("init ready", port);
	else
		tcpc_prints("init failed!", port);

	return rv;
}

#endif /* CONFIG_USB_PD_TCPC_LOW_POWER */

/**
 * Invalidate last message received at the port when the port gets disconnected
 * or reset(soft/hard). This is used to identify and handle the duplicate
 * messages.
 *
 * @param port USB PD TCPC port number
 */
static void invalidate_last_message_id(int port)
{
	pd[port].last_msg_id = INVALID_MSG_ID_COUNTER;
}

static bool consume_sop_repeat_message(int port, uint8_t msg_id)
{
	if (pd[port].last_msg_id != msg_id) {
		pd[port].last_msg_id = msg_id;
		return false;
	}
	CPRINTF("C%d Repeat msg_id %d\n", port, msg_id);
	return true;
}

/**
 * Identify and drop any duplicate messages received at the port.
 *
 * @param port USB PD TCPC port number
 * @param msg_header Message Header containing the RX message ID
 * @return True if the received message is a duplicate one, False otherwise.
 */
static bool consume_repeat_message(int port, uint32_t msg_header)
{
	uint8_t msg_id = PD_HEADER_ID(msg_header);

	/* If repeat message ignore, except softreset control request. */
	if (PD_HEADER_TYPE(msg_header) == PD_CTRL_SOFT_RESET &&
	    PD_HEADER_CNT(msg_header) == 0) {
		return false;
	} else {
		return consume_sop_repeat_message(port, msg_id);
	}
}

/**
 * Returns true if the port is currently in the try src state.
 */
static inline int is_try_src(int port)
{
	return pd[port].flags & PD_FLAGS_TRY_SRC;
}

static inline void set_state(int port, enum pd_states next_state)
{
	enum pd_states last_state = pd[port].task_state;
#if defined(CONFIG_LOW_POWER_IDLE) && !defined(CONFIG_USB_PD_TCPC_ON_CHIP)
	int i;
#endif
	int not_auto_toggling = 1;

	set_state_timeout(port, 0, 0);
	pd[port].task_state = next_state;

	if (last_state == next_state)
		return;

#if defined(CONFIG_USBC_PPC) && defined(CONFIG_USB_PD_DUAL_ROLE_AUTO_TOGGLE)
	/* If we're entering DRP_AUTO_TOGGLE, there is no sink connected. */
	if (next_state == PD_STATE_DRP_AUTO_TOGGLE) {
		ppc_dev_is_connected(port, PPC_DEV_DISCONNECTED);
		/* Disable Auto Discharge Disconnect */
		tcpm_enable_auto_discharge_disconnect(port, 0);

		if (IS_ENABLED(CONFIG_USBC_OCP)) {
			usbc_ocp_snk_is_connected(port, false);
			/*
			 * Clear the overcurrent event counter
			 * since we've detected a disconnect.
			 */
			usbc_ocp_clear_event_counter(port);
		}
	}
#endif /* CONFIG_USBC_PPC &&  CONFIG_USB_PD_DUAL_ROLE_AUTO_TOGGLE */

#ifdef CONFIG_USB_PD_DUAL_ROLE
#ifdef CONFIG_USB_PD_DUAL_ROLE_AUTO_TOGGLE
	if (last_state != PD_STATE_DRP_AUTO_TOGGLE)
		/* Clear flag to allow DRP auto toggle when possible */
		pd[port].flags &= ~PD_FLAGS_TCPC_DRP_TOGGLE;
	else
		/* This is an auto toggle instead of disconnect */
		not_auto_toggling = 0;
#endif

	/* Ignore dual-role toggling between sink and source */
	if ((last_state == PD_STATE_SNK_DISCONNECTED &&
	     next_state == PD_STATE_SRC_DISCONNECTED) ||
	    (last_state == PD_STATE_SRC_DISCONNECTED &&
	     next_state == PD_STATE_SNK_DISCONNECTED))
		return;

	if (next_state == PD_STATE_SRC_DISCONNECTED ||
	    next_state == PD_STATE_SNK_DISCONNECTED) {
#ifdef CONFIG_USBC_PPC
		enum tcpc_cc_voltage_status cc1, cc2;

		tcpm_get_cc(port, &cc1, &cc2);
		/*
		 * Neither a debug accessory nor UFP attached.
		 * Tell the PPC module that there is no device connected.
		 */
		if (!cc_is_at_least_one_rd(cc1, cc2)) {
			ppc_dev_is_connected(port, PPC_DEV_DISCONNECTED);

			if (IS_ENABLED(CONFIG_USBC_OCP)) {
				usbc_ocp_snk_is_connected(port, false);
				/*
				 * Clear the overcurrent event counter
				 * since we've detected a disconnect.
				 */
				usbc_ocp_clear_event_counter(port);
			}
		}
#endif /* CONFIG_USBC_PPC */

		/* Clear the holdoff timer since the port is disconnected. */
		pd[port].ready_state_holdoff_timer = 0;

		/*
		 * We should not clear any flags when transitioning back to the
		 * disconnected state from the debounce state as the two states
		 * here are really the same states in the state diagram.
		 */
		if (last_state != PD_STATE_SNK_DISCONNECTED_DEBOUNCE &&
		    last_state != PD_STATE_SRC_DISCONNECTED_DEBOUNCE) {
			pd[port].flags &= ~PD_FLAGS_RESET_ON_DISCONNECT_MASK;
		}

		/* Clear the input current limit */
		pd_set_input_current_limit(port, 0, 0);
#ifdef CONFIG_CHARGE_MANAGER
		typec_set_input_current_limit(port, 0, 0);
		charge_manager_set_ceil(port, CEIL_REQUESTOR_PD,
					CHARGE_CEIL_NONE);
#endif
#ifdef CONFIG_BC12_DETECT_DATA_ROLE_TRIGGER
		/*
		 * When data role set events are used to enable BC1.2, then CC
		 * detach events are used to notify BC1.2 that it can be powered
		 * down.
		 */
		usb_charger_task_set_event(port, USB_CHG_EVENT_CC_OPEN);
#endif /* CONFIG_BC12_DETECT_DATA_ROLE_TRIGGER */
#ifdef CONFIG_USBC_VCONN
		set_vconn(port, 0);
#endif /* defined(CONFIG_USBC_VCONN) */
		pd_update_saved_port_flags(port, PD_BBRMFLG_EXPLICIT_CONTRACT,
					   0);
#else /* CONFIG_USB_PD_DUAL_ROLE */
	if (next_state == PD_STATE_SRC_DISCONNECTED) {
#ifdef CONFIG_USBC_VCONN
		set_vconn(port, 0);
#endif /* CONFIG_USBC_VCONN */
#endif /* !CONFIG_USB_PD_DUAL_ROLE */
		/* If we are source, make sure VBUS is off and restore RP */
		if (pd[port].power_role == PD_ROLE_SOURCE) {
			/* Restore non-active ports to CONFIG_USB_PD_PULLUP */
			pd_power_supply_reset(port);
			tcpm_set_cc(port, TYPEC_CC_RP);
		}
#ifdef CONFIG_USB_PD_REV30
		/* Adjust rev to highest level*/
		pd[port].rev = PD_REV30;
#endif
		pd[port].dev_id = 0;
#ifdef CONFIG_CHARGE_MANAGER
		charge_manager_update_dualrole(port, CAP_UNKNOWN);
#endif
#ifdef CONFIG_USB_PD_ALT_MODE_DFP
		if (pd_dfp_exit_mode(port, TCPCI_MSG_SOP, 0, 0))
			usb_mux_set_safe_mode(port);
#endif
		/*
		 * Indicate that the port is disconnected by setting role to
		 * DFP as SoCs have special signals when they are the UFP ports
		 * (e.g. OTG signals)
		 */
		pd_execute_data_swap(port, PD_ROLE_DFP);
#ifdef CONFIG_USBC_SS_MUX
		usb_mux_set(port, USB_PD_MUX_NONE, USB_SWITCH_DISCONNECT,
			    pd[port].polarity);
#endif
		/* Disable TCPC RX */
		tcpm_set_rx_enable(port, 0);

		/* Invalidate message IDs. */
		invalidate_last_message_id(port);

		if (not_auto_toggling)
			/* Disable Auto Discharge Disconnect */
			tcpm_enable_auto_discharge_disconnect(port, 0);

		/* detect USB PD cc disconnect */
		if (IS_ENABLED(CONFIG_COMMON_RUNTIME))
			hook_notify(HOOK_USB_PD_DISCONNECT);
	}

#ifdef CONFIG_USB_PD_REV30
	/* Upon entering SRC_READY, it is safe for the sink to transmit */
	if (next_state == PD_STATE_SRC_READY) {
		if (pd[port].rev == PD_REV30 &&
		    pd[port].flags & PD_FLAGS_EXPLICIT_CONTRACT)
			sink_can_xmit(port, SINK_TX_OK);
	}
#endif

#if defined(CONFIG_LOW_POWER_IDLE) && !defined(CONFIG_USB_PD_TCPC_ON_CHIP)
	/* If a PD device is attached then disable deep sleep */
	for (i = 0; i < board_get_usb_pd_port_count(); i++) {
		if (pd_capable(i))
			break;
	}
	if (i == board_get_usb_pd_port_count())
		enable_sleep(SLEEP_MASK_USB_PD);
	else
		disable_sleep(SLEEP_MASK_USB_PD);
#endif

#ifdef CONFIG_USB_PD_TCPMV1_DEBUG
	if (debug_level > 0)
		CPRINTF("C%d st%d %s\n", port, next_state,
			pd_state_names[next_state]);
	else
#endif
		CPRINTF("C%d st%d\n", port, next_state);
}

/* increment message ID counter */
static void inc_id(int port)
{
	pd[port].msg_id = (pd[port].msg_id + 1) & PD_MESSAGE_ID_COUNT;
}

void pd_transmit_complete(int port, int status)
{
	if (status == TCPC_TX_COMPLETE_SUCCESS)
		inc_id(port);

	pd[port].tx_status = status;
	task_set_event(PD_PORT_TO_TASK_ID(port), PD_EVENT_TX);
}

static int pd_transmit(int port, enum tcpci_msg_type type, uint16_t header,
		       const uint32_t *data, enum ams_seq ams)
{
	int evt;
	int res;
#ifdef CONFIG_USB_PD_REV30
	int sink_ng = 0;
#endif

	/* If comms are disabled, do not transmit, return error */
	if (!pd_comm_is_enabled(port))
		return -1;

	/* Don't try to transmit anything until we have processed
	 * all RX messages.
	 */
	if (tcpm_has_pending_message(port))
		return -1;

#ifdef CONFIG_USB_PD_REV30
	/* Source-coordinated collision avoidance */
	/*
	 * USB PD Rev 3.0, Version 2.0: Section 2.7.3.2
	 * Collision Avoidance - Protocol Layer
	 *
	 * In order to avoid message collisions due to asynchronous Messaging
	 * sent from the Sink, the Source sets Rp to SinkTxOk (3A) to indicate
	 * to the Sink that it is ok to initiate an AMS. When the Source wishes
	 * to initiate an AMS, it sets Rp to SinkTxNG (1.5A).
	 * When the Sink detects that Rp is set to SinkTxOk, it May initiate an
	 * AMS. When the Sink detects that Rp is set to SinkTxNG it Shall Not
	 * initiate an AMS and Shall only send Messages that are part of an AMS
	 * the Source has initiated.
	 * Note that this restriction applies to SOP* AMS’s i.e. for both Port
	 * to Port and Port to Cable Plug communications.
	 *
	 * This starts after an Explicit Contract is in place (see section 2.5.2
	 * SOP* Collision Avoidance).
	 *
	 * Note: a Sink can still send Hard Reset signaling at any time.
	 */
	if ((pd[port].rev == PD_REV30) && ams == AMS_START &&
	    (pd[port].flags & PD_FLAGS_EXPLICIT_CONTRACT)) {
		if (pd[port].power_role == PD_ROLE_SOURCE) {
			/*
			 * Inform Sink that it can't transmit. If a sink
			 * transmission is in progress and a collision occurs,
			 * a reset is generated. This should be rare because
			 * all extended messages are chunked. This effectively
			 * defaults to PD REV 2.0 collision avoidance.
			 */
			sink_can_xmit(port, SINK_TX_NG);
			sink_ng = 1;
		} else if (type != TCPCI_MSG_TX_HARD_RESET) {
			enum tcpc_cc_voltage_status cc1, cc2;

			tcpm_get_cc(port, &cc1, &cc2);
			if (cc1 == TYPEC_CC_VOLT_RP_1_5 ||
			    cc2 == TYPEC_CC_VOLT_RP_1_5) {
				/* Sink can't transmit now. */
				/* Return failure, pd_task can retry later */
				return -1;
			}
		}
	}
#endif
	tcpm_transmit(port, type, header, data);

	/* Wait until TX is complete */
	evt = task_wait_event_mask(PD_EVENT_TX, PD_T_TCPC_TX_TIMEOUT);

	if (evt & TASK_EVENT_TIMER)
		return -1;

	/* TODO: give different error condition for failed vs discarded */
	res = pd[port].tx_status == TCPC_TX_COMPLETE_SUCCESS ? 1 : -1;

#ifdef CONFIG_USB_PD_REV30
	/* If the AMS transaction failed to start, reset CC to OK */
	if (res < 0 && sink_ng)
		sink_can_xmit(port, SINK_TX_OK);
#endif
	return res;
}

static void pd_update_roles(int port)
{
	/* Notify TCPC of role update */
	tcpm_set_msg_header(port, pd[port].power_role, pd[port].data_role);
}

static int send_control(int port, int type)
{
	int bit_len;
	uint16_t header = PD_HEADER(type, pd[port].power_role,
				    pd[port].data_role, pd[port].msg_id, 0,
				    pd_get_rev(port, TCPCI_MSG_SOP), 0);
	/*
	 * For PD 3.0, collision avoidance logic needs to know if this message
	 * will begin a new Atomic Message Sequence (AMS)
	 */
	enum ams_seq ams = ((1 << type) & PD_CTRL_AMS_START_MASK) ?
				   AMS_START :
				   AMS_RESPONSE;

	bit_len = pd_transmit(port, TCPCI_MSG_SOP, header, NULL, ams);
	if (debug_level >= 2)
		CPRINTF("C%d CTRL[%d]>%d\n", port, type, bit_len);

	return bit_len;
}

/*
 * Note: Source capabilities may either be in an existing AMS (ex. as a
 * response to Get_Source_Cap), or the beginning of an AMS for a power
 * negotiation.
 */
static int send_source_cap(int port, enum ams_seq ams)
{
	int bit_len;
#if defined(CONFIG_USB_PD_DYNAMIC_SRC_CAP) || \
	defined(CONFIG_USB_PD_MAX_SINGLE_SOURCE_CURRENT)
	const uint32_t *src_pdo;
	const int src_pdo_cnt = charge_manager_get_source_pdo(&src_pdo, port);
#else
	const uint32_t *src_pdo = pd_src_pdo;
	const int src_pdo_cnt = pd_src_pdo_cnt;
#endif
	uint16_t header;

	if (src_pdo_cnt == 0)
		/* No source capabilities defined, sink only */
		header = PD_HEADER(PD_CTRL_REJECT, pd[port].power_role,
				   pd[port].data_role, pd[port].msg_id, 0,
				   pd_get_rev(port, TCPCI_MSG_SOP), 0);
	else
		header = PD_HEADER(PD_DATA_SOURCE_CAP, pd[port].power_role,
				   pd[port].data_role, pd[port].msg_id,
				   src_pdo_cnt, pd_get_rev(port, TCPCI_MSG_SOP),
				   0);

	bit_len = pd_transmit(port, TCPCI_MSG_SOP, header, src_pdo, ams);
	if (debug_level >= 2)
		CPRINTF("C%d srcCAP>%d\n", port, bit_len);

	return bit_len;
}

#ifdef CONFIG_USB_PD_REV30
static int send_battery_cap(int port, uint32_t *payload)
{
	int bit_len;
	uint16_t msg[6] = { 0, 0, 0, 0, 0, 0 };
	uint16_t header = PD_HEADER(PD_EXT_BATTERY_CAP, pd[port].power_role,
				    pd[port].data_role, pd[port].msg_id,
				    3, /* Number of Data Objects */
				    pd[port].rev, 1 /* This is an exteded
						       message */
	);

	/* Set extended header */
	msg[0] = PD_EXT_HEADER(0, /* Chunk Number */
			       0, /* Request Chunk */
			       9 /* Data Size in bytes */
	);
	/* Set VID */
	msg[1] = USB_VID_GOOGLE;

	/* Set PID */
	msg[2] = CONFIG_USB_PID;

	if (battery_is_present()) {
		/*
		 * We only have one fixed battery,
		 * so make sure batt cap ref is 0.
		 */
		if (BATT_CAP_REF(payload[0]) != 0) {
			/* Invalid battery reference */
			msg[5] = 1;
		} else {
			uint32_t v;
			uint32_t c;

			/*
			 * The Battery Design Capacity field shall return the
			 * Battery’s design capacity in tenths of Wh. If the
			 * Battery is Hot Swappable and is not present, the
			 * Battery Design Capacity field shall be set to 0. If
			 * the Battery is unable to report its Design Capacity,
			 * it shall return 0xFFFF
			 */
			msg[3] = 0xffff;

			/*
			 * The Battery Last Full Charge Capacity field shall
			 * return the Battery’s last full charge capacity in
			 * tenths of Wh. If the Battery is Hot Swappable and
			 * is not present, the Battery Last Full Charge Capacity
			 * field shall be set to 0. If the Battery is unable to
			 * report its Design Capacity, the Battery Last Full
			 * Charge Capacity field shall be set to 0xFFFF.
			 */
			msg[4] = 0xffff;

			if (battery_design_voltage(&v) == 0) {
				if (battery_design_capacity(&c) == 0) {
					/*
					 * Wh = (c * v) / 1000000
					 * 10th of a Wh = Wh * 10
					 */
					msg[3] = DIV_ROUND_NEAREST((c * v),
								   100000);
				}

				if (battery_full_charge_capacity(&c) == 0) {
					/*
					 * Wh = (c * v) / 1000000
					 * 10th of a Wh = Wh * 10
					 */
					msg[4] = DIV_ROUND_NEAREST((c * v),
								   100000);
				}
			}
		}
	}

	bit_len = pd_transmit(port, TCPCI_MSG_SOP, header, (uint32_t *)msg,
			      AMS_RESPONSE);
	if (debug_level >= 2)
		CPRINTF("C%d batCap>%d\n", port, bit_len);
	return bit_len;
}

static int send_battery_status(int port, uint32_t *payload)
{
	int bit_len;
	uint32_t msg = 0;
	uint16_t header = PD_HEADER(PD_DATA_BATTERY_STATUS, pd[port].power_role,
				    pd[port].data_role, pd[port].msg_id,
				    1, /* Number of Data Objects */
				    pd[port].rev, 0 /* This is NOT an extended
						       message */
	);

	if (battery_is_present()) {
		/*
		 * We only have one fixed battery,
		 * so make sure batt cap ref is 0.
		 */
		if (BATT_CAP_REF(payload[0]) != 0) {
			/* Invalid battery reference */
			msg |= BSDO_INVALID;
		} else {
			uint32_t v;
			uint32_t c;

			if (battery_design_voltage(&v) != 0 ||
			    battery_remaining_capacity(&c) != 0) {
				msg |= BSDO_CAP(BSDO_CAP_UNKNOWN);
			} else {
				/*
				 * Wh = (c * v) / 1000000
				 * 10th of a Wh = Wh * 10
				 */
				msg |= BSDO_CAP(
					DIV_ROUND_NEAREST((c * v), 100000));
			}

			/* Battery is present */
			msg |= BSDO_PRESENT;

			/*
			 * For drivers that are not smart battery compliant,
			 * battery_status() returns EC_ERROR_UNIMPLEMENTED and
			 * the battery is assumed to be idle.
			 */
			if (battery_status(&c) != 0) {
				msg |= BSDO_IDLE; /* assume idle */
			} else {
				if (c & STATUS_FULLY_CHARGED)
					/* Fully charged */
					msg |= BSDO_IDLE;
				else if (c & STATUS_DISCHARGING)
					/* Discharging */
					msg |= BSDO_DISCHARGING;
				/* else battery is charging.*/
			}
		}
	} else {
		msg = BSDO_CAP(BSDO_CAP_UNKNOWN);
	}

	bit_len = pd_transmit(port, TCPCI_MSG_SOP, header, &msg, AMS_RESPONSE);
	if (debug_level >= 2)
		CPRINTF("C%d batStat>%d\n", port, bit_len);

	return bit_len;
}
#endif

#ifdef CONFIG_USB_PD_DUAL_ROLE
static void send_sink_cap(int port)
{
	int bit_len;
	uint16_t header = PD_HEADER(PD_DATA_SINK_CAP, pd[port].power_role,
				    pd[port].data_role, pd[port].msg_id,
				    pd_snk_pdo_cnt,
				    pd_get_rev(port, TCPCI_MSG_SOP), 0);

	bit_len = pd_transmit(port, TCPCI_MSG_SOP, header, pd_snk_pdo,
			      AMS_RESPONSE);
	if (debug_level >= 2)
		CPRINTF("C%d snkCAP>%d\n", port, bit_len);
}

static int send_request(int port, uint32_t rdo)
{
	int bit_len;
	uint16_t header = PD_HEADER(PD_DATA_REQUEST, pd[port].power_role,
				    pd[port].data_role, pd[port].msg_id, 1,
				    pd_get_rev(port, TCPCI_MSG_SOP), 0);

	/* Note: ams will need to be AMS_START if used for PPS keep alive */
	bit_len = pd_transmit(port, TCPCI_MSG_SOP, header, &rdo, AMS_RESPONSE);
	if (debug_level >= 2)
		CPRINTF("C%d REQ>%d\n", port, bit_len);

	return bit_len;
}

#endif /* CONFIG_USB_PD_DUAL_ROLE */

#ifdef CONFIG_COMMON_RUNTIME
static int send_bist_cmd(int port)
{
	/* currently only support sending bist carrier 2 */
	uint32_t bdo = BDO(BDO_MODE_CARRIER2, 0);
	int bit_len;
	uint16_t header = PD_HEADER(PD_DATA_BIST, pd[port].power_role,
				    pd[port].data_role, pd[port].msg_id, 1,
				    pd_get_rev(port, TCPCI_MSG_SOP), 0);

	bit_len = pd_transmit(port, TCPCI_MSG_SOP, header, &bdo, AMS_START);
	CPRINTF("C%d BIST>%d\n", port, bit_len);

	return bit_len;
}
#endif

static void queue_vdm(int port, uint32_t *header, const uint32_t *data,
		      int data_cnt, enum tcpci_msg_type type)
{
	pd[port].vdo_count = data_cnt + 1;
	pd[port].vdo_data[0] = header[0];
	pd[port].xmit_type = type;
	memcpy(&pd[port].vdo_data[1], data, sizeof(uint32_t) * data_cnt);
	/* Set ready, pd task will actually send */
	pd[port].vdm_state = VDM_STATE_READY;
}

/* ----------------- Vendor Defined Messages ------------------ */
__overridable int pd_custom_vdm(int port, int cnt, uint32_t *payload,
				uint32_t **rpayload)
{
	int cmd = PD_VDO_CMD(payload[0]);
	uint16_t dev_id = 0;
	int is_rw, is_latest;

	/* make sure we have some payload */
	if (cnt == 0)
		return 0;

	/* Only handle custom requests for SVID Google */
	if (PD_VDO_VID(*payload) != USB_VID_GOOGLE)
		return 0;

	switch (cmd) {
	case VDO_CMD_VERSION:
		/* guarantee last byte of payload is null character */
		*(payload + cnt - 1) = 0;
		CPRINTF("version: %s\n", (char *)(payload + 1));
		break;
	case VDO_CMD_READ_INFO:
	case VDO_CMD_SEND_INFO:
		/* copy hash */
		if (cnt == 7) {
			dev_id = VDO_INFO_HW_DEV_ID(payload[6]);
			is_rw = VDO_INFO_IS_RW(payload[6]);

			is_latest = pd_dev_store_rw_hash(
				port, dev_id, payload + 1,
				is_rw ? EC_IMAGE_RW : EC_IMAGE_RO);

			/*
			 * Send update host event unless our RW hash is
			 * already known to be the latest update RW.
			 */
			if (!is_rw || !is_latest)
				pd_send_host_event(PD_EVENT_UPDATE_DEVICE);

			CPRINTF("DevId:%d.%d SW:%d RW:%d\n",
				HW_DEV_ID_MAJ(dev_id), HW_DEV_ID_MIN(dev_id),
				VDO_INFO_SW_DBG_VER(payload[6]), is_rw);
		} else if (cnt == 6) {
			/* really old devices don't have last byte */
			pd_dev_store_rw_hash(port, dev_id, payload + 1,
					     EC_IMAGE_UNKNOWN);
		}
		break;
	case VDO_CMD_CURRENT:
		CPRINTF("Current: %dmA\n", payload[1]);
		break;
	case VDO_CMD_FLIP:
		if (IS_ENABLED(CONFIG_USBC_SS_MUX))
			usb_mux_flip(port);
		break;
#ifdef CONFIG_USB_PD_LOGGING
	case VDO_CMD_GET_LOG:
		pd_log_recv_vdm(port, cnt, payload);
		break;
#endif /* CONFIG_USB_PD_LOGGING */
	}

	return 0;
}

static void handle_vdm_request(int port, int cnt, uint32_t *payload,
			       uint32_t head)
{
	int rlen = 0;
	uint32_t *rdata;
	enum tcpci_msg_type rtype = TCPCI_MSG_SOP;

	if (pd[port].vdm_state == VDM_STATE_BUSY) {
		/* If UFP responded busy retry after timeout */
		if (PD_VDO_CMDT(payload[0]) == CMDT_RSP_BUSY) {
			pd[port].vdm_timeout.val =
				get_time().val + PD_T_VDM_BUSY;
			pd[port].vdm_state = VDM_STATE_WAIT_RSP_BUSY;
			pd[port].vdo_retry = (payload[0] & ~VDO_CMDT_MASK) |
					     CMDT_INIT;
			return;
		} else {
			pd[port].vdm_state = VDM_STATE_DONE;
#ifdef CONFIG_USB_PD_REV30
			if (pd[port].rev == PD_REV30 &&
			    pd[port].power_role == PD_ROLE_SOURCE &&
			    pd[port].flags & PD_FLAGS_EXPLICIT_CONTRACT)
				sink_can_xmit(port, SINK_TX_OK);
#endif
		}
	}

	if (PD_VDO_SVDM(payload[0]))
		rlen = pd_svdm(port, cnt, payload, &rdata, head, &rtype);
	else
		rlen = pd_custom_vdm(port, cnt, payload, &rdata);

	if (rlen > 0) {
		queue_vdm(port, rdata, &rdata[1], rlen - 1, rtype);
		return;
	}

	if (debug_level >= 2)
		CPRINTF("C%d Unhandled VDM VID %04x CMD %04x\n", port,
			PD_VDO_VID(payload[0]), payload[0] & 0xFFFF);
}

bool pd_is_disconnected(int port)
{
	return pd[port].task_state == PD_STATE_SRC_DISCONNECTED
#ifdef CONFIG_USB_PD_DUAL_ROLE
	       || pd[port].task_state == PD_STATE_SNK_DISCONNECTED
#endif
		;
}

static void pd_set_data_role(int port, enum pd_data_role role)
{
	pd[port].data_role = role;
#ifdef CONFIG_USB_PD_DUAL_ROLE
	pd_update_saved_port_flags(port, PD_BBRMFLG_DATA_ROLE, role);
#endif /* defined(CONFIG_USB_PD_DUAL_ROLE) */
	pd_execute_data_swap(port, role);

	set_usb_mux_with_current_data_role(port);
	pd_update_roles(port);
#ifdef CONFIG_BC12_DETECT_DATA_ROLE_TRIGGER
	/*
	 * For BC1.2 detection that is triggered on data role change events
	 * instead of VBUS changes, need to set an event to wake up the USB_CHG
	 * task and indicate the current data role.
	 */
	if (role == PD_ROLE_UFP)
		usb_charger_task_set_event(port, USB_CHG_EVENT_DR_UFP);
	else if (role == PD_ROLE_DFP)
		usb_charger_task_set_event(port, USB_CHG_EVENT_DR_DFP);
#endif /* CONFIG_BC12_DETECT_DATA_ROLE_TRIGGER */
}

#ifdef CONFIG_USBC_VCONN
static void pd_set_vconn_role(int port, int role)
{
	if (role == PD_ROLE_VCONN_ON)
		pd[port].flags |= PD_FLAGS_VCONN_ON;
	else
		pd[port].flags &= ~PD_FLAGS_VCONN_ON;

#ifdef CONFIG_USB_PD_DUAL_ROLE
	pd_update_saved_port_flags(port, PD_BBRMFLG_VCONN_ROLE, role);
#endif
}
#endif /* CONFIG_USBC_VCONN */

void pd_execute_hard_reset(int port)
{
	int hard_rst_tx = pd[port].last_state == PD_STATE_HARD_RESET_SEND;

	CPRINTF("C%d HARD RST %cX\n", port, hard_rst_tx ? 'T' : 'R');

	pd[port].msg_id = 0;
	invalidate_last_message_id(port);
	tcpm_set_rx_enable(port, 0);
#ifdef CONFIG_USB_PD_ALT_MODE_DFP
	if (pd_dfp_exit_mode(port, TCPCI_MSG_SOP, 0, 0))
		usb_mux_set_safe_mode(port);
#endif

#ifdef CONFIG_USB_PD_REV30
	pd[port].rev = PD_REV30;
#endif
	/*
	 * Fake set last state to hard reset to make sure that the next
	 * state to run knows that we just did a hard reset.
	 */
	pd[port].last_state = PD_STATE_HARD_RESET_EXECUTE;

#ifdef CONFIG_USB_PD_DUAL_ROLE
	/*
	 * If we are swapping to a source and have changed to Rp, restore back
	 * to Rd and turn off vbus to match our power_role.
	 */
	if (pd[port].task_state == PD_STATE_SNK_SWAP_STANDBY ||
	    pd[port].task_state == PD_STATE_SNK_SWAP_COMPLETE) {
		tcpm_set_cc(port, TYPEC_CC_RD);
		pd_power_supply_reset(port);
	}

	if (pd[port].power_role == PD_ROLE_SINK) {
		/* Initial data role for sink is UFP */
		pd_set_data_role(port, PD_ROLE_UFP);

		/* Clear the input current limit */
		pd_set_input_current_limit(port, 0, 0);
#ifdef CONFIG_CHARGE_MANAGER
		charge_manager_set_ceil(port, CEIL_REQUESTOR_PD,
					CHARGE_CEIL_NONE);
#endif /* CONFIG_CHARGE_MANAGER */

#ifdef CONFIG_USBC_VCONN
		/*
		 * Sink must turn off Vconn after a hard reset if it was being
		 * sourced previously
		 */
		if (pd[port].flags & PD_FLAGS_VCONN_ON) {
			set_vconn(port, 0);
			pd_set_vconn_role(port, PD_ROLE_VCONN_OFF);
		}
#endif

		set_state(port, PD_STATE_SNK_HARD_RESET_RECOVER);
		return;
	} else {
		/* Initial data role for source is DFP */
		pd_set_data_role(port, PD_ROLE_DFP);
	}

#endif /* CONFIG_USB_PD_DUAL_ROLE */

	if (!hard_rst_tx)
		crec_usleep(PD_T_PS_HARD_RESET);

	/* We are a source, cut power */
	pd_power_supply_reset(port);
	pd[port].src_recover = get_time().val + PD_T_SRC_RECOVER;
#ifdef CONFIG_USBC_VCONN
	set_vconn(port, 0);
#endif
	set_state(port, PD_STATE_SRC_HARD_RESET_RECOVER);
}

static void execute_soft_reset(int port)
{
	invalidate_last_message_id(port);
	set_state(port, DUAL_ROLE_IF_ELSE(port, PD_STATE_SNK_DISCOVERY,
					  PD_STATE_SRC_DISCOVERY));
	CPRINTF("C%d Soft Rst\n", port);
}

void pd_soft_reset(void)
{
	int i;

	for (i = 0; i < board_get_usb_pd_port_count(); ++i)
		if (pd_is_connected(i)) {
			set_state(i, PD_STATE_SOFT_RESET);
			task_wake(PD_PORT_TO_TASK_ID(i));
		}
}

#ifdef CONFIG_USB_PD_DUAL_ROLE
/*
 * Request desired charge voltage from source.
 * Returns EC_SUCCESS on success or non-zero on failure.
 */
static int pd_send_request_msg(int port, int always_send_request)
{
	uint32_t rdo, curr_limit, supply_voltage;
	int res;

	/* Clear new power request */
	pd[port].new_power_request = 0;

	/* Build and send request RDO */
	pd_build_request(0, &rdo, &curr_limit, &supply_voltage, port);

	if (!always_send_request) {
		/* Don't re-request the same voltage */
		if (pd[port].prev_request_mv == supply_voltage)
			return EC_SUCCESS;
#ifdef CONFIG_CHARGE_MANAGER
		/* Limit current to PD_MIN_MA during transition */
		else
			charge_manager_force_ceil(port, PD_MIN_MA);
#endif
	}

	CPRINTF("C%d Req [%d] %dmV %dmA", port, RDO_POS(rdo), supply_voltage,
		curr_limit);
	if (rdo & RDO_CAP_MISMATCH)
		CPRINTF(" Mismatch");
	CPRINTF("\n");

	pd[port].curr_limit = curr_limit;
	pd[port].supply_voltage = supply_voltage;
	pd[port].prev_request_mv = supply_voltage;
	res = send_request(port, rdo);
	if (res < 0)
		return res;
	set_state(port, PD_STATE_SNK_REQUESTED);
	return EC_SUCCESS;
}
#endif

static void pd_update_pdo_flags(int port, int pdo_cnt, uint32_t *pdos)
{
	/* can only parse PDO flags if type is fixed */
	if ((pdos[0] & PDO_TYPE_MASK) != PDO_TYPE_FIXED)
		return;

#ifdef CONFIG_USB_PD_DUAL_ROLE
	if (pdos[0] & PDO_FIXED_DUAL_ROLE)
		pd[port].flags |= PD_FLAGS_PARTNER_DR_POWER;
	else
		pd[port].flags &= ~PD_FLAGS_PARTNER_DR_POWER;

	if (pdos[0] & PDO_FIXED_UNCONSTRAINED)
		pd[port].flags |= PD_FLAGS_PARTNER_UNCONSTR;
	else
		pd[port].flags &= ~PD_FLAGS_PARTNER_UNCONSTR;

	if (pdos[0] & PDO_FIXED_COMM_CAP)
		pd[port].flags |= PD_FLAGS_PARTNER_USB_COMM;
	else
		pd[port].flags &= ~PD_FLAGS_PARTNER_USB_COMM;
#endif

	if (pdos[0] & PDO_FIXED_DATA_SWAP)
		pd[port].flags |= PD_FLAGS_PARTNER_DR_DATA;
	else
		pd[port].flags &= ~PD_FLAGS_PARTNER_DR_DATA;

	/*
	 * Treat device as a dedicated charger (meaning we should charge
	 * from it) if:
	 *   - it does not support power swap, or
	 *   - it is unconstrained power, or
	 *   - it presents at least 27 W of available power
	 */
	if (IS_ENABLED(CONFIG_CHARGE_MANAGER)) {
		uint32_t max_ma, max_mv, max_pdo, max_mw, unused;

		/*
		 * Get max power that the partner offers (not necessarily what
		 * this board will request)
		 */
		pd_find_pdo_index(pdo_cnt, pdos, PD_REV3_MAX_VOLTAGE, &max_pdo);
		pd_extract_pdo_power(max_pdo, &max_ma, &max_mv, &unused);
		max_mw = max_ma * max_mv / 1000;

		if (!(pdos[0] & PDO_FIXED_DUAL_ROLE) ||
		    (pdos[0] & PDO_FIXED_UNCONSTRAINED) ||
		    max_mw >= PD_DRP_CHARGE_POWER_MIN)
			charge_manager_update_dualrole(port, CAP_DEDICATED);
		else
			charge_manager_update_dualrole(port, CAP_DUALROLE);
	}
}

static void handle_data_request(int port, uint32_t head, uint32_t *payload)
{
	int type = PD_HEADER_TYPE(head);
	int cnt = PD_HEADER_CNT(head);

	switch (type) {
#ifdef CONFIG_USB_PD_DUAL_ROLE
	case PD_DATA_SOURCE_CAP:
		if ((pd[port].task_state == PD_STATE_SNK_DISCOVERY) ||
		    (pd[port].task_state == PD_STATE_SNK_TRANSITION) ||
		    (pd[port].task_state == PD_STATE_SNK_REQUESTED) ||
		    ((get_usb_pd_vbus_detect() == USB_PD_VBUS_DETECT_NONE) &&
		     (pd[port].task_state == PD_STATE_SNK_HARD_RESET_RECOVER)) ||
		    (pd[port].task_state == PD_STATE_SNK_READY)) {
#ifdef CONFIG_USB_PD_REV30
			/*
			 * Only adjust sink rev if source rev is higher.
			 */
			if (PD_HEADER_REV(head) < pd[port].rev)
				pd[port].rev = PD_HEADER_REV(head);
#endif
			/* Port partner is now known to be PD capable */
			pd[port].flags |= PD_FLAGS_PREVIOUS_PD_CONN;

			/* src cap 0 should be fixed PDO */
			pd_update_pdo_flags(port, cnt, payload);

			pd_process_source_cap(port, cnt, payload);

			/* Source will resend source cap on failure */
			pd_send_request_msg(port, 1);
		}
		break;
#endif /* CONFIG_USB_PD_DUAL_ROLE */
	case PD_DATA_REQUEST:
		if ((pd[port].power_role == PD_ROLE_SOURCE) && (cnt == 1)) {
#ifdef CONFIG_USB_PD_REV30
			/*
			 * Adjust the rev level to what the sink supports. If
			 * they're equal, no harm done.
			 */
			pd[port].rev = PD_HEADER_REV(head);
#endif
			if (!pd_check_requested_voltage(payload[0], port)) {
				if (send_control(port, PD_CTRL_ACCEPT) < 0)
					/*
					 * if we fail to send accept, do
					 * nothing and let sink timeout and
					 * send hard reset
					 */
					return;

				/* explicit contract is now in place */
				pd[port].flags |= PD_FLAGS_EXPLICIT_CONTRACT;
#ifdef CONFIG_USB_PD_DUAL_ROLE
				pd_update_saved_port_flags(
					port, PD_BBRMFLG_EXPLICIT_CONTRACT, 1);
#endif /* CONFIG_USB_PD_DUAL_ROLE */
				pd[port].requested_idx = RDO_POS(payload[0]);
				set_state(port, PD_STATE_SRC_ACCEPTED);
				return;
			}
		}
		/* the message was incorrect or cannot be satisfied */
		send_control(port, PD_CTRL_REJECT);
		/* keep last contract in place (whether implicit or explicit) */
		set_state(port, PD_STATE_SRC_READY);
		break;
	case PD_DATA_BIST:
		/* If not in READY state, then don't start BIST */
		if (DUAL_ROLE_IF_ELSE(
			    port, pd[port].task_state == PD_STATE_SNK_READY,
			    pd[port].task_state == PD_STATE_SRC_READY)) {
			/* currently only support sending bist carrier mode 2 */
			if ((payload[0] >> 28) == 5) {
				/* bist data object mode is 2 */
				pd_transmit(port, TCPCI_MSG_TX_BIST_MODE_2, 0,
					    NULL, AMS_RESPONSE);
				/* Set to appropriate port disconnected state */
				set_state(port,
					  DUAL_ROLE_IF_ELSE(
						  port,
						  PD_STATE_SNK_DISCONNECTED,
						  PD_STATE_SRC_DISCONNECTED));
			}
		}
		break;
	case PD_DATA_SINK_CAP:
		pd[port].flags |= PD_FLAGS_SNK_CAP_RECVD;
		/* snk cap 0 should be fixed PDO */
		pd_update_pdo_flags(port, cnt, payload);
		if (pd[port].task_state == PD_STATE_SRC_GET_SINK_CAP)
			set_state(port, PD_STATE_SRC_READY);
		break;
#ifdef CONFIG_USB_PD_REV30
	case PD_DATA_BATTERY_STATUS:
		break;
#endif
	case PD_DATA_VENDOR_DEF:
		handle_vdm_request(port, cnt, payload, head);
		break;
	default:
		CPRINTF("C%d Unhandled data message type %d\n", port, type);
	}
}

#ifdef CONFIG_USB_PD_DUAL_ROLE
void pd_request_power_swap(int port)
{
	if (pd[port].task_state == PD_STATE_SRC_READY)
		set_state(port, PD_STATE_SRC_SWAP_INIT);
	else if (pd[port].task_state == PD_STATE_SNK_READY)
		set_state(port, PD_STATE_SNK_SWAP_INIT);
	task_wake(PD_PORT_TO_TASK_ID(port));
}

#ifdef CONFIG_USBC_VCONN_SWAP
void pd_request_vconn_swap(int port)
{
	if (pd[port].task_state == PD_STATE_SRC_READY ||
	    pd[port].task_state == PD_STATE_SNK_READY)
		set_state(port, PD_STATE_VCONN_SWAP_SEND);
	task_wake(PD_PORT_TO_TASK_ID(port));
}

void pd_try_vconn_src(int port)
{
	/*
	 * If we don't currently provide vconn, and we can supply it, send
	 * a vconn swap request.
	 */
	if (!(pd[port].flags & PD_FLAGS_VCONN_ON)) {
		if (pd_check_vconn_swap(port))
			pd_request_vconn_swap(port);
	}
}
#endif
#endif /* CONFIG_USB_PD_DUAL_ROLE */

void pd_request_data_swap(int port)
{
	if (DUAL_ROLE_IF_ELSE(port, pd[port].task_state == PD_STATE_SNK_READY,
			      pd[port].task_state == PD_STATE_SRC_READY))
		set_state(port, PD_STATE_DR_SWAP);
	task_wake(PD_PORT_TO_TASK_ID(port));
}

static void pd_set_power_role(int port, enum pd_power_role role)
{
	pd[port].power_role = role;
#ifdef CONFIG_USB_PD_DUAL_ROLE
	pd_update_saved_port_flags(port, PD_BBRMFLG_POWER_ROLE, role);
#endif /* defined(CONFIG_USB_PD_DUAL_ROLE) */
}

static void pd_dr_swap(int port)
{
	pd_set_data_role(port, !pd[port].data_role);
	pd[port].flags |= PD_FLAGS_CHECK_IDENTITY;
}

static void handle_ctrl_request(int port, uint32_t head, uint32_t *payload)
{
	int type = PD_HEADER_TYPE(head);
	int res;

	switch (type) {
	case PD_CTRL_GOOD_CRC:
		/* should not get it */
		break;
	case PD_CTRL_PING:
		/* Nothing else to do */
		break;
	case PD_CTRL_GET_SOURCE_CAP:
		if (pd[port].task_state == PD_STATE_SRC_READY)
			set_state(port, PD_STATE_SRC_DISCOVERY);
		else {
			res = send_source_cap(port, AMS_RESPONSE);
			if ((res >= 0) &&
			    (pd[port].task_state == PD_STATE_SRC_DISCOVERY))
				set_state(port, PD_STATE_SRC_NEGOCIATE);
		}
		break;
	case PD_CTRL_GET_SINK_CAP:
#ifdef CONFIG_USB_PD_DUAL_ROLE
		send_sink_cap(port);
#else
		send_control(port, NOT_SUPPORTED(pd[port].rev));
#endif
		break;
#ifdef CONFIG_USB_PD_DUAL_ROLE
	case PD_CTRL_GOTO_MIN:
#ifdef CONFIG_USB_PD_GIVE_BACK
		if (pd[port].task_state == PD_STATE_SNK_READY) {
			/*
			 * Reduce power consumption now!
			 *
			 * The source will restore power to this sink
			 * by sending a new source cap message at a
			 * later time.
			 */
			pd_snk_give_back(port, &pd[port].curr_limit,
					 &pd[port].supply_voltage);
			set_state(port, PD_STATE_SNK_TRANSITION);
		}
#endif

		break;
	case PD_CTRL_PS_RDY:
		if (pd[port].task_state == PD_STATE_SNK_SWAP_SRC_DISABLE) {
			set_state(port, PD_STATE_SNK_SWAP_STANDBY);
		} else if (pd[port].task_state == PD_STATE_SRC_SWAP_STANDBY) {
			/* reset message ID and swap roles */
			pd[port].msg_id = 0;
			invalidate_last_message_id(port);
			pd_set_power_role(port, PD_ROLE_SINK);
			pd_update_roles(port);
			/*
			 * Give the state machine time to read VBUS as high.
			 * Note: This is empirically determined, not strictly
			 * part of the USB PD spec.
			 */
			pd[port].vbus_debounce_time =
				get_time().val + PD_T_DEBOUNCE;
			set_state(port, PD_STATE_SNK_DISCOVERY);
#ifdef CONFIG_USBC_VCONN_SWAP
		} else if (pd[port].task_state == PD_STATE_VCONN_SWAP_INIT) {
			/*
			 * If VCONN is on, then this PS_RDY tells us it's
			 * ok to turn VCONN off
			 */
			if (pd[port].flags & PD_FLAGS_VCONN_ON)
				set_state(port, PD_STATE_VCONN_SWAP_READY);
#endif
		} else if (pd[port].task_state == PD_STATE_SNK_DISCOVERY) {
			/* Don't know what power source is ready. Reset. */
			set_state(port, PD_STATE_HARD_RESET_SEND);
		} else if (pd[port].task_state == PD_STATE_SNK_SWAP_STANDBY) {
			/* Do nothing, assume this is a redundant PD_RDY */
		} else if (pd[port].power_role == PD_ROLE_SINK) {
			/*
			 * Give the source some time to send any messages before
			 * we start our interrogation.  Add some jitter of up to
			 * ~192ms to prevent multiple collisions.
			 */
			if (pd[port].task_state == PD_STATE_SNK_TRANSITION)
				pd[port].ready_state_holdoff_timer =
					get_time().val + SNK_READY_HOLD_OFF_US +
					(get_time().le.lo & 0xf) * 12 * MSEC;

			set_state(port, PD_STATE_SNK_READY);
			pd_set_input_current_limit(port, pd[port].curr_limit,
						   pd[port].supply_voltage);
#ifdef CONFIG_CHARGE_MANAGER
			/* Set ceiling based on what's negotiated */
			charge_manager_set_ceil(port, CEIL_REQUESTOR_PD,
						pd[port].curr_limit);
#endif
		}
		break;
#endif
	case PD_CTRL_REJECT:
	case PD_CTRL_WAIT:
		if (pd[port].task_state == PD_STATE_DR_SWAP) {
			if (type == PD_CTRL_WAIT) /* try again ... */
				pd[port].flags |= PD_FLAGS_CHECK_DR_ROLE;
			set_state(port, READY_RETURN_STATE(port));
		}
#ifdef CONFIG_USBC_VCONN_SWAP
		else if (pd[port].task_state == PD_STATE_VCONN_SWAP_SEND)
			set_state(port, READY_RETURN_STATE(port));
#endif
#ifdef CONFIG_USB_PD_DUAL_ROLE
		else if (pd[port].task_state == PD_STATE_SRC_SWAP_INIT)
			set_state(port, PD_STATE_SRC_READY);
		else if (pd[port].task_state == PD_STATE_SNK_SWAP_INIT)
			set_state(port, PD_STATE_SNK_READY);
		else if (pd[port].task_state == PD_STATE_SNK_REQUESTED) {
			/*
			 * On reception of a WAIT message, transition to
			 * PD_STATE_SNK_READY after PD_T_SINK_REQUEST ms to
			 * send another request.
			 *
			 * On reception of a REJECT message, transition to
			 * PD_STATE_SNK_READY but don't resend the request if
			 * we already have a contract in place.
			 *
			 * On reception of a REJECT message without a contract,
			 * transition to PD_STATE_SNK_DISCOVERY instead.
			 */
			if (type == PD_CTRL_WAIT) {
				/*
				 * Trigger a new power request when
				 * we enter PD_STATE_SNK_READY
				 */
				pd[port].new_power_request = 1;

				/*
				 * After the request is triggered,
				 * make sure the request is sent.
				 */
				pd[port].prev_request_mv = 0;

				/*
				 * Transition to PD_STATE_SNK_READY
				 * after PD_T_SINK_REQUEST ms.
				 */
				set_state_timeout(port,
						  get_time().val +
							  PD_T_SINK_REQUEST,
						  PD_STATE_SNK_READY);
			} else {
				/* The request was rejected */
				const int in_contract =
					pd[port].flags &
					PD_FLAGS_EXPLICIT_CONTRACT;
				set_state(port, in_contract ?
							PD_STATE_SNK_READY :
							PD_STATE_SNK_DISCOVERY);
			}
		}
#endif
		break;
	case PD_CTRL_ACCEPT:
		if (pd[port].task_state == PD_STATE_SOFT_RESET) {
			/*
			 * For the case that we sent soft reset in SNK_DISCOVERY
			 * on startup due to VBUS never low, clear the flag.
			 */
			pd[port].flags &= ~PD_FLAGS_VBUS_NEVER_LOW;
			execute_soft_reset(port);
		} else if (pd[port].task_state == PD_STATE_DR_SWAP) {
			/* switch data role */
			pd_dr_swap(port);
			set_state(port, READY_RETURN_STATE(port));
#ifdef CONFIG_USB_PD_DUAL_ROLE
#ifdef CONFIG_USBC_VCONN_SWAP
		} else if (pd[port].task_state == PD_STATE_VCONN_SWAP_SEND) {
			/* switch vconn */
			set_state(port, PD_STATE_VCONN_SWAP_INIT);
#endif
		} else if (pd[port].task_state == PD_STATE_SRC_SWAP_INIT) {
			/* explicit contract goes away for power swap */
			pd[port].flags &= ~PD_FLAGS_EXPLICIT_CONTRACT;
			pd_update_saved_port_flags(
				port, PD_BBRMFLG_EXPLICIT_CONTRACT, 0);
			set_state(port, PD_STATE_SRC_SWAP_SNK_DISABLE);
		} else if (pd[port].task_state == PD_STATE_SNK_SWAP_INIT) {
			/* explicit contract goes away for power swap */
			pd[port].flags &= ~PD_FLAGS_EXPLICIT_CONTRACT;
			pd_update_saved_port_flags(
				port, PD_BBRMFLG_EXPLICIT_CONTRACT, 0);
			set_state(port, PD_STATE_SNK_SWAP_SNK_DISABLE);
		} else if (pd[port].task_state == PD_STATE_SNK_REQUESTED) {
			/* explicit contract is now in place */
			pd[port].flags |= PD_FLAGS_EXPLICIT_CONTRACT;
			pd_update_saved_port_flags(
				port, PD_BBRMFLG_EXPLICIT_CONTRACT, 1);
			set_state(port, PD_STATE_SNK_TRANSITION);
#endif
		}
		break;
	case PD_CTRL_SOFT_RESET:
		execute_soft_reset(port);
		pd[port].msg_id = 0;
		/* We are done, acknowledge with an Accept packet */
		send_control(port, PD_CTRL_ACCEPT);
		break;
	case PD_CTRL_PR_SWAP:
#ifdef CONFIG_USB_PD_DUAL_ROLE
		if (pd_check_power_swap(port)) {
			send_control(port, PD_CTRL_ACCEPT);
			/*
			 * Clear flag for checking power role to avoid
			 * immediately requesting another swap.
			 */
			pd[port].flags &= ~PD_FLAGS_CHECK_PR_ROLE;
			set_state(port,
				  DUAL_ROLE_IF_ELSE(
					  port, PD_STATE_SNK_SWAP_SNK_DISABLE,
					  PD_STATE_SRC_SWAP_SNK_DISABLE));
		} else {
			send_control(port, PD_CTRL_REJECT);
		}
#else
		send_control(port, NOT_SUPPORTED(pd[port].rev));
#endif
		break;
	case PD_CTRL_DR_SWAP:
		if (pd_check_data_swap(port, pd[port].data_role)) {
			/*
			 * Accept switch and perform data swap. Clear
			 * flag for checking data role to avoid
			 * immediately requesting another swap.
			 */
			pd[port].flags &= ~PD_FLAGS_CHECK_DR_ROLE;
			if (send_control(port, PD_CTRL_ACCEPT) >= 0)
				pd_dr_swap(port);
		} else {
			send_control(port, PD_CTRL_REJECT);
		}
		break;
	case PD_CTRL_VCONN_SWAP:
#ifdef CONFIG_USBC_VCONN_SWAP
		if (pd[port].task_state == PD_STATE_SRC_READY ||
		    pd[port].task_state == PD_STATE_SNK_READY) {
			if (pd_check_vconn_swap(port)) {
				if (send_control(port, PD_CTRL_ACCEPT) > 0)
					set_state(port,
						  PD_STATE_VCONN_SWAP_INIT);
			} else {
				send_control(port, PD_CTRL_REJECT);
			}
		}
#else
		send_control(port, NOT_SUPPORTED(pd[port].rev));
#endif
		break;
	default:
#ifdef CONFIG_USB_PD_REV30
		send_control(port, PD_CTRL_NOT_SUPPORTED);
#endif
		CPRINTF("C%d Unhandled ctrl message type %d\n", port, type);
	}
}

#ifdef CONFIG_USB_PD_REV30
static void handle_ext_request(int port, uint16_t head, uint32_t *payload)
{
	int type = PD_HEADER_TYPE(head);

	switch (type) {
	case PD_EXT_GET_BATTERY_CAP:
		send_battery_cap(port, payload);
		break;
	case PD_EXT_GET_BATTERY_STATUS:
		send_battery_status(port, payload);
		break;
	case PD_EXT_BATTERY_CAP:
		break;
	default:
		send_control(port, PD_CTRL_NOT_SUPPORTED);
	}
}
#endif

static void handle_request(int port, uint32_t head, uint32_t *payload)
{
	int cnt = PD_HEADER_CNT(head);
	int data_role = PD_HEADER_DROLE(head);
	int p;

	/* dump received packet content (only dump ping at debug level 3) */
	if ((debug_level == 2 && PD_HEADER_TYPE(head) != PD_CTRL_PING) ||
	    debug_level >= 3) {
		CPRINTF("C%d RECV %04x/%d ", port, head, cnt);
		for (p = 0; p < cnt; p++)
			CPRINTF("[%d]%08x ", p, payload[p]);
		CPRINTF("\n");
	}

	/*
	 * If we are in disconnected state, we shouldn't get a request. Do
	 * a hard reset if we get one.
	 */
	if (!pd_is_connected(port))
		set_state(port, PD_STATE_HARD_RESET_SEND);

	/*
	 * When a data role conflict is detected, USB-C ErrorRecovery
	 * actions shall be performed, and transitioning to unattached state
	 * is one such legal action.
	 */
	if (pd[port].data_role == data_role) {
		/*
		 * If the port doesn't support removing the terminations, just
		 * go to the unattached state.
		 */
		if (tcpm_set_cc(port, TYPEC_CC_OPEN) == EC_SUCCESS) {
			/* Do not drive VBUS or VCONN. */
			pd_power_supply_reset(port);
#ifdef CONFIG_USBC_VCONN
			set_vconn(port, 0);
#endif /* defined(CONFIG_USBC_VCONN) */
			crec_usleep(PD_T_ERROR_RECOVERY);

			/* Restore terminations. */
			tcpm_set_cc(port, DUAL_ROLE_IF_ELSE(port, TYPEC_CC_RD,
							    TYPEC_CC_RP));
		}
		set_state(port,
			  DUAL_ROLE_IF_ELSE(port, PD_STATE_SNK_DISCONNECTED,
					    PD_STATE_SRC_DISCONNECTED));
		return;
	}

#ifdef CONFIG_USB_PD_REV30
	/* Check if this is an extended chunked data message. */
	if (pd[port].rev == PD_REV30 && PD_HEADER_EXT(head)) {
		handle_ext_request(port, head, payload);
		return;
	}
#endif
	if (cnt)
		handle_data_request(port, head, payload);
	else
		handle_ctrl_request(port, head, payload);
}

void pd_send_vdm(int port, uint32_t vid, int cmd, const uint32_t *data,
		 int count)
{
	if (count > VDO_MAX_SIZE - 1) {
		CPRINTF("C%d VDM over max size\n", port);
		return;
	}

	/* set VDM header with VID & CMD */
	pd[port].vdo_data[0] = VDO(vid,
				   ((vid & USB_SID_PD) == USB_SID_PD) ?
					   1 :
					   (PD_VDO_CMD(cmd) <= CMD_ATTENTION),
				   cmd);
#ifdef CONFIG_USB_PD_REV30
	pd[port].vdo_data[0] |= VDO_SVDM_VERS_MAJOR(vdo_ver[pd[port].rev]);
#endif
	queue_vdm(port, pd[port].vdo_data, data, count, TCPCI_MSG_SOP);

	task_wake(PD_PORT_TO_TASK_ID(port));
}

static inline int pdo_busy(int port)
{
	/*
	 * Note, main PDO state machine (pd_task) uses READY state exclusively
	 * to denote port partners have successfully negociated a contract.  All
	 * other protocol actions force state transitions.
	 */
	int rv = (pd[port].task_state != PD_STATE_SRC_READY);
#ifdef CONFIG_USB_PD_DUAL_ROLE
	rv &= (pd[port].task_state != PD_STATE_SNK_READY);
#endif
	return rv;
}

static uint64_t vdm_get_ready_timeout(uint32_t vdm_hdr)
{
	uint64_t timeout;
	int cmd = PD_VDO_CMD(vdm_hdr);

	/* its not a structured VDM command */
	if (!PD_VDO_SVDM(vdm_hdr))
		return 500 * MSEC;

	switch (PD_VDO_CMDT(vdm_hdr)) {
	case CMDT_INIT:
		if ((cmd == CMD_ENTER_MODE) || (cmd == CMD_EXIT_MODE))
			timeout = PD_T_VDM_WAIT_MODE_E;
		else
			timeout = PD_T_VDM_SNDR_RSP;
		break;
	default:
		if ((cmd == CMD_ENTER_MODE) || (cmd == CMD_EXIT_MODE))
			timeout = PD_T_VDM_E_MODE;
		else
			timeout = PD_T_VDM_RCVR_RSP;
		break;
	}
	return timeout;
}

static void pd_vdm_send_state_machine(int port)
{
	int res;
	uint16_t header;

	switch (pd[port].vdm_state) {
	case VDM_STATE_READY:
		/* Only transmit VDM if connected. */
		if (!pd_is_connected(port)) {
			pd[port].vdm_state = VDM_STATE_ERR_BUSY;
			break;
		}

		/*
		 * if there's traffic or we're not in PDO ready state don't send
		 * a VDM.
		 */
		if (pdo_busy(port))
			break;

		/* Prepare SOP header and send VDM */
		header = PD_HEADER(PD_DATA_VENDOR_DEF, pd[port].power_role,
				   pd[port].data_role, pd[port].msg_id,
				   (int)pd[port].vdo_count,
				   pd_get_rev(port, TCPCI_MSG_SOP), 0);
		res = pd_transmit(port, TCPCI_MSG_SOP, header,
				  pd[port].vdo_data, AMS_START);

		if (res < 0) {
			pd[port].vdm_state = VDM_STATE_ERR_SEND;
		} else {
			pd[port].vdm_state = VDM_STATE_BUSY;
			pd[port].vdm_timeout.val =
				get_time().val +
				vdm_get_ready_timeout(pd[port].vdo_data[0]);
		}
		break;
	case VDM_STATE_WAIT_RSP_BUSY:
		/* wait and then initiate request again */
		if (get_time().val > pd[port].vdm_timeout.val) {
			pd[port].vdo_data[0] = pd[port].vdo_retry;
			pd[port].vdo_count = 1;
			pd[port].vdm_state = VDM_STATE_READY;
		}
		break;
	case VDM_STATE_BUSY:
		/* Wait for VDM response or timeout */
		if (pd[port].vdm_timeout.val &&
		    (get_time().val > pd[port].vdm_timeout.val)) {
			pd[port].vdm_state = VDM_STATE_ERR_TMOUT;
		}
		break;
	case VDM_STATE_ERR_SEND:
		/* Sending the VDM failed, so try again. */
		CPRINTF("C%d VDMretry\n", port);
		pd[port].vdm_state = VDM_STATE_READY;
		break;
	default:
		break;
	}
}

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

int pd_dev_store_rw_hash(int port, uint16_t dev_id, uint32_t *rw_hash,
			 uint32_t current_image)
{
#ifdef CONFIG_COMMON_RUNTIME
	int i;
#endif

	pd[port].dev_id = dev_id;
	memcpy(pd[port].dev_rw_hash, rw_hash, PD_RW_HASH_SIZE);
#ifdef CONFIG_CMD_PD_DEV_DUMP_INFO
	if (debug_level >= 2)
		pd_dev_dump_info(dev_id, (uint8_t *)rw_hash);
#endif
	pd[port].current_image = current_image;

#ifdef CONFIG_COMMON_RUNTIME
	/* Search table for matching device / hash */
	for (i = 0; i < RW_HASH_ENTRIES; i++)
		if (dev_id == rw_hash_table[i].dev_id)
			return !memcmp(rw_hash, rw_hash_table[i].dev_rw_hash,
				       PD_RW_HASH_SIZE);
#endif
	return 0;
}

void pd_dev_get_rw_hash(int port, uint16_t *dev_id, uint8_t *rw_hash,
			uint32_t *current_image)
{
	*dev_id = pd[port].dev_id;
	*current_image = pd[port].current_image;
	if (*dev_id)
		memcpy(rw_hash, pd[port].dev_rw_hash, PD_RW_HASH_SIZE);
}

__maybe_unused static void exit_supported_alt_mode(int port)
{
	int i;

	if (!IS_ENABLED(CONFIG_USB_PD_ALT_MODE_DFP))
		return;

	for (i = 0; i < supported_modes_cnt; i++) {
		int opos = pd_alt_mode(port, TCPCI_MSG_SOP,
				       supported_modes[i].svid);

		if (opos > 0 &&
		    pd_dfp_exit_mode(port, TCPCI_MSG_SOP,
				     supported_modes[i].svid, opos)) {
			CPRINTS("C%d Exiting ALT mode with SVID = 0x%x", port,
				supported_modes[i].svid);
			usb_mux_set_safe_mode(port);
			pd_send_vdm(port, supported_modes[i].svid,
				    CMD_EXIT_MODE | VDO_OPOS(opos), NULL, 0);
			/* Wait for an ACK from port-partner */
			pd_vdm_send_state_machine(port);
		}
	}
}

#ifdef CONFIG_AP_POWER_CONTROL
static void handle_new_power_state(int port)
{
	if (chipset_in_or_transitioning_to_state(CHIPSET_STATE_ANY_OFF)) {
		/*
		 * The SoC will negotiate the alternate mode again when
		 * it boots up.
		 */
		exit_supported_alt_mode(port);
	}
#ifdef CONFIG_USBC_VCONN_SWAP
	else {
		/* Request for Vconn Swap */
		pd_try_vconn_src(port);
	}
#endif
	/* Ensure mux is set properly after chipset transition */
	set_usb_mux_with_current_data_role(port);
}
#endif /* CONFIG_AP_POWER_CONTROL */

#ifdef CONFIG_USB_PD_DUAL_ROLE
enum pd_dual_role_states pd_get_dual_role(int port)
{
	return drp_state[port];
}

#ifdef CONFIG_USB_PD_TRY_SRC
static void pd_update_try_source(void)
{
	int i;

	pd_try_src_enable = pd_is_try_source_capable();

	/*
	 * Clear this flag to cover case where a TrySrc
	 * mode went from enabled to disabled and trying_source
	 * was active at that time.
	 */
	for (i = 0; i < board_get_usb_pd_port_count(); i++)
		pd[i].flags &= ~PD_FLAGS_TRY_SRC;
}
#endif /* CONFIG_USB_PD_TRY_SRC */

#ifdef CONFIG_USB_PD_RESET_MIN_BATT_SOC
static void pd_update_snk_reset(void)
{
	int i;
	int batt_soc = usb_get_battery_soc();

	if (batt_soc < CONFIG_USB_PD_RESET_MIN_BATT_SOC ||
	    battery_get_disconnect_state() != BATTERY_NOT_DISCONNECTED)
		return;

	for (i = 0; i < board_get_usb_pd_port_count(); i++) {
		if (pd[i].flags & PD_FLAGS_SNK_WAITING_BATT) {
			/*
			 * Battery has gained sufficient charge to kick off PD
			 * negotiation and withstand a hard reset.  Clear the
			 * flag and let reset begin if task is waiting in
			 * SNK_DISCOVERY.
			 */
			pd[i].flags &= ~PD_FLAGS_SNK_WAITING_BATT;

			if (pd[i].task_state == PD_STATE_SNK_DISCOVERY) {
				CPRINTS("C%d: Starting soft reset timer", i);
				set_state_timeout(
					i, get_time().val + PD_T_SINK_WAIT_CAP,
					PD_STATE_SOFT_RESET);
			}
		}
	}
}
#endif

#if defined(CONFIG_USB_PD_TRY_SRC) || defined(CONFIG_USB_PD_RESET_MIN_BATT_SOC)
static void pd_update_battery_soc_change(void)
{
#ifdef CONFIG_USB_PD_TRY_SRC
	pd_update_try_source();
#endif

#ifdef CONFIG_USB_PD_RESET_MIN_BATT_SOC
	pd_update_snk_reset();
#endif
}
DECLARE_HOOK(HOOK_BATTERY_SOC_CHANGE, pd_update_battery_soc_change,
	     HOOK_PRIO_DEFAULT);
#endif /* CONFIG_USB_PD_TRY_SRC || CONFIG_USB_PD_RESET_MIN_BATT_SOC */

static inline void pd_set_dual_role_no_wakeup(int port,
					      enum pd_dual_role_states state)
{
	drp_state[port] = state;

#ifdef CONFIG_USB_PD_TRY_SRC
	pd_update_try_source();
#endif
}

void pd_set_dual_role(int port, enum pd_dual_role_states state)
{
	pd_set_dual_role_no_wakeup(port, state);

	/* Wake task up to process change */
	task_set_event(PD_PORT_TO_TASK_ID(port), PD_EVENT_UPDATE_DUAL_ROLE);
}

static int pd_is_power_swapping(int port)
{
	/* return true if in the act of swapping power roles */
	return pd[port].task_state == PD_STATE_SNK_SWAP_SNK_DISABLE ||
	       pd[port].task_state == PD_STATE_SNK_SWAP_SRC_DISABLE ||
	       pd[port].task_state == PD_STATE_SNK_SWAP_STANDBY ||
	       pd[port].task_state == PD_STATE_SNK_SWAP_COMPLETE ||
	       pd[port].task_state == PD_STATE_SRC_SWAP_SNK_DISABLE ||
	       pd[port].task_state == PD_STATE_SRC_SWAP_SRC_DISABLE ||
	       pd[port].task_state == PD_STATE_SRC_SWAP_STANDBY;
}

/* This must only be called from the PD task */
static void pd_update_dual_role_config(int port)
{
	/*
	 * Change to sink if port is currently a source AND (new DRP
	 * state is force sink OR new DRP state is toggle off and we are in the
	 * source disconnected state).
	 */
	if (pd[port].power_role == PD_ROLE_SOURCE &&
	    (drp_state[port] == PD_DRP_FORCE_SINK ||
	     (drp_state[port] == PD_DRP_TOGGLE_OFF &&
	      pd[port].task_state == PD_STATE_SRC_DISCONNECTED))) {
		pd_set_power_role(port, PD_ROLE_SINK);
		set_state(port, PD_STATE_SNK_DISCONNECTED);
		tcpm_set_cc(port, TYPEC_CC_RD);
		/* Make sure we're not sourcing VBUS. */
		pd_power_supply_reset(port);
	}

	/*
	 * Change to source if port is currently a sink and the
	 * new DRP state is force source. If we are performing
	 * power swap we won't change anything because
	 * changing state will disrupt power swap process
	 * and we are power swapping to desired power role.
	 */
	if (pd[port].power_role == PD_ROLE_SINK &&
	    drp_state[port] == PD_DRP_FORCE_SOURCE &&
	    !pd_is_power_swapping(port)) {
		pd_set_power_role(port, PD_ROLE_SOURCE);
		set_state(port, PD_STATE_SRC_DISCONNECTED);
		tcpm_set_cc(port, TYPEC_CC_RP);
	}
}

/*
 * Provide Rp to ensure the partner port is in a known state (eg. not
 * PD negotiated, not sourcing 20V).
 */
static void pd_partner_port_reset(int port)
{
	uint64_t timeout;
	uint8_t flags;

	/*
	 * If there is no contract in place (or if we fail to read the BBRAM
	 * flags), there is no need to reset the partner.
	 */
	if (pd_get_saved_port_flags(port, &flags) != EC_SUCCESS ||
	    !(flags & PD_BBRMFLG_EXPLICIT_CONTRACT))
		return;

	/*
	 * If we reach here, an explicit contract is in place.
	 *
	 * If PD communications are allowed, don't apply Rp.  We'll issue a
	 * SoftReset later on and renegotiate our contract.  This particular
	 * condition only applies to unlocked RO images with an explicit
	 * contract in place.
	 */
	if (pd_comm_is_enabled(port))
		return;

	/* If we just lost power, don't apply Rp. */
	if (system_get_reset_flags() &
	    (EC_RESET_FLAG_BROWNOUT | EC_RESET_FLAG_POWER_ON))
		return;

	/*
	 * Clear the active contract bit before we apply Rp in case we
	 * intentionally brown out because we cut off our only power supply.
	 */
	pd_update_saved_port_flags(port, PD_BBRMFLG_EXPLICIT_CONTRACT, 0);

	/* Provide Rp for 200 msec. or until we no longer have VBUS. */
	CPRINTF("C%d Apply Rp!\n", port);
	cflush();
	tcpm_set_cc(port, TYPEC_CC_RP);
	timeout = get_time().val + 200 * MSEC;

	while (get_time().val < timeout && pd_is_vbus_present(port))
		crec_msleep(10);
}
#endif /* CONFIG_USB_PD_DUAL_ROLE */

enum pd_power_role pd_get_power_role(int port)
{
	return pd[port].power_role;
}

enum pd_data_role pd_get_data_role(int port)
{
	return pd[port].data_role;
}

enum pd_cc_states pd_get_task_cc_state(int port)
{
	return pd[port].cc_state;
}

uint8_t pd_get_task_state(int port)
{
	return pd[port].task_state;
}

#ifdef CONFIG_USB_PD_DUAL_ROLE
uint32_t pd_get_requested_voltage(int port)
{
	return pd[port].supply_voltage;
}

uint32_t pd_get_requested_current(int port)
{
	return pd[port].curr_limit;
}
#endif

const char *pd_get_task_state_name(int port)
{
#ifdef CONFIG_USB_PD_TCPMV1_DEBUG
	if (debug_level > 0)
		return pd_state_names[pd[port].task_state];
#endif
	return "";
}

bool pd_get_vconn_state(int port)
{
	return !!(pd[port].flags & PD_FLAGS_VCONN_ON);
}

bool pd_get_partner_dual_role_power(int port)
{
	return !!(pd[port].flags & PD_FLAGS_PARTNER_DR_POWER);
}

bool pd_get_partner_unconstr_power(int port)
{
	return !!(pd[port].flags & PD_FLAGS_PARTNER_UNCONSTR);
}

enum tcpc_cc_polarity pd_get_polarity(int port)
{
	return pd[port].polarity;
}

bool pd_get_partner_data_swap_capable(int port)
{
	/* return data swap capable status of port partner */
	return !!(pd[port].flags & PD_FLAGS_PARTNER_DR_DATA);
}

#ifdef CONFIG_COMMON_RUNTIME
void pd_comm_enable(int port, int enable)
{
	/* We don't check port >= CONFIG_USB_PD_PORT_MAX_COUNT deliberately */
	pd_comm_enabled[port] = enable;

	/* If type-C connection, then update the TCPC RX enable */
	if (pd_is_connected(port))
		tcpm_set_rx_enable(port, enable);

#ifdef CONFIG_USB_PD_DUAL_ROLE
	/*
	 * If communications are enabled, start hard reset timer for
	 * any port in PD_SNK_DISCOVERY.
	 */
	if (enable && pd[port].task_state == PD_STATE_SNK_DISCOVERY)
		set_state_timeout(port, get_time().val + PD_T_SINK_WAIT_CAP,
				  PD_STATE_HARD_RESET_SEND);
#endif
}
#endif

void pd_ping_enable(int port, int enable)
{
	if (enable)
		pd[port].flags |= PD_FLAGS_PING_ENABLED;
	else
		pd[port].flags &= ~PD_FLAGS_PING_ENABLED;
}

__overridable uint8_t board_get_src_dts_polarity(int port)
{
	/*
	 * If the port in SRC DTS, the polarity is determined by the board,
	 * i.e. what Rp impedance the CC lines are pulled. If this function
	 * is not overridden, assume CC1 is primary.
	 */
	return 0;
}

#if defined(CONFIG_CHARGE_MANAGER)

/**
 * Signal power request to indicate a charger update that affects the port.
 */
void pd_set_new_power_request(int port)
{
	pd[port].new_power_request = 1;
	task_wake(PD_PORT_TO_TASK_ID(port));
}
#endif /* CONFIG_CHARGE_MANAGER */

#if defined(CONFIG_USBC_BACKWARDS_COMPATIBLE_DFP) && defined(CONFIG_USBC_SS_MUX)
/*
 * Backwards compatible DFP does not support USB SS because it applies VBUS
 * before debouncing CC and setting USB SS muxes, but SS detection will fail
 * before we are done debouncing CC.
 */
#error "Backwards compatible DFP does not support USB"
#endif

#ifdef CONFIG_COMMON_RUNTIME

/* Initialize globals based on system state. */
static void pd_init_tasks(void)
{
	static int initialized;
	int enable = 1;
	int i;

	/* Initialize globals once, for all PD tasks.  */
	if (initialized)
		return;

#if defined(CONFIG_AP_POWER_CONTROL) && defined(CONFIG_USB_PD_DUAL_ROLE)
	/* Set dual-role state based on chipset power state */
	if (chipset_in_state(CHIPSET_STATE_ANY_OFF))
		for (i = 0; i < board_get_usb_pd_port_count(); i++)
			drp_state[i] = PD_DRP_FORCE_SINK;
	else if (chipset_in_state(CHIPSET_STATE_ANY_SUSPEND))
		for (i = 0; i < board_get_usb_pd_port_count(); i++)
			drp_state[i] = PD_DRP_TOGGLE_OFF;
	else /* CHIPSET_STATE_ON */
		for (i = 0; i < board_get_usb_pd_port_count(); i++)
			drp_state[i] = PD_DRP_TOGGLE_ON;
#endif

#if defined(CONFIG_USB_PD_COMM_DISABLED)
	enable = 0;
#elif defined(CONFIG_USB_PD_COMM_LOCKED)
	/* Disable PD communication if we're in RO, WP is enabled, and EFS
	 * didn't register NO_BOOT. */
	if (!system_is_in_rw() && system_is_locked() && !vboot_allow_usb_pd())
		enable = 0;
#endif
	for (i = 0; i < board_get_usb_pd_port_count(); i++)
		pd_comm_enabled[i] = enable;
	CPRINTS("PD comm %sabled", enable ? "en" : "dis");

	initialized = 1;
}
#endif /* CONFIG_COMMON_RUNTIME */

#if !defined(CONFIG_USB_PD_TCPC) && defined(CONFIG_USB_PD_DUAL_ROLE)
static int pd_restart_tcpc(int port)
{
	if (board_set_tcpc_power_mode) {
		/* force chip reset */
		board_set_tcpc_power_mode(port, 0);
	}
	return tcpm_init(port);
}
#endif

void pd_task(void *u)
{
	uint32_t head;
	int port = TASK_ID_TO_PD_PORT(task_get_current());
	uint32_t payload[7];
	int timeout = 10 * MSEC;
	enum tcpc_cc_voltage_status cc1, cc2;
	int res, incoming_packet = 0;
	int hard_reset_count = 0;
#ifdef CONFIG_USB_PD_DUAL_ROLE
	uint64_t next_role_swap = PD_T_DRP_SNK;
	uint8_t saved_flgs = 0;
#ifndef CONFIG_USB_PD_VBUS_DETECT_NONE
	int snk_hard_reset_vbus_off = 0;
#endif
#ifdef CONFIG_USB_PD_DUAL_ROLE_AUTO_TOGGLE
	const int auto_toggle_supported = tcpm_auto_toggle_supported(port);
#endif
#if defined(CONFIG_CHARGE_MANAGER)
	typec_current_t typec_curr = 0, typec_curr_change = 0;
#endif /* CONFIG_CHARGE_MANAGER */
#endif /* CONFIG_USB_PD_DUAL_ROLE */
	enum pd_states this_state;
	enum pd_cc_states new_cc_state;
	timestamp_t now;
	uint64_t next_src_cap = 0;
	int caps_count = 0, hard_reset_sent = 0;
	int snk_cap_count = 0;
	int evt;

#ifdef CONFIG_USB_PD_TCPC_LOW_POWER
	/*
	 * Set the ports in Low Power Mode so that other tasks wait until
	 * TCPC is initialized and ready.
	 */
	pd[port].flags |= PD_FLAGS_LPM_ENGAGED;
#endif

#ifdef CONFIG_COMMON_RUNTIME
	pd_init_tasks();
#endif

	/*
	 * Ensure the power supply is in the default state and ensure we are not
	 * sourcing Vconn
	 */
	pd_power_supply_reset(port);
#ifdef CONFIG_USBC_VCONN
#ifdef CONFIG_USB_PD_DUAL_ROLE
	/*
	 * If we were previously a sink but also the VCONN source, we should
	 * still continue to source VCONN. Otherwise, we should turn off VCONN
	 * since we are also going to turn off VBUS.
	 */
	if (pd_comm_is_enabled(port) &&
	    (pd_get_saved_port_flags(port, &saved_flgs) == EC_SUCCESS) &&
	    ((saved_flgs & PD_BBRMFLG_POWER_ROLE) == PD_ROLE_SINK) &&
	    (saved_flgs & PD_BBRMFLG_EXPLICIT_CONTRACT) &&
	    (saved_flgs & PD_BBRMFLG_VCONN_ROLE))
		set_vconn(port, 1);
	else
#endif
		set_vconn(port, 0);
#endif

#ifdef CONFIG_USB_PD_TCPC_BOARD_INIT
	/* Board specific TCPC init */
	board_tcpc_init();
#endif

	/* Initialize TCPM driver and wait for TCPC to be ready */
	res = reset_device_and_notify(port);
	invalidate_last_message_id(port);

#ifdef CONFIG_USB_PD_DUAL_ROLE
	pd_partner_port_reset(port);
#endif

	this_state = res ? PD_STATE_SUSPENDED : PD_DEFAULT_STATE(port);
#ifndef CONFIG_USB_PD_TCPC
	if (!res) {
		struct ec_response_pd_chip_info_v1 info;

		if (tcpm_get_chip_info(port, 0, &info) == EC_SUCCESS) {
			CPRINTS("TCPC p%d VID:0x%x PID:0x%x DID:0x%x "
				"FWV:0x%" PRIx64,
				port, info.vendor_id, info.product_id,
				info.device_id, info.fw_version_number);
		}
	}
#endif

#ifdef CONFIG_USB_PD_REV30
	/* Set Revision to highest */
	pd[port].rev = PD_REV30;
#endif

#ifdef CONFIG_USB_PD_DUAL_ROLE
	/*
	 * If VBUS is high, then initialize flag for VBUS has always been
	 * present. This flag is used to maintain a PD connection after a
	 * reset by sending a soft reset.
	 */
	pd[port].flags |= pd_is_vbus_present(port) ? PD_FLAGS_VBUS_NEVER_LOW :
						     0;
#endif

	/* Disable TCPC RX until connection is established */
	tcpm_set_rx_enable(port, 0);

#ifdef CONFIG_USBC_SS_MUX
	/* Initialize USB mux to its default state */
	usb_mux_init(port);
#endif

#ifdef CONFIG_USB_PD_DUAL_ROLE
	/*
	 * If there's an explicit contract in place, let's restore the data and
	 * power roles such that any messages we send to the port partner will
	 * still be valid.
	 */
	if (pd_comm_is_enabled(port) &&
	    (pd_get_saved_port_flags(port, &saved_flgs) == EC_SUCCESS) &&
	    (saved_flgs & PD_BBRMFLG_EXPLICIT_CONTRACT)) {
		/* Only attempt to maintain previous sink contracts */
		if ((saved_flgs & PD_BBRMFLG_POWER_ROLE) == PD_ROLE_SINK) {
			pd_set_power_role(port,
					  (saved_flgs & PD_BBRMFLG_POWER_ROLE) ?
						  PD_ROLE_SOURCE :
						  PD_ROLE_SINK);
			pd_set_data_role(port,
					 (saved_flgs & PD_BBRMFLG_DATA_ROLE) ?
						 PD_ROLE_DFP :
						 PD_ROLE_UFP);
#ifdef CONFIG_USBC_VCONN
			pd_set_vconn_role(port,
					  (saved_flgs & PD_BBRMFLG_VCONN_ROLE) ?
						  PD_ROLE_VCONN_ON :
						  PD_ROLE_VCONN_OFF);
#endif /* CONFIG_USBC_VCONN */

			/*
			 * Since there is an explicit contract in place, let's
			 * issue a SoftReset such that we can renegotiate with
			 * our port partner in order to synchronize our state
			 * machines.
			 */
			this_state = PD_STATE_SOFT_RESET;

			/*
			 * Re-discover any alternate modes we may have been
			 * using with this port partner.
			 */
			pd[port].flags |= PD_FLAGS_CHECK_IDENTITY;
		} else {
			/*
			 * Vbus was turned off during the power supply reset
			 * earlier, so clear the contract flag and re-start as
			 * default role
			 */
			pd_update_saved_port_flags(
				port, PD_BBRMFLG_EXPLICIT_CONTRACT, 0);
		}
		/*
		 * Set the TCPC reset event such that we can set our CC
		 * terminations, determine polarity, and enable RX so we
		 * can hear back from our port partner if maintaining our old
		 * connection.
		 */
		task_set_event(task_get_current(), PD_EVENT_TCPC_RESET);
	}
#endif /* defined(CONFIG_USB_PD_DUAL_ROLE) */
	/* Set the power role if we haven't already. */
	if (this_state != PD_STATE_SOFT_RESET)
		pd_set_power_role(port, PD_ROLE_DEFAULT(port));

	/* Initialize PD protocol state variables for each port. */
	pd[port].vdm_state = VDM_STATE_DONE;
	set_state(port, this_state);
	tcpm_select_rp_value(port, CONFIG_USB_PD_PULLUP);
#ifdef CONFIG_USB_PD_DUAL_ROLE
	/*
	 * If we're not in an explicit contract, set our terminations to match
	 * our default power role.
	 */
	if (!(saved_flgs & PD_BBRMFLG_EXPLICIT_CONTRACT))
#endif /* CONFIG_USB_PD_DUAL_ROLE */
		tcpm_set_cc(port, PD_ROLE_DEFAULT(port) == PD_ROLE_SOURCE ?
					  TYPEC_CC_RP :
					  TYPEC_CC_RD);

#ifdef CONFIG_USBC_PPC
	/*
	 * Wait to initialize the PPC after setting the correct Rd values in
	 * the TCPC otherwise the TCPC might not be pulling the CC lines down
	 * when the PPC connects the CC lines from the USB connector to the
	 * TCPC cause the source to drop Vbus causing a brown out.
	 */
	ppc_init(port);
#endif

#ifdef CONFIG_USB_PD_ALT_MODE_DFP
	/* Initialize PD Policy engine */
	pd_dfp_discovery_init(port);
	pd_dfp_mode_init(port);
#endif

#ifdef CONFIG_CHARGE_MANAGER
	/* Initialize PD and type-C supplier current limits to 0 */
	pd_set_input_current_limit(port, 0, 0);
	typec_set_input_current_limit(port, 0, 0);
	charge_manager_update_dualrole(port, CAP_UNKNOWN);
#endif

	/*
	 * Since most boards configure the TCPC interrupt as edge
	 * and it is possible that the interrupt line was asserted between init
	 * and calling set_state, we need to process any pending interrupts now.
	 * Otherwise future interrupts will never fire because another edge
	 * never happens. Note this needs to happen after set_state() is called.
	 */
	if (IS_ENABLED(CONFIG_HAS_TASK_PD_INT))
		schedule_deferred_pd_interrupt(port);

	while (1) {
		/* process VDM messages last */
		pd_vdm_send_state_machine(port);

		/* Verify board specific health status : current, voltages... */
		res = pd_board_checks();
		if (res != EC_SUCCESS) {
			/* cut the power */
			pd_execute_hard_reset(port);
			/* notify the other side of the issue */
			pd_transmit(port, TCPCI_MSG_TX_HARD_RESET, 0, NULL,
				    AMS_START);
		}

		/* wait for next event/packet or timeout expiration */
		evt = task_wait_event(timeout);

#ifdef CONFIG_USB_PD_TCPC_LOW_POWER
		if (evt & (PD_EXIT_LOW_POWER_EVENT_MASK | TASK_EVENT_WAKE))
			exit_low_power_mode(port);
		if (evt & PD_EVENT_DEVICE_ACCESSED)
			handle_device_access(port);
#endif
#ifdef CONFIG_AP_POWER_CONTROL
		if (evt & PD_EVENT_POWER_STATE_CHANGE)
			handle_new_power_state(port);
#endif

#if defined(CONFIG_USB_PD_ALT_MODE_DFP)
		if (evt & PD_EVENT_SYSJUMP) {
			exit_supported_alt_mode(port);
			notify_sysjump_ready();
		}
#endif

#ifdef CONFIG_USB_PD_DUAL_ROLE
		if (evt & PD_EVENT_UPDATE_DUAL_ROLE)
			pd_update_dual_role_config(port);
#endif

#ifdef CONFIG_USB_PD_TCPC
		/*
		 * run port controller task to check CC and/or read incoming
		 * messages
		 */
		tcpc_run(port, evt);
#else
		/* if TCPC has reset, then need to initialize it again */
		if (evt & PD_EVENT_TCPC_RESET) {
			reset_device_and_notify(port);
#ifdef CONFIG_USB_PD_DUAL_ROLE_AUTO_TOGGLE
		}

		if ((evt & PD_EVENT_TCPC_RESET) &&
		    (pd[port].task_state != PD_STATE_DRP_AUTO_TOGGLE)) {
#endif
#ifdef CONFIG_USB_PD_DUAL_ROLE
			if (pd[port].task_state == PD_STATE_SOFT_RESET) {
				enum tcpc_cc_voltage_status cc1, cc2;

				/*
				 * Set the terminations to match our power
				 * role.
				 */
				tcpm_set_cc(port, pd[port].power_role ?
							  TYPEC_CC_RP :
							  TYPEC_CC_RD);

				/* Determine the polarity. */
				tcpm_get_cc(port, &cc1, &cc2);
				if (pd[port].power_role == PD_ROLE_SINK) {
					pd[port].polarity =
						get_snk_polarity(cc1, cc2);
				} else if (cc_is_snk_dbg_acc(cc1, cc2)) {
					pd[port].polarity =
						board_get_src_dts_polarity(
							port);
				} else {
					pd[port].polarity =
						get_src_polarity(cc1, cc2);
				}
			} else
#endif /* CONFIG_USB_PD_DUAL_ROLE */
			{
				/* Ensure CC termination is default */
				tcpm_set_cc(port,
					    PD_ROLE_DEFAULT(port) ==
							    PD_ROLE_SOURCE ?
						    TYPEC_CC_RP :
						    TYPEC_CC_RD);
			}

			/*
			 * If we have a stable contract in the default role,
			 * then simply update TCPC with some missing info
			 * so that we can continue without resetting PD comms.
			 * Otherwise, go to the default disconnected state
			 * and force renegotiation.
			 */
			if (pd[port].vdm_state == VDM_STATE_DONE &&
			    (
#ifdef CONFIG_USB_PD_DUAL_ROLE
				    (PD_ROLE_DEFAULT(port) == PD_ROLE_SINK &&
				     pd[port].task_state ==
					     PD_STATE_SNK_READY) ||
				    (pd[port].task_state ==
				     PD_STATE_SOFT_RESET) ||
#endif
				    (PD_ROLE_DEFAULT(port) == PD_ROLE_SOURCE &&
				     pd[port].task_state ==
					     PD_STATE_SRC_READY))) {
				typec_set_polarity(port, pd[port].polarity);
				tcpm_set_msg_header(port, pd[port].power_role,
						    pd[port].data_role);
				tcpm_set_rx_enable(port, 1);
			} else {
				/* Ensure state variables are at default */
				pd_set_power_role(port, PD_ROLE_DEFAULT(port));
				pd[port].vdm_state = VDM_STATE_DONE;
				set_state(port, PD_DEFAULT_STATE(port));
#ifdef CONFIG_USB_PD_DUAL_ROLE
				pd_update_dual_role_config(port);
#endif
			}
		}
#endif

#ifdef CONFIG_USBC_PPC
		/*
		 * TODO: Useful for non-PPC cases as well, but only needed
		 * for PPC cases right now. Revisit later.
		 */
		if (evt & PD_EVENT_SEND_HARD_RESET)
			set_state(port, PD_STATE_HARD_RESET_SEND);
#endif /* defined(CONFIG_USBC_PPC) */

		if (evt & PD_EVENT_RX_HARD_RESET)
			pd_execute_hard_reset(port);

		/* process any potential incoming message */
		incoming_packet = tcpm_has_pending_message(port);
		if (incoming_packet) {
			/* Dequeue and consume duplicate message ID. */
			if (tcpm_dequeue_message(port, payload, &head) ==
				    EC_SUCCESS &&
			    !consume_repeat_message(port, head))
				handle_request(port, head, payload);

			/* Check if there are any more messages */
			if (tcpm_has_pending_message(port))
				task_set_event(PD_PORT_TO_TASK_ID(port),
					       TASK_EVENT_WAKE);
		}

		if (pd[port].req_suspend_state)
			set_state(port, PD_STATE_SUSPENDED);

		/* if nothing to do, verify the state of the world in 500ms */
		this_state = pd[port].task_state;
		timeout = 500 * MSEC;
		switch (this_state) {
		case PD_STATE_DISABLED:
			/* Nothing to do */
			break;
		case PD_STATE_SRC_DISCONNECTED:
			timeout = 10 * MSEC;
			pd_set_src_caps(port, 0, NULL);
#ifdef CONFIG_USB_PD_TCPC_LOW_POWER
			/*
			 * If SW decided we should be in a low power state and
			 * the CC lines did not change, then don't talk with the
			 * TCPC otherwise we might wake it up.
			 */
			if (pd[port].flags & PD_FLAGS_LPM_REQUESTED &&
			    !(evt & PD_EVENT_CC))
				break;
#endif /* CONFIG_USB_PD_TCPC_LOW_POWER */

			tcpm_get_cc(port, &cc1, &cc2);

#ifdef CONFIG_USB_PD_DUAL_ROLE_AUTO_TOGGLE
			/*
			 * Attempt TCPC auto DRP toggle if it is
			 * not already auto toggling and not try.src
			 */
			if (auto_toggle_supported &&
			    !(pd[port].flags & PD_FLAGS_TCPC_DRP_TOGGLE) &&
			    !is_try_src(port) && cc_is_open(cc1, cc2)) {
				set_state(port, PD_STATE_DRP_AUTO_TOGGLE);
				timeout = 2 * MSEC;
				break;
			}
#endif
			/*
			 * Transition to DEBOUNCE if we detect appropriate
			 * signals
			 *
			 * (from 4.5.2.2.10.2 Exiting from Try.SRC State)
			 * If try_src -and-
			 *    have only one Rd (not both) => DEBOUNCE
			 *
			 * (from 4.5.2.2.7.2 Exiting from Unattached.SRC State)
			 * If not try_src -and-
			 *    have at least one Rd => DEBOUNCE -or-
			 *    have audio access => DEBOUNCE
			 *
			 * try_src should not exit if both pins are Rd
			 */
			if ((is_try_src(port) && cc_is_only_one_rd(cc1, cc2)) ||
			    (!is_try_src(port) &&
			     (cc_is_at_least_one_rd(cc1, cc2) ||
			      cc_is_audio_acc(cc1, cc2)))) {
#ifdef CONFIG_USBC_BACKWARDS_COMPATIBLE_DFP
				/* Enable VBUS */
				if (pd_set_power_supply_ready(port))
					break;
#endif
				pd[port].cc_state = PD_CC_NONE;
				set_state(port,
					  PD_STATE_SRC_DISCONNECTED_DEBOUNCE);
				break;
			}
#if defined(CONFIG_USB_PD_DUAL_ROLE)
			now = get_time();
			/*
			 * Try.SRC state is embedded here. The port
			 * shall transition to TryWait.SNK after
			 * tDRPTry (PD_T_DRP_TRY) and Vbus is within
			 * vSafe0V, or after tTryTimeout
			 * (PD_T_TRY_TIMEOUT). Otherwise we should stay
			 * within Try.SRC (break).
			 */
			if (is_try_src(port)) {
				if (now.val < pd[port].try_src_marker) {
					break;
				} else if (now.val < pd[port].try_timeout) {
					if (pd_is_vbus_present(port))
						break;
				}

				/*
				 * Transition to TryWait.SNK now, so set
				 * state and update src marker time.
				 */
				set_state(port, PD_STATE_SNK_DISCONNECTED);
				pd_set_power_role(port, PD_ROLE_SINK);
				tcpm_set_cc(port, TYPEC_CC_RD);
				pd[port].try_src_marker =
					get_time().val + PD_T_DEBOUNCE;
				timeout = 2 * MSEC;
				break;
			}

			/*
			 * If Try.SRC state is not active, then handle
			 * the normal DRP toggle from SRC->SNK.
			 */
			if (now.val < next_role_swap ||
			    drp_state[port] == PD_DRP_FORCE_SOURCE ||
			    drp_state[port] == PD_DRP_FREEZE)
				break;

			/*
			 * Transition to SNK now, so set state and
			 * update next role swap time.
			 */
			set_state(port, PD_STATE_SNK_DISCONNECTED);
			pd_set_power_role(port, PD_ROLE_SINK);
			tcpm_set_cc(port, TYPEC_CC_RD);
			next_role_swap = get_time().val + PD_T_DRP_SNK;
			/* Swap states quickly */
			timeout = 2 * MSEC;
#endif
			break;
		case PD_STATE_SRC_DISCONNECTED_DEBOUNCE:
			timeout = 20 * MSEC;
			tcpm_get_cc(port, &cc1, &cc2);

			if (cc_is_snk_dbg_acc(cc1, cc2)) {
				/* Debug accessory */
				new_cc_state = PD_CC_UFP_DEBUG_ACC;
			} else if (cc_is_at_least_one_rd(cc1, cc2)) {
				/* UFP attached */
				new_cc_state = PD_CC_UFP_ATTACHED;
			} else if (cc_is_audio_acc(cc1, cc2)) {
				/* Audio accessory */
				new_cc_state = PD_CC_UFP_AUDIO_ACC;
			} else {
				/* No UFP */
				set_state(port, PD_STATE_SRC_DISCONNECTED);
				timeout = 5 * MSEC;
				break;
			}

			/* Set debounce timer */
			if (new_cc_state != pd[port].cc_state) {
				pd[port].cc_debounce =
					get_time().val +
					(is_try_src(port) ? PD_T_DEBOUNCE :
							    PD_T_CC_DEBOUNCE);
				pd[port].cc_state = new_cc_state;
				break;
			}

			/* Debounce the cc state */
			if (get_time().val < pd[port].cc_debounce)
				break;

			/* Debounce complete */
			if (IS_ENABLED(CONFIG_COMMON_RUNTIME))
				hook_notify(HOOK_USB_PD_CONNECT);

#ifdef CONFIG_USBC_PPC
			/*
			 * If the port is latched off, just continue to
			 * monitor for a detach.
			 */
			if (usbc_ocp_is_port_latched_off(port))
				break;
#endif /* CONFIG_USBC_PPC */

			/* UFP is attached */
			if (new_cc_state == PD_CC_UFP_ATTACHED ||
			    new_cc_state == PD_CC_UFP_DEBUG_ACC) {
#ifdef CONFIG_USBC_PPC
				/* Inform PPC that a sink is connected. */
				ppc_dev_is_connected(port, PPC_DEV_SNK);
#endif /* CONFIG_USBC_PPC */
				if (IS_ENABLED(CONFIG_USBC_OCP))
					usbc_ocp_snk_is_connected(port, true);
				if (new_cc_state == PD_CC_UFP_DEBUG_ACC) {
					pd[port].polarity =
						board_get_src_dts_polarity(
							port);
				} else {
					pd[port].polarity =
						get_src_polarity(cc1, cc2);
				}
				typec_set_polarity(port, pd[port].polarity);

				/* initial data role for source is DFP */
				pd_set_data_role(port, PD_ROLE_DFP);

				/* Enable Auto Discharge Disconnect */
				tcpm_enable_auto_discharge_disconnect(port, 1);

				if (new_cc_state == PD_CC_UFP_DEBUG_ACC)
					pd[port].flags |=
						PD_FLAGS_TS_DTS_PARTNER;

#ifdef CONFIG_USBC_VCONN
				/*
				 * Do not source Vconn when debug accessory is
				 * detected. Section 4.5.2.2.17.1 in USB spec
				 * v1-3
				 */
				if (new_cc_state != PD_CC_UFP_DEBUG_ACC) {
					/*
					 * Start sourcing Vconn before Vbus to
					 * ensure we are within USB Type-C
					 * Spec 1.3 tVconnON.
					 */
					set_vconn(port, 1);
					pd_set_vconn_role(port,
							  PD_ROLE_VCONN_ON);
				}
#endif

#ifndef CONFIG_USBC_BACKWARDS_COMPATIBLE_DFP
				/* Enable VBUS */
				if (pd_set_power_supply_ready(port)) {
#ifdef CONFIG_USBC_VCONN
					/* Stop sourcing Vconn if Vbus failed */
					set_vconn(port, 0);
					pd_set_vconn_role(port,
							  PD_ROLE_VCONN_OFF);
#endif /* CONFIG_USBC_VCONN */
#ifdef CONFIG_USBC_SS_MUX
					usb_mux_set(port, USB_PD_MUX_NONE,
						    USB_SWITCH_DISCONNECT,
						    pd[port].polarity);
#endif /* CONFIG_USBC_SS_MUX */
					break;
				}
				/*
				 * Set correct Rp value determined during
				 * pd_set_power_supply_ready.  This should be
				 * safe because Vconn is being sourced,
				 * preventing incorrect CCD detection.
				 */
				tcpm_set_cc(port, TYPEC_CC_RP);
#endif /* CONFIG_USBC_BACKWARDS_COMPATIBLE_DFP */
				/* If PD comm is enabled, enable TCPC RX */
				if (pd_comm_is_enabled(port))
					tcpm_set_rx_enable(port, 1);

				pd[port].flags |= PD_FLAGS_CHECK_PR_ROLE |
						  PD_FLAGS_CHECK_DR_ROLE;
				hard_reset_count = 0;
				timeout = 5 * MSEC;

				set_state(port, PD_STATE_SRC_STARTUP);
			}
			/*
			 * AUDIO_ACC will remain in this state indefinitely
			 * until disconnect.
			 */
			break;
		case PD_STATE_SRC_HARD_RESET_RECOVER:
			/* Do not continue until hard reset recovery time */
			if (get_time().val < pd[port].src_recover) {
				timeout = 50 * MSEC;
				break;
			}

#ifdef CONFIG_USBC_VCONN
			/*
			 * Start sourcing Vconn again and set the flag, in case
			 * it was 0 due to a previous swap
			 */
			set_vconn(port, 1);
			pd_set_vconn_role(port, PD_ROLE_VCONN_ON);
#endif

			/* Enable VBUS */
			timeout = 10 * MSEC;
			if (pd_set_power_supply_ready(port)) {
				set_state(port, PD_STATE_SRC_DISCONNECTED);
				break;
			}
#if defined(CONFIG_USB_PD_TCPM_TCPCI) || defined(CONFIG_USB_PD_TCPM_STUB)
			/*
			 * After transmitting hard reset, TCPM writes
			 * to RECEIVE_DETECT register to enable
			 * PD message passing.
			 */
			if (pd_comm_is_enabled(port))
				tcpm_set_rx_enable(port, 1);
#endif /* CONFIG_USB_PD_TCPM_TCPCI  || CONFIG_USB_PD_TCPM_STUB */

			pd[port].flags |= PD_FLAGS_CHECK_PR_ROLE |
					  PD_FLAGS_CHECK_DR_ROLE;
			set_state(port, PD_STATE_SRC_STARTUP);
			break;
		case PD_STATE_SRC_STARTUP:
			/* Wait for power source to enable */
			if (pd[port].last_state != pd[port].task_state) {
				pd[port].flags |= PD_FLAGS_CHECK_IDENTITY;
				/* reset various counters */
				caps_count = 0;
				pd[port].msg_id = 0;
				snk_cap_count = 0;
				set_state_timeout(
					port,
#ifdef CONFIG_USBC_BACKWARDS_COMPATIBLE_DFP
					/*
					 * delay for power supply to start up.
					 * subtract out debounce time if coming
					 * from debounce state since vbus is
					 * on during debounce.
					 */
					get_time().val +
						PD_POWER_SUPPLY_TURN_ON_DELAY -
						(pd[port].last_state ==
								 PD_STATE_SRC_DISCONNECTED_DEBOUNCE ?
							 PD_T_CC_DEBOUNCE :
							 0),
#else
					get_time().val +
						PD_POWER_SUPPLY_TURN_ON_DELAY,
#endif
					PD_STATE_SRC_DISCOVERY);
			}
			break;
		case PD_STATE_SRC_DISCOVERY:
			now = get_time();
			if (pd[port].last_state != pd[port].task_state) {
				caps_count = 0;
				next_src_cap = now.val;
				/*
				 * If we have had PD connection with this port
				 * partner, then start NoResponseTimer.
				 */
				if (pd_capable(port))
					set_state_timeout(
						port,
						get_time().val +
							PD_T_NO_RESPONSE,
						hard_reset_count <
								PD_HARD_RESET_COUNT ?
							PD_STATE_HARD_RESET_SEND :
							PD_STATE_SRC_DISCONNECTED);
			}

			/* Send source cap some minimum number of times */
			if (caps_count < PD_CAPS_COUNT &&
			    next_src_cap <= now.val) {
				/* Query capabilities of the other side */
				res = send_source_cap(port, AMS_START);
				/* packet was acked => PD capable device) */
				if (res >= 0) {
					set_state(port, PD_STATE_SRC_NEGOCIATE);
					timeout = 10 * MSEC;
					hard_reset_count = 0;
					caps_count = 0;
					/* Port partner is PD capable */
					pd[port].flags |=
						PD_FLAGS_PREVIOUS_PD_CONN;
				} else { /* failed, retry later */
					timeout = PD_T_SEND_SOURCE_CAP;
					next_src_cap =
						now.val + PD_T_SEND_SOURCE_CAP;
					caps_count++;
				}
			} else if (caps_count < PD_CAPS_COUNT) {
				timeout = next_src_cap - now.val;
			}
			break;
		case PD_STATE_SRC_NEGOCIATE:
			/* wait for a "Request" message */
			if (pd[port].last_state != pd[port].task_state)
				set_state_timeout(port,
						  get_time().val +
							  PD_T_SENDER_RESPONSE,
						  PD_STATE_HARD_RESET_SEND);
			break;
		case PD_STATE_SRC_ACCEPTED:
			/* Accept sent, wait for enabling the new voltage */
			if (pd[port].last_state != pd[port].task_state)
				set_state_timeout(port,
						  get_time().val +
							  PD_T_SINK_TRANSITION,
						  PD_STATE_SRC_POWERED);
			break;
		case PD_STATE_SRC_POWERED:
			/* Switch to the new requested voltage */
			if (pd[port].last_state != pd[port].task_state) {
				pd[port].flags |= PD_FLAGS_CHECK_VCONN_STATE;
				pd_transition_voltage(pd[port].requested_idx);
				set_state_timeout(
					port,
					get_time().val +
						PD_POWER_SUPPLY_TURN_ON_DELAY,
					PD_STATE_SRC_TRANSITION);
			}
			break;
		case PD_STATE_SRC_TRANSITION:
			/* the voltage output is good, notify the source */
			res = send_control(port, PD_CTRL_PS_RDY);
			if (res >= 0) {
				timeout = 10 * MSEC;

				/*
				 * Give the sink some time to send any messages
				 * before we may send messages of our own.  Add
				 * some jitter of up to ~192ms, to prevent
				 * multiple collisions. This delay also allows
				 * the sink device to request power role swap
				 * and allow the the accept message to be sent
				 * prior to CMD_DISCOVER_IDENT being sent in the
				 * SRC_READY state.
				 */
				pd[port].ready_state_holdoff_timer =
					get_time().val + SRC_READY_HOLD_OFF_US +
					(get_time().le.lo & 0xf) * 12 * MSEC;

				/* it's time to ping regularly the sink */
				set_state(port, PD_STATE_SRC_READY);
			} else {
				/* The sink did not ack, cut the power... */
				set_state(port, PD_STATE_SRC_DISCONNECTED);
			}
			break;
		case PD_STATE_SRC_READY:
			timeout = PD_T_SOURCE_ACTIVITY;

			/*
			 * Don't send any traffic yet until our holdoff timer
			 * has expired.  Some devices are chatty once we reach
			 * the SRC_READY state and we may end up in a collision
			 * of messages if we try to immediately send our
			 * interrogations.
			 */
			if (get_time().val <=
			    pd[port].ready_state_holdoff_timer)
				break;

			/*
			 * Don't send any PD traffic if we woke up due to
			 * incoming packet or if VDO response pending to avoid
			 * collisions.
			 */
			if (incoming_packet ||
			    (pd[port].vdm_state == VDM_STATE_BUSY))
				break;

			/* Send updated source capabilities to our partner */
			if (pd[port].flags & PD_FLAGS_UPDATE_SRC_CAPS) {
				res = send_source_cap(port, AMS_START);
				if (res >= 0) {
					set_state(port, PD_STATE_SRC_NEGOCIATE);
					pd[port].flags &=
						~PD_FLAGS_UPDATE_SRC_CAPS;
				}
				break;
			}

			/* Send get sink cap if haven't received it yet */
			if (!(pd[port].flags & PD_FLAGS_SNK_CAP_RECVD)) {
				if (++snk_cap_count <= PD_SNK_CAP_RETRIES) {
					/* Get sink cap to know if dual-role
					 * device */
					send_control(port,
						     PD_CTRL_GET_SINK_CAP);
					set_state(port,
						  PD_STATE_SRC_GET_SINK_CAP);
					break;
				} else if (debug_level >= 2 &&
					   snk_cap_count ==
						   PD_SNK_CAP_RETRIES + 1) {
					CPRINTF("C%d ERR SNK_CAP\n", port);
				}
			}

			/* Check power role policy, which may trigger a swap */
			if (pd[port].flags & PD_FLAGS_CHECK_PR_ROLE) {
				pd_check_pr_role(port, PD_ROLE_SOURCE,
						 pd[port].flags);
				pd[port].flags &= ~PD_FLAGS_CHECK_PR_ROLE;
			}

			/* Check data role policy, which may trigger a swap */
			if (pd[port].flags & PD_FLAGS_CHECK_DR_ROLE) {
				pd_check_dr_role(port, pd[port].data_role,
						 pd[port].flags);
				pd[port].flags &= ~PD_FLAGS_CHECK_DR_ROLE;
				break;
			}

			/* Check for Vconn source, which may trigger a swap */
			if (pd[port].flags & PD_FLAGS_CHECK_VCONN_STATE) {
				/*
				 * Ref: Section 2.6.1 of both
				 * USB-PD Spec Revision 2.0, Version 1.3 &
				 * USB-PD Spec Revision 3.0, Version 2.0
				 * During Explicit contract the Sink can
				 * initiate or receive a request an exchange
				 * of VCONN Source.
				 */
				pd_try_execute_vconn_swap(port, pd[port].flags);
				pd[port].flags &= ~PD_FLAGS_CHECK_VCONN_STATE;
				break;
			}

			/* Send discovery SVDMs last */
			if (pd[port].data_role == PD_ROLE_DFP &&
			    (pd[port].flags & PD_FLAGS_CHECK_IDENTITY)) {
#ifndef CONFIG_USB_PD_SIMPLE_DFP
				pd_send_vdm(port, USB_SID_PD,
					    CMD_DISCOVER_IDENT, NULL, 0);
#endif
				pd[port].flags &= ~PD_FLAGS_CHECK_IDENTITY;
				break;
			}

			if (!(pd[port].flags & PD_FLAGS_PING_ENABLED))
				break;

			/* Verify that the sink is alive */
			res = send_control(port, PD_CTRL_PING);
			if (res >= 0)
				break;

			/* Ping dropped. Try soft reset. */
			set_state(port, PD_STATE_SOFT_RESET);
			timeout = 10 * MSEC;
			break;
		case PD_STATE_SRC_GET_SINK_CAP:
			if (pd[port].last_state != pd[port].task_state)
				set_state_timeout(port,
						  get_time().val +
							  PD_T_SENDER_RESPONSE,
						  PD_STATE_SRC_READY);
			break;
		case PD_STATE_DR_SWAP:
			if (pd[port].last_state != pd[port].task_state) {
				res = send_control(port, PD_CTRL_DR_SWAP);
				if (res < 0) {
					timeout = 10 * MSEC;
					/*
					 * If failed to get goodCRC, send
					 * soft reset, otherwise ignore
					 * failure.
					 */
					set_state(port,
						  res == -1 ?
							  PD_STATE_SOFT_RESET :
							  READY_RETURN_STATE(
								  port));
					break;
				}
				/* Wait for accept or reject */
				set_state_timeout(port,
						  get_time().val +
							  PD_T_SENDER_RESPONSE,
						  READY_RETURN_STATE(port));
			}
			break;
#ifdef CONFIG_USB_PD_DUAL_ROLE
		case PD_STATE_SRC_SWAP_INIT:
			if (pd[port].last_state != pd[port].task_state) {
				res = send_control(port, PD_CTRL_PR_SWAP);
				if (res < 0) {
					timeout = 10 * MSEC;
					/*
					 * If failed to get goodCRC, send
					 * soft reset, otherwise ignore
					 * failure.
					 */
					set_state(port,
						  res == -1 ?
							  PD_STATE_SOFT_RESET :
							  PD_STATE_SRC_READY);
					break;
				}
				/* Wait for accept or reject */
				set_state_timeout(port,
						  get_time().val +
							  PD_T_SENDER_RESPONSE,
						  PD_STATE_SRC_READY);
			}
			break;
		case PD_STATE_SRC_SWAP_SNK_DISABLE:
			/* Give time for sink to stop drawing current */
			if (pd[port].last_state != pd[port].task_state)
				set_state_timeout(
					port,
					get_time().val + PD_T_SINK_TRANSITION,
					PD_STATE_SRC_SWAP_SRC_DISABLE);
			break;
		case PD_STATE_SRC_SWAP_SRC_DISABLE:
			if (pd[port].last_state != pd[port].task_state) {
				/* Turn power off */
				pd_power_supply_reset(port);

				/*
				 * Switch to Rd and swap roles to sink
				 *
				 * The reason we do this as early as possible is
				 * to help prevent CC disconnection cases where
				 * both partners are applying an Rp.  Certain PD
				 * stacks (e.g. qualcomm), reflexively apply
				 * their Rp once VBUS falls beneath
				 * ~3.67V. (b/77827528).
				 */
				tcpm_set_cc(port, TYPEC_CC_RD);
				pd_set_power_role(port, PD_ROLE_SINK);

				/* Inform TCPC of power role update. */
				pd_update_roles(port);

				set_state_timeout(
					port,
					get_time().val +
						PD_POWER_SUPPLY_TURN_OFF_DELAY,
					PD_STATE_SRC_SWAP_STANDBY);
			}
			break;
		case PD_STATE_SRC_SWAP_STANDBY:
			/* Send PS_RDY to let sink know our power is off */
			if (pd[port].last_state != pd[port].task_state) {
				/* Send PS_RDY */
				res = send_control(port, PD_CTRL_PS_RDY);
				if (res < 0) {
					timeout = 10 * MSEC;
					set_state(port,
						  PD_STATE_SRC_DISCONNECTED);
					break;
				}
				/* Wait for PS_RDY from new source */
				set_state_timeout(port,
						  get_time().val +
							  PD_T_PS_SOURCE_ON,
						  PD_STATE_SNK_DISCONNECTED);
			}
			break;
		case PD_STATE_SUSPENDED: {
#ifndef CONFIG_USB_PD_TCPC
			int rstatus;
#endif
			tcpc_prints("suspended!", port);
			pd[port].req_suspend_state = 0;
#ifdef CONFIG_USB_PD_TCPC
			pd_rx_disable_monitoring(port);
			pd_hw_release(port);
			pd_power_supply_reset(port);
#else
			pd_power_supply_reset(port);
#ifdef CONFIG_USBC_VCONN
			set_vconn(port, 0);
#endif
			rstatus = tcpm_release(port);
			if (rstatus != 0 && rstatus != EC_ERROR_UNIMPLEMENTED)
				tcpc_prints("release failed!", port);
#endif
			/* Drain any outstanding software message queues. */
			tcpm_clear_pending_messages(port);

			/* Wait for resume */
			while (pd[port].task_state == PD_STATE_SUSPENDED) {
#ifdef CONFIG_USB_PD_ALT_MODE_DFP
				int evt = task_wait_event(-1);

				if (evt & PD_EVENT_SYSJUMP)
					/* Nothing to do for sysjump prep */
					notify_sysjump_ready();
#else
				task_wait_event(-1);
#endif
			}
#ifdef CONFIG_USB_PD_TCPC
			pd_hw_init(port, PD_ROLE_DEFAULT(port));
			tcpc_prints("resumed!", port);
#else
			if (rstatus != EC_ERROR_UNIMPLEMENTED &&
			    pd_restart_tcpc(port) != 0) {
				/* stay in PD_STATE_SUSPENDED */
				tcpc_prints("restart failed!", port);
				break;
			}
			/* Set the CC termination and state back to default */
			tcpm_set_cc(port,
				    PD_ROLE_DEFAULT(port) == PD_ROLE_SOURCE ?
					    TYPEC_CC_RP :
					    TYPEC_CC_RD);
			set_state(port, PD_DEFAULT_STATE(port));
			tcpc_prints("resumed!", port);
#endif
			break;
		}
		case PD_STATE_SNK_DISCONNECTED:
#ifdef CONFIG_USB_PD_LOW_POWER
			timeout = (drp_state[port] != PD_DRP_TOGGLE_ON ?
					   SECOND :
					   10 * MSEC);
#else
			timeout = 10 * MSEC;
#endif
			pd_set_src_caps(port, 0, NULL);
#ifdef CONFIG_USB_PD_TCPC_LOW_POWER
			/*
			 * If SW decided we should be in a low power state and
			 * the CC lines did not change, then don't talk with the
			 * TCPC otherwise we might wake it up.
			 */
			if (pd[port].flags & PD_FLAGS_LPM_REQUESTED &&
			    !(evt & PD_EVENT_CC))
				break;
#endif /* CONFIG_USB_PD_TCPC_LOW_POWER */

			tcpm_get_cc(port, &cc1, &cc2);

#ifdef CONFIG_USB_PD_DUAL_ROLE_AUTO_TOGGLE
			/*
			 * Attempt TCPC auto DRP toggle if it is not already
			 * auto toggling and not try.src, and dual role toggling
			 * is allowed.
			 */
			if (auto_toggle_supported &&
			    !(pd[port].flags & PD_FLAGS_TCPC_DRP_TOGGLE) &&
			    !is_try_src(port) && cc_is_open(cc1, cc2) &&
			    (drp_state[port] == PD_DRP_TOGGLE_ON)) {
				set_state(port, PD_STATE_DRP_AUTO_TOGGLE);
				timeout = 2 * MSEC;
				break;
			}
#endif

			/* Source connection monitoring */
			if (!cc_is_open(cc1, cc2)) {
				pd[port].cc_state = PD_CC_NONE;
				hard_reset_count = 0;
				new_cc_state = PD_CC_NONE;
				pd[port].cc_debounce =
					get_time().val + PD_T_CC_DEBOUNCE;
				set_state(port,
					  PD_STATE_SNK_DISCONNECTED_DEBOUNCE);
				timeout = 10 * MSEC;
				break;
			}

			/*
			 * If Try.SRC is active and failed to detect a SNK,
			 * then it transitions to TryWait.SNK. Need to prevent
			 * normal dual role toggle until tDRPTryWait timer
			 * expires.
			 */
			if (pd[port].flags & PD_FLAGS_TRY_SRC) {
				if (get_time().val > pd[port].try_src_marker)
					pd[port].flags &= ~PD_FLAGS_TRY_SRC;
				break;
			}

			/* If no source detected, check for role toggle. */
			if (drp_state[port] == PD_DRP_TOGGLE_ON &&
			    get_time().val >= next_role_swap) {
				/* Swap roles to source */
				pd_set_power_role(port, PD_ROLE_SOURCE);
				set_state(port, PD_STATE_SRC_DISCONNECTED);
				tcpm_set_cc(port, TYPEC_CC_RP);
				next_role_swap = get_time().val + PD_T_DRP_SRC;

#ifdef CONFIG_USB_PD_TCPC_LOW_POWER
				/*
				 * Clear low power mode flag as we are swapping
				 * states quickly.
				 */
				pd[port].flags &= ~PD_FLAGS_LPM_REQUESTED;
#endif

				/* Swap states quickly */
				timeout = 2 * MSEC;
				break;
			}

#ifdef CONFIG_USB_PD_TCPC_LOW_POWER
			/*
			 * If we are remaining in the SNK_DISCONNECTED state,
			 * let's go into low power mode and wait for a change on
			 * CC status.
			 */
			pd[port].flags |= PD_FLAGS_LPM_REQUESTED;
#endif /* CONFIG_USB_PD_TCPC_LOW_POWER */
			break;

		case PD_STATE_SNK_DISCONNECTED_DEBOUNCE:
			tcpm_get_cc(port, &cc1, &cc2);

			if (cc_is_rp(cc1) && cc_is_rp(cc2)) {
				/* Debug accessory */
				new_cc_state = PD_CC_DFP_DEBUG_ACC;
			} else if (cc_is_rp(cc1) || cc_is_rp(cc2)) {
				new_cc_state = PD_CC_DFP_ATTACHED;
			} else {
				/* No connection any more */
				set_state(port, PD_STATE_SNK_DISCONNECTED);
				timeout = 5 * MSEC;
				break;
			}

			timeout = 20 * MSEC;

			/* Debounce the cc state */
			if (new_cc_state != pd[port].cc_state) {
				pd[port].cc_debounce =
					get_time().val + PD_T_CC_DEBOUNCE;
				pd[port].cc_state = new_cc_state;
				break;
			}
			/* Wait for CC debounce and VBUS present */
			if (get_time().val < pd[port].cc_debounce ||
			    !pd_is_vbus_present(port))
				break;

			if (pd_try_src_enable &&
			    !(pd[port].flags & PD_FLAGS_TRY_SRC)) {
				/*
				 * If TRY_SRC is enabled, but not active,
				 * then force attempt to connect as source.
				 */
				pd[port].try_src_marker =
					get_time().val + PD_T_DRP_TRY;
				pd[port].try_timeout =
					get_time().val + PD_T_TRY_TIMEOUT;
				/* Swap roles to source */
				pd_set_power_role(port, PD_ROLE_SOURCE);
				tcpm_set_cc(port, TYPEC_CC_RP);
				timeout = 2 * MSEC;
				set_state(port, PD_STATE_SRC_DISCONNECTED);
				/* Set flag after the state change */
				pd[port].flags |= PD_FLAGS_TRY_SRC;
				break;
			}

			/* We are attached */
			if (IS_ENABLED(CONFIG_COMMON_RUNTIME))
				hook_notify(HOOK_USB_PD_CONNECT);
			pd[port].polarity = get_snk_polarity(cc1, cc2);
			typec_set_polarity(port, pd[port].polarity);
			/* reset message ID  on connection */
			pd[port].msg_id = 0;
			/* initial data role for sink is UFP */
			pd_set_data_role(port, PD_ROLE_UFP);
			/* Enable Auto Discharge Disconnect */
			tcpm_enable_auto_discharge_disconnect(port, 1);
#if defined(CONFIG_CHARGE_MANAGER)
			typec_curr = usb_get_typec_current_limit(
				pd[port].polarity, cc1, cc2);
			typec_set_input_current_limit(port, typec_curr,
						      TYPE_C_VOLTAGE);
#endif

#ifdef CONFIG_USBC_PPC
			/* Inform PPC that a source is connected. */
			ppc_dev_is_connected(port, PPC_DEV_SRC);
#endif /* CONFIG_USBC_PPC */
			if (IS_ENABLED(CONFIG_USBC_OCP))
				usbc_ocp_snk_is_connected(port, false);

			/* If PD comm is enabled, enable TCPC RX */
			if (pd_comm_is_enabled(port))
				tcpm_set_rx_enable(port, 1);

			/* DFP is attached */
			if (new_cc_state == PD_CC_DFP_ATTACHED ||
			    new_cc_state == PD_CC_DFP_DEBUG_ACC) {
				pd[port].flags |= PD_FLAGS_CHECK_PR_ROLE |
						  PD_FLAGS_CHECK_DR_ROLE |
						  PD_FLAGS_CHECK_IDENTITY;
				if (new_cc_state == PD_CC_DFP_DEBUG_ACC)
					pd[port].flags |=
						PD_FLAGS_TS_DTS_PARTNER;
				set_state(port, PD_STATE_SNK_DISCOVERY);
				timeout = 10 * MSEC;
				hook_call_deferred(
					&pd_usb_billboard_deferred_data,
					PD_T_AME);
			}
			break;
		case PD_STATE_SNK_HARD_RESET_RECOVER:
			if (pd[port].last_state != pd[port].task_state)
				pd[port].flags |= PD_FLAGS_CHECK_IDENTITY;

			if (get_usb_pd_vbus_detect() ==
			    USB_PD_VBUS_DETECT_NONE) {
				/*
				 * Can't measure vbus state so this is the
				 * maximum recovery time for the source.
				 */
				if (pd[port].last_state != pd[port].task_state)
					set_state_timeout(
						port,
						get_time().val + PD_T_SAFE_0V +
							PD_T_SRC_RECOVER_MAX +
							PD_T_SRC_TURN_ON,
						PD_STATE_SNK_DISCONNECTED);
			} else {
#ifndef CONFIG_USB_PD_VBUS_DETECT_NONE
				/* Wait for VBUS to go low and then high*/
				if (pd[port].last_state !=
				    pd[port].task_state) {
					snk_hard_reset_vbus_off = 0;
					set_state_timeout(
						port,
						get_time().val + PD_T_SAFE_0V,
						hard_reset_count <
								PD_HARD_RESET_COUNT ?
							PD_STATE_HARD_RESET_SEND :
							PD_STATE_SNK_DISCOVERY);
				}

				if (!pd_is_vbus_present(port) &&
				    !snk_hard_reset_vbus_off) {
					/* VBUS has gone low, reset timeout */
					snk_hard_reset_vbus_off = 1;
					set_state_timeout(
						port,
						get_time().val +
							PD_T_SRC_RECOVER_MAX +
							PD_T_SRC_TURN_ON,
						PD_STATE_SNK_DISCONNECTED);
				}
				if (pd_is_vbus_present(port) &&
				    snk_hard_reset_vbus_off) {
					/* VBUS went high again */
					set_state(port, PD_STATE_SNK_DISCOVERY);
					timeout = 10 * MSEC;
				}

				/*
				 * Don't need to set timeout because VBUS
				 * changing will trigger an interrupt and
				 * wake us up.
				 */
#endif
			}
			break;
		case PD_STATE_SNK_DISCOVERY:
			/* Wait for source cap expired only if we are enabled */
			if ((pd[port].last_state != pd[port].task_state) &&
			    pd_comm_is_enabled(port)) {
#if defined(CONFIG_USB_PD_TCPM_TCPCI) || defined(CONFIG_USB_PD_TCPM_STUB)
				/*
				 * If we come from hard reset recover state,
				 * then we can process the source capabilities
				 * form partner now, so enable PHY layer
				 * receiving function.
				 */
				if (pd[port].last_state ==
				    PD_STATE_SNK_HARD_RESET_RECOVER)
					tcpm_set_rx_enable(port, 1);
#endif /* CONFIG_USB_PD_TCPM_TCPCI  || CONFIG_USB_PD_TCPM_STUB */
#ifdef CONFIG_USB_PD_RESET_MIN_BATT_SOC
				/*
				 * If the battery has not met a configured safe
				 * level for hard resets, refrain from starting
				 * reset timers as a hard reset could brown out
				 * the board.  Note this may mean that
				 * high-power chargers will stay at 15W until a
				 * reset is sent, depending on boot timing.
				 */
				int batt_soc = usb_get_battery_soc();

				if (batt_soc <
					    CONFIG_USB_PD_RESET_MIN_BATT_SOC ||
				    battery_get_disconnect_state() !=
					    BATTERY_NOT_DISCONNECTED)
					pd[port].flags |=
						PD_FLAGS_SNK_WAITING_BATT;
				else
					pd[port].flags &=
						~PD_FLAGS_SNK_WAITING_BATT;
#endif

				if (pd[port].flags &
				    PD_FLAGS_SNK_WAITING_BATT) {
#ifdef CONFIG_CHARGE_MANAGER
					/*
					 * Configure this port as dedicated for
					 * now, so it won't be de-selected by
					 * the charge manager leaving safe mode.
					 */
					charge_manager_update_dualrole(
						port, CAP_DEDICATED);
#endif
					CPRINTS("C%d: Battery low. "
						"Hold reset timer",
						port);
					/*
					 * If VBUS has never been low, and we
					 * timeout waiting for source cap, try a
					 * soft reset first, in case we were
					 * already in a stable contract before
					 * this boot.
					 */
				} else if (pd[port].flags &
					   PD_FLAGS_VBUS_NEVER_LOW) {
					set_state_timeout(
						port,
						get_time().val +
							PD_T_SINK_WAIT_CAP,
						PD_STATE_SOFT_RESET);
					/*
					 * If we haven't passed hard reset
					 * counter, start SinkWaitCapTimer,
					 * otherwise start NoResponseTimer.
					 */
				} else if (hard_reset_count <
					   PD_HARD_RESET_COUNT) {
					set_state_timeout(
						port,
						get_time().val +
							PD_T_SINK_WAIT_CAP,
						PD_STATE_HARD_RESET_SEND);
				} else if (pd_capable(port)) {
					/* ErrorRecovery */
					set_state_timeout(
						port,
						get_time().val +
							PD_T_NO_RESPONSE,
						PD_STATE_SNK_DISCONNECTED);
				}
#if defined(CONFIG_CHARGE_MANAGER)
				/*
				 * If we didn't come from disconnected, must
				 * have come from some path that did not set
				 * typec current limit. So, set to 0 so that
				 * we guarantee this is revised below.
				 */
				if (pd[port].last_state !=
				    PD_STATE_SNK_DISCONNECTED_DEBOUNCE)
					typec_curr = 0;
#endif
			}

#if defined(CONFIG_CHARGE_MANAGER)
			timeout = PD_T_SINK_ADJ - PD_T_DEBOUNCE;

			/* Check if CC pull-up has changed */
			tcpm_get_cc(port, &cc1, &cc2);
			if (typec_curr !=
			    usb_get_typec_current_limit(pd[port].polarity, cc1,
							cc2)) {
				/* debounce signal by requiring two reads */
				if (typec_curr_change) {
					/* set new input current limit */
					typec_curr = usb_get_typec_current_limit(
						pd[port].polarity, cc1, cc2);
					typec_set_input_current_limit(
						port, typec_curr,
						TYPE_C_VOLTAGE);
				} else {
					/* delay for debounce */
					timeout = PD_T_DEBOUNCE;
				}
				typec_curr_change = !typec_curr_change;
			} else {
				typec_curr_change = 0;
			}
#endif
			break;
		case PD_STATE_SNK_REQUESTED:
			/* Wait for ACCEPT or REJECT */
			if (pd[port].last_state != pd[port].task_state) {
				pd[port].flags |= PD_FLAGS_CHECK_VCONN_STATE;
				hard_reset_count = 0;
				set_state_timeout(port,
						  get_time().val +
							  PD_T_SENDER_RESPONSE,
						  PD_STATE_HARD_RESET_SEND);
			}
			break;
		case PD_STATE_SNK_TRANSITION:
			/* Wait for PS_RDY */
			if (pd[port].last_state != pd[port].task_state)
				set_state_timeout(port,
						  get_time().val +
							  PD_T_PS_TRANSITION,
						  PD_STATE_HARD_RESET_SEND);
			break;
		case PD_STATE_SNK_READY:
			timeout = 20 * MSEC;

			/*
			 * Don't send any traffic yet until our holdoff timer
			 * has expired.  Some devices are chatty once we reach
			 * the SNK_READY state and we may end up in a collision
			 * of messages if we try to immediately send our
			 * interrogations.
			 */
			if (get_time().val <=
			    pd[port].ready_state_holdoff_timer)
				break;

			/*
			 * Don't send any PD traffic if we woke up due to
			 * incoming packet or if VDO response pending to avoid
			 * collisions.
			 */
			if (incoming_packet ||
			    (pd[port].vdm_state == VDM_STATE_BUSY))
				break;

			/* Check for new power to request */
			if (pd[port].new_power_request) {
				if (pd_send_request_msg(port, 0) != EC_SUCCESS)
					set_state(port, PD_STATE_SOFT_RESET);
				break;
			}

			/* Check power role policy, which may trigger a swap */
			if (pd[port].flags & PD_FLAGS_CHECK_PR_ROLE) {
				pd_check_pr_role(port, PD_ROLE_SINK,
						 pd[port].flags);
				pd[port].flags &= ~PD_FLAGS_CHECK_PR_ROLE;
				break;
			}

			/* Check data role policy, which may trigger a swap */
			if (pd[port].flags & PD_FLAGS_CHECK_DR_ROLE) {
				pd_check_dr_role(port, pd[port].data_role,
						 pd[port].flags);
				pd[port].flags &= ~PD_FLAGS_CHECK_DR_ROLE;
				break;
			}

			/* Check for Vconn source, which may trigger a swap */
			if (pd[port].flags & PD_FLAGS_CHECK_VCONN_STATE) {
				/*
				 * Ref: Section 2.6.2 of both
				 * USB-PD Spec Revision 2.0, Version 1.3 &
				 * USB-PD Spec Revision 3.0, Version 2.0
				 * During Explicit contract the Sink can
				 * initiate or receive a request an exchange
				 * of VCONN Source.
				 */
				pd_try_execute_vconn_swap(port, pd[port].flags);
				pd[port].flags &= ~PD_FLAGS_CHECK_VCONN_STATE;
				break;
			}

			/* If DFP, send discovery SVDMs */
			if (pd[port].data_role == PD_ROLE_DFP &&
			    (pd[port].flags & PD_FLAGS_CHECK_IDENTITY)) {
				pd_send_vdm(port, USB_SID_PD,
					    CMD_DISCOVER_IDENT, NULL, 0);
				pd[port].flags &= ~PD_FLAGS_CHECK_IDENTITY;
				break;
			}

			/* Sent all messages, don't need to wake very often */
			timeout = 200 * MSEC;
			break;
		case PD_STATE_SNK_SWAP_INIT:
			if (pd[port].last_state != pd[port].task_state) {
				res = send_control(port, PD_CTRL_PR_SWAP);
				if (res < 0) {
					timeout = 10 * MSEC;
					/*
					 * If failed to get goodCRC, send
					 * soft reset, otherwise ignore
					 * failure.
					 */
					set_state(port,
						  res == -1 ?
							  PD_STATE_SOFT_RESET :
							  PD_STATE_SNK_READY);
					break;
				}
				/* Wait for accept or reject */
				set_state_timeout(port,
						  get_time().val +
							  PD_T_SENDER_RESPONSE,
						  PD_STATE_SNK_READY);
			}
			break;
		case PD_STATE_SNK_SWAP_SNK_DISABLE:
			/* Stop drawing power */
			pd_set_input_current_limit(port, 0, 0);
#ifdef CONFIG_CHARGE_MANAGER
			typec_set_input_current_limit(port, 0, 0);
			charge_manager_set_ceil(port, CEIL_REQUESTOR_PD,
						CHARGE_CEIL_NONE);
#endif
			set_state(port, PD_STATE_SNK_SWAP_SRC_DISABLE);
			timeout = 10 * MSEC;
			break;
		case PD_STATE_SNK_SWAP_SRC_DISABLE:
			/* Wait for PS_RDY */
			if (pd[port].last_state != pd[port].task_state)
				set_state_timeout(port,
						  get_time().val +
							  PD_T_PS_SOURCE_OFF,
						  PD_STATE_HARD_RESET_SEND);
			break;
		case PD_STATE_SNK_SWAP_STANDBY:
			if (pd[port].last_state != pd[port].task_state) {
				/* Switch to Rp and enable power supply. */
				tcpm_set_cc(port, TYPEC_CC_RP);
				if (pd_set_power_supply_ready(port)) {
					/* Restore Rd */
					tcpm_set_cc(port, TYPEC_CC_RD);
					timeout = 10 * MSEC;
					set_state(port,
						  PD_STATE_SNK_DISCONNECTED);
					break;
				}
				/* Wait for power supply to turn on */
				set_state_timeout(
					port,
					get_time().val +
						PD_POWER_SUPPLY_TURN_ON_DELAY,
					PD_STATE_SNK_SWAP_COMPLETE);
			}
			break;
		case PD_STATE_SNK_SWAP_COMPLETE:
			/* Send PS_RDY and change to source role */
			res = send_control(port, PD_CTRL_PS_RDY);
			if (res < 0) {
				/* Restore Rd */
				tcpm_set_cc(port, TYPEC_CC_RD);
				pd_power_supply_reset(port);
				timeout = 10 * MSEC;
				set_state(port, PD_STATE_SNK_DISCONNECTED);
				break;
			}

			/* Don't send GET_SINK_CAP on swap */
			snk_cap_count = PD_SNK_CAP_RETRIES + 1;
			caps_count = 0;
			pd[port].msg_id = 0;
			pd_set_power_role(port, PD_ROLE_SOURCE);
			pd_update_roles(port);
			set_state(port, PD_STATE_SRC_DISCOVERY);
			timeout = 10 * MSEC;
			break;
#ifdef CONFIG_USBC_VCONN_SWAP
		case PD_STATE_VCONN_SWAP_SEND:
			if (pd[port].last_state != pd[port].task_state) {
				res = send_control(port, PD_CTRL_VCONN_SWAP);
				if (res < 0) {
					timeout = 10 * MSEC;
					/*
					 * If failed to get goodCRC, send
					 * soft reset, otherwise ignore
					 * failure.
					 */
					set_state(port,
						  res == -1 ?
							  PD_STATE_SOFT_RESET :
							  READY_RETURN_STATE(
								  port));
					break;
				}
				/* Wait for accept or reject */
				set_state_timeout(port,
						  get_time().val +
							  PD_T_SENDER_RESPONSE,
						  READY_RETURN_STATE(port));
			}
			break;
		case PD_STATE_VCONN_SWAP_INIT:
			if (pd[port].last_state != pd[port].task_state) {
				if (!(pd[port].flags & PD_FLAGS_VCONN_ON)) {
					/* Turn VCONN on and wait for it */
					set_vconn(port, 1);
					set_state_timeout(
						port,
						get_time().val +
							CONFIG_USBC_VCONN_SWAP_DELAY_US,
						PD_STATE_VCONN_SWAP_READY);
				} else {
					set_state_timeout(
						port,
						get_time().val +
							PD_T_VCONN_SOURCE_ON,
						READY_RETURN_STATE(port));
				}
			}
			break;
		case PD_STATE_VCONN_SWAP_READY:
			if (pd[port].last_state != pd[port].task_state) {
				if (!(pd[port].flags & PD_FLAGS_VCONN_ON)) {
					/* VCONN is now on, send PS_RDY */
					pd_set_vconn_role(port,
							  PD_ROLE_VCONN_ON);
					res = send_control(port,
							   PD_CTRL_PS_RDY);
					if (res == -1) {
						timeout = 10 * MSEC;
						/*
						 * If failed to get goodCRC,
						 * send soft reset
						 */
						set_state(port,
							  PD_STATE_SOFT_RESET);
						break;
					}
					set_state(port,
						  READY_RETURN_STATE(port));
				} else {
					/* Turn VCONN off and wait for it */
					set_vconn(port, 0);
					pd_set_vconn_role(port,
							  PD_ROLE_VCONN_OFF);
					set_state_timeout(
						port,
						get_time().val +
							CONFIG_USBC_VCONN_SWAP_DELAY_US,
						READY_RETURN_STATE(port));
				}
			}
			break;
#endif /* CONFIG_USBC_VCONN_SWAP */
#endif /* CONFIG_USB_PD_DUAL_ROLE */
		case PD_STATE_SOFT_RESET:
			if (pd[port].last_state != pd[port].task_state) {
				/* Message ID of soft reset is always 0 */
				invalidate_last_message_id(port);
				pd[port].msg_id = 0;
				res = send_control(port, PD_CTRL_SOFT_RESET);

				/* if soft reset failed, try hard reset. */
				if (res < 0) {
					set_state(port,
						  PD_STATE_HARD_RESET_SEND);
					timeout = 5 * MSEC;
					break;
				}

				set_state_timeout(port,
						  get_time().val +
							  PD_T_SENDER_RESPONSE,
						  PD_STATE_HARD_RESET_SEND);
			}
			break;
		case PD_STATE_HARD_RESET_SEND:
			hard_reset_count++;
			if (pd[port].last_state != pd[port].task_state) {
				hard_reset_sent = 0;
				pd[port].hard_reset_complete_timer = 0;
			}
#ifdef CONFIG_CHARGE_MANAGER
			if (pd[port].last_state == PD_STATE_SNK_DISCOVERY ||
			    (pd[port].last_state == PD_STATE_SOFT_RESET &&
			     (pd[port].flags & PD_FLAGS_VBUS_NEVER_LOW))) {
				pd[port].flags &= ~PD_FLAGS_VBUS_NEVER_LOW;
				/*
				 * If discovery timed out, assume that we
				 * have a dedicated charger attached. This
				 * may not be a correct assumption according
				 * to the specification, but it generally
				 * works in practice and the harmful
				 * effects of a wrong assumption here
				 * are minimal.
				 */
				charge_manager_update_dualrole(port,
							       CAP_DEDICATED);
			}
#endif

			if (hard_reset_sent)
				break;

			if (pd_transmit(port, TCPCI_MSG_TX_HARD_RESET, 0, NULL,
					AMS_START) < 0) {
				/*
				 * likely a non-idle channel
				 * TCPCI r2.0 v1.0 4.4.15:
				 * the TCPC does not retry HARD_RESET
				 * but we can try periodically until the timer
				 * expires.
				 */
				now = get_time();
				if (pd[port].hard_reset_complete_timer == 0) {
					pd[port].hard_reset_complete_timer =
						now.val +
						PD_T_HARD_RESET_COMPLETE;
					timeout = PD_T_HARD_RESET_RETRY;
					break;
				}
				if (now.val <
				    pd[port].hard_reset_complete_timer) {
					CPRINTS("C%d: Retrying hard reset",
						port);
					timeout = PD_T_HARD_RESET_RETRY;
					break;
				}
				/*
				 * PD 2.0 spec, section 6.5.11.1
				 * Pretend TX_HARD_RESET succeeded after
				 * timeout.
				 */
			}

			hard_reset_sent = 1;
			/*
			 * If we are source, delay before cutting power
			 * to allow sink time to get hard reset.
			 */
			if (pd[port].power_role == PD_ROLE_SOURCE) {
				set_state_timeout(port,
						  get_time().val +
							  PD_T_PS_HARD_RESET,
						  PD_STATE_HARD_RESET_EXECUTE);
			} else {
				set_state(port, PD_STATE_HARD_RESET_EXECUTE);
				timeout = 10 * MSEC;
			}
			break;
		case PD_STATE_HARD_RESET_EXECUTE:
#ifdef CONFIG_USB_PD_DUAL_ROLE
			/*
			 * If hard reset while in the last stages of power
			 * swap, then we need to restore our CC resistor.
			 */
			if (pd[port].last_state == PD_STATE_SNK_SWAP_STANDBY)
				tcpm_set_cc(port, TYPEC_CC_RD);
#endif

			/* reset our own state machine */
			pd_execute_hard_reset(port);
			timeout = 10 * MSEC;
			break;
#ifdef CONFIG_COMMON_RUNTIME
		case PD_STATE_BIST_RX:
			send_bist_cmd(port);
			/* Delay at least enough for partner to finish BIST */
			timeout = PD_T_BIST_RECEIVE + 20 * MSEC;
			/* Set to appropriate port disconnected state */
			set_state(port, DUAL_ROLE_IF_ELSE(
						port, PD_STATE_SNK_DISCONNECTED,
						PD_STATE_SRC_DISCONNECTED));
			break;
		case PD_STATE_BIST_TX:
			pd_transmit(port, TCPCI_MSG_TX_BIST_MODE_2, 0, NULL,
				    AMS_START);
			/* Delay at least enough to finish sending BIST */
			timeout = PD_T_BIST_TRANSMIT + 20 * MSEC;
			/* Set to appropriate port disconnected state */
			set_state(port, DUAL_ROLE_IF_ELSE(
						port, PD_STATE_SNK_DISCONNECTED,
						PD_STATE_SRC_DISCONNECTED));
			break;
#endif
#ifdef CONFIG_USB_PD_DUAL_ROLE_AUTO_TOGGLE
		case PD_STATE_DRP_AUTO_TOGGLE: {
			enum pd_drp_next_states next_state;

			assert(auto_toggle_supported);

#ifdef CONFIG_USB_PD_TCPC_LOW_POWER
			/*
			 * If SW decided we should be in a low power state and
			 * the CC lines did not change, then don't talk with the
			 * TCPC otherwise we might wake it up.
			 */
			if (pd[port].flags & PD_FLAGS_LPM_REQUESTED &&
			    !(evt & PD_EVENT_CC))
				break;

			/*
			 * Debounce low power mode exit.  Some TCPCs need time
			 * for the CC_STATUS register to be stable after exiting
			 * low power mode.
			 */
			if (pd[port].flags & PD_FLAGS_LPM_EXIT) {
				uint64_t now;

				now = get_time().val;
				if (now < pd[port].low_power_exit_time)
					break;

				CPRINTS("TCPC p%d Exit Low Power Mode done",
					port);
				pd[port].flags &= ~PD_FLAGS_LPM_EXIT;
			}
#endif

			/*
			 * Check for connection
			 *
			 * Send FALSE for supports_auto_toggle to not change
			 * the current return value of UNATTACHED instead of
			 * the auto-toggle ATTACHED_WAIT response for TCPMv1.
			 */
			tcpm_get_cc(port, &cc1, &cc2);

			next_state = drp_auto_toggle_next_state(
				&pd[port].drp_sink_time, pd[port].power_role,
				drp_state[port], cc1, cc2, false);

#ifdef CONFIG_USB_PD_TCPC_LOW_POWER
			/*
			 * The next state is not determined just by what is
			 * attached, but also depends on DRP_STATE. Regardless
			 * of next state, if nothing is attached, then always
			 * request low power mode.
			 */
			if (cc_is_open(cc1, cc2))
				pd[port].flags |= PD_FLAGS_LPM_REQUESTED;
#endif
			if (next_state == DRP_TC_DEFAULT) {
				if (PD_DEFAULT_STATE(port) ==
				    PD_STATE_SNK_DISCONNECTED)
					next_state = DRP_TC_UNATTACHED_SNK;
				else
					next_state = DRP_TC_UNATTACHED_SRC;
			}

			if (next_state == DRP_TC_UNATTACHED_SNK) {
				/*
				 * The TCPCI comes out of auto toggle with
				 * a prospective connection.  It is expecting
				 * us to set the CC lines to what it is
				 * thinking is best or it goes direct back to
				 * unattached.  So get the SNK polarity to
				 * be able to setup the CC lines to avoid this.
				 */
				pd[port].polarity = get_snk_polarity(cc1, cc2);

				tcpm_set_cc(port, TYPEC_CC_RD);
				pd_set_power_role(port, PD_ROLE_SINK);
				timeout = 2 * MSEC;
				set_state(port, PD_STATE_SNK_DISCONNECTED);
			} else if (next_state == DRP_TC_UNATTACHED_SRC) {
				/*
				 * The TCPCI comes out of auto toggle with
				 * a prospective connection.  It is expecting
				 * us to set the CC lines to what it is
				 * thinking is best or it goes direct back to
				 * unattached.  So get the SNK polarity to
				 * be able to setup the CC lines to avoid this.
				 */
				pd[port].polarity = get_src_polarity(cc1, cc2);

				tcpm_set_cc(port, TYPEC_CC_RP);
				pd_set_power_role(port, PD_ROLE_SOURCE);
				timeout = 2 * MSEC;
				set_state(port, PD_STATE_SRC_DISCONNECTED);
			} else {
				/*
				 * We are staying in PD_STATE_DRP_AUTO_TOGGLE,
				 * therefore enable auto-toggle.
				 */
				tcpm_enable_drp_toggle(port);
				pd[port].flags |= PD_FLAGS_TCPC_DRP_TOGGLE;
				set_state(port, PD_STATE_DRP_AUTO_TOGGLE);
			}

			break;
		}
#endif
		default:
			break;
		}

		pd[port].last_state = this_state;

		/*
		 * Check for state timeout, and if not check if need to adjust
		 * timeout value to wake up on the next state timeout.
		 */
		now = get_time();
		if (pd[port].timeout) {
			if (now.val >= pd[port].timeout) {
				set_state(port, pd[port].timeout_state);
				/* On a state timeout, run next state soon */
				timeout = timeout < 10 * MSEC ? timeout :
								10 * MSEC;
			} else if (pd[port].timeout - now.val < timeout) {
				timeout = pd[port].timeout - now.val;
			}
		}

#ifdef CONFIG_USB_PD_TCPC_LOW_POWER
		/* Determine if we need to put the TCPC in low power mode */
		if (pd[port].flags & PD_FLAGS_LPM_REQUESTED &&
		    !(pd[port].flags & PD_FLAGS_LPM_ENGAGED)) {
			int64_t time_left;

			/* If any task prevents LPM, wait another debounce */
			if (pd[port].tasks_preventing_lpm) {
				pd[port].low_power_time =
					PD_LPM_DEBOUNCE_US + now.val;
			}

			time_left = pd[port].low_power_time - now.val;
			if (time_left <= 0) {
				pd[port].flags |= PD_FLAGS_LPM_ENGAGED;
				pd[port].flags |= PD_FLAGS_LPM_TRANSITION;
				tcpm_enter_low_power_mode(port);
				pd[port].flags &= ~PD_FLAGS_LPM_TRANSITION;
				tcpc_prints("Enter Low Power Mode", port);
				timeout = -1;
			} else if (timeout < 0 || timeout > time_left) {
				timeout = time_left;
			}
		}
#endif

		/* Check for disconnection if we're connected */
		if (!pd_is_connected(port))
			continue;
#ifdef CONFIG_USB_PD_DUAL_ROLE
		if (pd_is_power_swapping(port))
			continue;
#endif
		if (pd[port].power_role == PD_ROLE_SOURCE) {
			/* Source: detect disconnect by monitoring CC */
			tcpm_get_cc(port, &cc1, &cc2);
			if (polarity_rm_dts(pd[port].polarity))
				cc1 = cc2;
			if (cc1 == TYPEC_CC_VOLT_OPEN) {
				set_state(port, PD_STATE_SRC_DISCONNECTED);
				/* Debouncing */
				timeout = 10 * MSEC;
#ifdef CONFIG_USB_PD_DUAL_ROLE
				/*
				 * If Try.SRC is configured, then ATTACHED_SRC
				 * needs to transition to TryWait.SNK. Change
				 * power role to SNK and start state timer.
				 */
				if (pd_try_src_enable) {
					/* Swap roles to sink */
					pd_set_power_role(port, PD_ROLE_SINK);
					tcpm_set_cc(port, TYPEC_CC_RD);
					/* Set timer for TryWait.SNK state */
					pd[port].try_src_marker =
						get_time().val + PD_T_DEBOUNCE;
					/* Advance to TryWait.SNK state */
					set_state(port,
						  PD_STATE_SNK_DISCONNECTED);
					/* Mark state as TryWait.SNK */
					pd[port].flags |= PD_FLAGS_TRY_SRC;
				}
#endif
			}
		}
#ifdef CONFIG_USB_PD_DUAL_ROLE
		/*
		 * Sink disconnect if VBUS is low and
		 *  1) we are not waiting for VBUS to debounce after a power
		 *     role swap.
		 *  2) we are not recovering from a hard reset.
		 */
		if (pd[port].power_role == PD_ROLE_SINK &&
		    pd[port].vbus_debounce_time < get_time().val &&
		    !pd_is_vbus_present(port) &&
		    pd[port].task_state != PD_STATE_SNK_HARD_RESET_RECOVER &&
		    pd[port].task_state != PD_STATE_HARD_RESET_EXECUTE) {
			/* Sink: detect disconnect by monitoring VBUS */
			set_state(port, PD_STATE_SNK_DISCONNECTED);
			/* set timeout small to reconnect fast */
			timeout = 5 * MSEC;
		}
#endif /* CONFIG_USB_PD_DUAL_ROLE */
	}
}

#ifdef CONFIG_USB_PD_DUAL_ROLE
static void pd_chipset_resume(void)
{
	int i;

	for (i = 0; i < board_get_usb_pd_port_count(); i++) {
#ifdef CONFIG_CHARGE_MANAGER
		if (charge_manager_get_active_charge_port() != i)
#endif
			pd[i].flags |= PD_FLAGS_CHECK_PR_ROLE |
				       PD_FLAGS_CHECK_DR_ROLE;
		pd_set_dual_role(i, PD_DRP_TOGGLE_ON);
	}

	CPRINTS("PD:S3->S0");
}
DECLARE_HOOK(HOOK_CHIPSET_RESUME, pd_chipset_resume, HOOK_PRIO_DEFAULT);

static void pd_chipset_suspend(void)
{
	int i;

	for (i = 0; i < board_get_usb_pd_port_count(); i++)
		pd_set_dual_role(i, PD_DRP_TOGGLE_OFF);
	CPRINTS("PD:S0->S3");
}
DECLARE_HOOK(HOOK_CHIPSET_SUSPEND, pd_chipset_suspend, HOOK_PRIO_DEFAULT);

static void pd_chipset_startup(void)
{
	int i;

	for (i = 0; i < board_get_usb_pd_port_count(); i++) {
		pd_set_dual_role_no_wakeup(i, PD_DRP_TOGGLE_OFF);
		pd[i].flags |= PD_FLAGS_CHECK_IDENTITY;
		task_set_event(PD_PORT_TO_TASK_ID(i),
			       PD_EVENT_POWER_STATE_CHANGE |
				       PD_EVENT_UPDATE_DUAL_ROLE);
	}
	CPRINTS("PD:S5->S3");
}
DECLARE_HOOK(HOOK_CHIPSET_STARTUP, pd_chipset_startup, HOOK_PRIO_DEFAULT);

static void pd_chipset_shutdown(void)
{
	int i;

	for (i = 0; i < board_get_usb_pd_port_count(); i++) {
		pd_set_dual_role_no_wakeup(i, PD_DRP_FORCE_SINK);
		task_set_event(PD_PORT_TO_TASK_ID(i),
			       PD_EVENT_POWER_STATE_CHANGE |
				       PD_EVENT_UPDATE_DUAL_ROLE);
	}
	CPRINTS("PD:S3->S5");
}
DECLARE_HOOK(HOOK_CHIPSET_SHUTDOWN, pd_chipset_shutdown, HOOK_PRIO_DEFAULT);

#endif /* CONFIG_USB_PD_DUAL_ROLE */

#ifdef CONFIG_COMMON_RUNTIME

static void pd_control_resume(int port)
{
	if (pd[port].task_state != PD_STATE_SUSPENDED)
		return;

	set_state(port, PD_DEFAULT_STATE(port));
	/*
	 * Since we did not service interrupts while we were suspended,
	 * see if there is a waiting interrupt to be serviced. If the
	 * interrupt line isn't asserted, we won't communicate with the
	 * TCPC.
	 */
	if (IS_ENABLED(HAS_TASK_PD_INT_C0))
		schedule_deferred_pd_interrupt(port);
	task_wake(PD_PORT_TO_TASK_ID(port));
}

/*
 * (suspend=1) request pd_task transition to the suspended state.  hang
 * around for a while until we observe the state change.  this can
 * take a while (like 300ms) on startup when pd_task is sleeping in
 * tcpci_tcpm_init.
 *
 * (suspend=0) force pd_task out of the suspended state and into the
 * port's default state.
 */

void pd_set_suspend(int port, int suspend)
{
	int tries = 300;

	if (suspend) {
		pd[port].req_suspend_state = 1;
		do {
			task_wake(PD_PORT_TO_TASK_ID(port));
			if (pd[port].task_state == PD_STATE_SUSPENDED)
				break;
			crec_msleep(1);
		} while (--tries != 0);
		if (!tries)
			tcpc_prints("set_suspend failed!", port);
	} else {
		pd_control_resume(port);
	}
}

int pd_is_port_enabled(int port)
{
	switch (pd[port].task_state) {
	case PD_STATE_DISABLED:
	case PD_STATE_SUSPENDED:
		return 0;
	default:
		return 1;
	}
}

void pd_set_error_recovery(int port)
{
	/* No-op for linking.  Can be implemented later if necessary */
}

#if defined(CONFIG_USB_PD_ALT_MODE) && !defined(CONFIG_USB_PD_ALT_MODE_DFP)
void pd_send_hpd(int port, enum hpd_event hpd)
{
	uint32_t data[1];
	int opos = pd_alt_mode(port, TCPCI_MSG_SOP, USB_SID_DISPLAYPORT);
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
	/* Wait until VDM is done. */
	while (pd[0].vdm_state > 0)
		task_wait_event(USB_PD_RX_TMOUT_US *
				(CONFIG_PD_RETRY_COUNT + 1));
}
#endif

int pd_fetch_acc_log_entry(int port)
{
	timestamp_t timeout;

	/* Cannot send a VDM now, the host should retry */
	if (pd[port].vdm_state > 0)
		return pd[port].vdm_state == VDM_STATE_BUSY ?
			       EC_RES_BUSY :
			       EC_RES_UNAVAILABLE;

	pd_send_vdm(port, USB_VID_GOOGLE, VDO_CMD_GET_LOG, NULL, 0);
	timeout.val = get_time().val + 75 * MSEC;

	/* Wait until VDM is done */
	while ((pd[port].vdm_state > 0) && (get_time().val < timeout.val))
		task_wait_event(10 * MSEC);

	if (pd[port].vdm_state > 0)
		return EC_RES_TIMEOUT;
	else if (pd[port].vdm_state < 0)
		return EC_RES_ERROR;

	return EC_RES_SUCCESS;
}

#ifdef CONFIG_USB_PD_DUAL_ROLE
void pd_request_source_voltage(int port, int mv)
{
	pd_set_max_voltage(mv);

	if (pd[port].task_state == PD_STATE_SNK_READY ||
	    pd[port].task_state == PD_STATE_SNK_TRANSITION) {
		/* Set flag to send new power request in pd_task */
		pd[port].new_power_request = 1;
	} else {
		pd_set_power_role(port, PD_ROLE_SINK);
		tcpm_set_cc(port, TYPEC_CC_RD);
		set_state(port, PD_STATE_SNK_DISCONNECTED);
	}

	task_wake(PD_PORT_TO_TASK_ID(port));
}

void pd_set_external_voltage_limit(int port, int mv)
{
	pd_set_max_voltage(mv);

	if (pd[port].task_state == PD_STATE_SNK_READY ||
	    pd[port].task_state == PD_STATE_SNK_TRANSITION) {
		/* Set flag to send new power request in pd_task */
		pd[port].new_power_request = 1;
		task_wake(PD_PORT_TO_TASK_ID(port));
	}
}

void pd_update_contract(int port)
{
	if ((pd[port].task_state >= PD_STATE_SRC_NEGOCIATE) &&
	    (pd[port].task_state <= PD_STATE_SRC_GET_SINK_CAP)) {
		pd[port].flags |= PD_FLAGS_UPDATE_SRC_CAPS;
		task_wake(PD_PORT_TO_TASK_ID(port));
	}
}

#endif /* CONFIG_USB_PD_DUAL_ROLE */

#if defined(CONFIG_CMD_PD) && defined(CONFIG_CMD_PD_FLASH)
int hex8tou32(char *str, uint32_t *val)
{
	char *ptr = str;
	uint32_t tmp = 0;

	while (*ptr) {
		char c = *ptr++;

		if (c >= '0' && c <= '9')
			tmp = (tmp << 4) + (c - '0');
		else if (c >= 'A' && c <= 'F')
			tmp = (tmp << 4) + (c - 'A' + 10);
		else if (c >= 'a' && c <= 'f')
			tmp = (tmp << 4) + (c - 'a' + 10);
		else
			return EC_ERROR_INVAL;
	}
	if (ptr != str + 8)
		return EC_ERROR_INVAL;
	*val = tmp;
	return EC_SUCCESS;
}

/*
 * Flash a USB PD device using the ChromeOS Vendor Defined Command.
 *
 * @param argc number arguments in argv. Must be greater than 3.
 * @param argv [1] is the usb port
 *             [2] unused
 *             [3] is the command {"erase", "rebooot", "signature",
 *                                 "info", "version", "write"}
 *             [4] if command was "write", then this will be the
 *                 start of the data that will be written.
 * @return EC_SUCCESS on success, else EC_ERROR_PARAM_COUNT or EC_ERROR_PARAM2
 *         on failure.
 */
static int remote_flashing(int argc, char **argv)
{
	int port, cnt, cmd;
	uint32_t data[VDO_MAX_SIZE - 1];
	char *e;
	static int flash_offset[CONFIG_USB_PD_PORT_MAX_COUNT];

	if (argc < 4 || argc > (VDO_MAX_SIZE + 4 - 1))
		return EC_ERROR_PARAM_COUNT;

	port = strtoi(argv[1], &e, 10);
	if (*e || port >= board_get_usb_pd_port_count())
		return EC_ERROR_PARAM2;

	cnt = 0;
	if (!strcasecmp(argv[3], "erase")) {
		cmd = VDO_CMD_FLASH_ERASE;
		flash_offset[port] = 0;
		ccprintf("ERASE ...");
	} else if (!strcasecmp(argv[3], "reboot")) {
		cmd = VDO_CMD_REBOOT;
		ccprintf("REBOOT ...");
	} else if (!strcasecmp(argv[3], "signature")) {
		cmd = VDO_CMD_ERASE_SIG;
		ccprintf("ERASE SIG ...");
	} else if (!strcasecmp(argv[3], "info")) {
		cmd = VDO_CMD_READ_INFO;
		ccprintf("INFO...");
	} else if (!strcasecmp(argv[3], "version")) {
		cmd = VDO_CMD_VERSION;
		ccprintf("VERSION...");
	} else {
		int i;

		argc -= 3;
		for (i = 0; i < argc; i++)
			if (hex8tou32(argv[i + 3], data + i))
				return EC_ERROR_INVAL;
		cmd = VDO_CMD_FLASH_WRITE;
		cnt = argc;
		ccprintf("WRITE %d @%04x ...", argc * 4, flash_offset[port]);
		flash_offset[port] += argc * 4;
	}

	pd_send_vdm(port, USB_VID_GOOGLE, cmd, data, cnt);

	/* Wait until VDM is done */
	while (pd[port].vdm_state > 0)
		task_wait_event(100 * MSEC);

	ccprintf("DONE %d\n", pd[port].vdm_state);
	return EC_SUCCESS;
}
#endif /* defined(CONFIG_CMD_PD) && defined(CONFIG_CMD_PD_FLASH) */

static int command_pd(int argc, const char **argv)
{
	int port;
	char *e;

	if (argc < 2)
		return EC_ERROR_PARAM_COUNT;

	if (!strcasecmp(argv[1], "dump")) {
		if (argc >= 3) {
#ifdef CONFIG_USB_PD_DEBUG_LEVEL
			return EC_ERROR_PARAM2;
#else
			int level = strtoi(argv[2], &e, 10);
			if (*e)
				return EC_ERROR_PARAM2;
			debug_level = level;
#endif
		}
		ccprintf("debug=%d\n", debug_level);

		return EC_SUCCESS;
	}
#ifdef CONFIG_CMD_PD
#ifdef CONFIG_CMD_PD_DEV_DUMP_INFO
	else if (!strncasecmp(argv[1], "rwhashtable", 3)) {
		int i;
		struct ec_params_usb_pd_rw_hash_entry *p;
		for (i = 0; i < RW_HASH_ENTRIES; i++) {
			p = &rw_hash_table[i];
			pd_dev_dump_info(p->dev_id, p->dev_rw_hash);
		}
		return EC_SUCCESS;
	}
#endif /* CONFIG_CMD_PD_DEV_DUMP_INFO */
#ifdef CONFIG_USB_PD_TRY_SRC
	else if (!strncasecmp(argv[1], "trysrc", 6)) {
		int enable;

		if (argc >= 3) {
			enable = strtoi(argv[2], &e, 10);
			if (*e)
				return EC_ERROR_PARAM3;
			pd_try_src_enable = enable ? 1 : 0;
		}

		ccprintf("Try.SRC %s\n", pd_try_src_enable ? "on" : "off");
		return EC_SUCCESS;
	}
#endif
#endif
	else if (!strcasecmp(argv[1], "version")) {
		ccprintf("%d\n", PD_STACK_VERSION);
		return EC_SUCCESS;
	}

	/* command: pd <port> <subcmd> [args] */
	port = strtoi(argv[1], &e, 10);
	if (argc < 3)
		return EC_ERROR_PARAM_COUNT;
	if (*e || port >= board_get_usb_pd_port_count())
		return EC_ERROR_PARAM2;
#if defined(CONFIG_CMD_PD) && defined(CONFIG_USB_PD_DUAL_ROLE)

	if (!strcasecmp(argv[2], "tx")) {
		set_state(port, PD_STATE_SNK_DISCOVERY);
		task_wake(PD_PORT_TO_TASK_ID(port));
	} else if (!strcasecmp(argv[2], "bist_rx")) {
		set_state(port, PD_STATE_BIST_RX);
		task_wake(PD_PORT_TO_TASK_ID(port));
	} else if (!strcasecmp(argv[2], "bist_tx")) {
		if (*e)
			return EC_ERROR_PARAM3;
		set_state(port, PD_STATE_BIST_TX);
		task_wake(PD_PORT_TO_TASK_ID(port));
	} else if (!strcasecmp(argv[2], "charger")) {
		pd_set_power_role(port, PD_ROLE_SOURCE);
		tcpm_set_cc(port, TYPEC_CC_RP);
		set_state(port, PD_STATE_SRC_DISCONNECTED);
		task_wake(PD_PORT_TO_TASK_ID(port));
	} else if (!strncasecmp(argv[2], "dev", 3)) {
		int max_volt;
		if (argc >= 4)
			max_volt = strtoi(argv[3], &e, 10) * 1000;
		else
			max_volt = pd_get_max_voltage();

		pd_request_source_voltage(port, max_volt);
		ccprintf("max req: %dmV\n", max_volt);
	} else if (!strcasecmp(argv[2], "disable")) {
		pd_comm_enable(port, 0);
		ccprintf("Port C%d disable\n", port);
		return EC_SUCCESS;
	} else if (!strcasecmp(argv[2], "enable")) {
		pd_comm_enable(port, 1);
		ccprintf("Port C%d enabled\n", port);
		return EC_SUCCESS;
	} else if (!strncasecmp(argv[2], "hard", 4)) {
		set_state(port, PD_STATE_HARD_RESET_SEND);
		task_wake(PD_PORT_TO_TASK_ID(port));
	} else if (!strncasecmp(argv[2], "info", 4)) {
		int i;
		ccprintf("Hash ");
		for (i = 0; i < PD_RW_HASH_SIZE / 4; i++)
			ccprintf("%08x ", pd[port].dev_rw_hash[i]);
		ccprintf("\nImage %s\n",
			 ec_image_to_string(pd[port].current_image));
	} else if (!strncasecmp(argv[2], "soft", 4)) {
		set_state(port, PD_STATE_SOFT_RESET);
		task_wake(PD_PORT_TO_TASK_ID(port));
	} else if (!strncasecmp(argv[2], "swap", 4)) {
		if (argc < 4)
			return EC_ERROR_PARAM_COUNT;

		if (!strncasecmp(argv[3], "power", 5))
			pd_request_power_swap(port);
		else if (!strncasecmp(argv[3], "data", 4))
			pd_request_data_swap(port);
#ifdef CONFIG_USBC_VCONN_SWAP
		else if (!strncasecmp(argv[3], "vconn", 5))
			pd_request_vconn_swap(port);
#endif
		else
			return EC_ERROR_PARAM3;
	} else if (!strncasecmp(argv[2], "srccaps", 7)) {
		pd_srccaps_dump(port);
	} else if (!strncasecmp(argv[2], "ping", 4)) {
		int enable;

		if (argc > 3) {
			enable = strtoi(argv[3], &e, 10);
			if (*e)
				return EC_ERROR_PARAM3;
			pd_ping_enable(port, enable);
		}

		ccprintf("Pings %s\n",
			 (pd[port].flags & PD_FLAGS_PING_ENABLED) ? "on" :
								    "off");
	} else if (!strncasecmp(argv[2], "vdm", 3)) {
		if (argc < 4)
			return EC_ERROR_PARAM_COUNT;

		if (!strncasecmp(argv[3], "ping", 4)) {
			uint32_t enable;
			if (argc < 5)
				return EC_ERROR_PARAM_COUNT;
			enable = strtoi(argv[4], &e, 10);
			if (*e)
				return EC_ERROR_PARAM4;
			pd_send_vdm(port, USB_VID_GOOGLE, VDO_CMD_PING_ENABLE,
				    &enable, 1);
		} else if (!strncasecmp(argv[3], "curr", 4)) {
			pd_send_vdm(port, USB_VID_GOOGLE, VDO_CMD_CURRENT, NULL,
				    0);
		} else if (!strncasecmp(argv[3], "vers", 4)) {
			pd_send_vdm(port, USB_VID_GOOGLE, VDO_CMD_VERSION, NULL,
				    0);
		} else {
			return EC_ERROR_PARAM_COUNT;
		}
#if defined(CONFIG_CMD_PD) && defined(CONFIG_CMD_PD_FLASH)
	} else if (!strncasecmp(argv[2], "flash", 4)) {
		return remote_flashing(argc, argv);
#endif
#if defined(CONFIG_CMD_PD) && defined(CONFIG_USB_PD_DUAL_ROLE)
	} else if (!strcasecmp(argv[2], "dualrole")) {
		if (argc < 4) {
			ccprintf("dual-role toggling: ");
			switch (drp_state[port]) {
			case PD_DRP_TOGGLE_ON:
				ccprintf("on\n");
				break;
			case PD_DRP_TOGGLE_OFF:
				ccprintf("off\n");
				break;
			case PD_DRP_FREEZE:
				ccprintf("freeze\n");
				break;
			case PD_DRP_FORCE_SINK:
				ccprintf("force sink\n");
				break;
			case PD_DRP_FORCE_SOURCE:
				ccprintf("force source\n");
				break;
			}
		} else {
			if (!strcasecmp(argv[3], "on"))
				pd_set_dual_role(port, PD_DRP_TOGGLE_ON);
			else if (!strcasecmp(argv[3], "off"))
				pd_set_dual_role(port, PD_DRP_TOGGLE_OFF);
			else if (!strcasecmp(argv[3], "freeze"))
				pd_set_dual_role(port, PD_DRP_FREEZE);
			else if (!strcasecmp(argv[3], "sink"))
				pd_set_dual_role(port, PD_DRP_FORCE_SINK);
			else if (!strcasecmp(argv[3], "source"))
				pd_set_dual_role(port, PD_DRP_FORCE_SOURCE);
			else
				return EC_ERROR_PARAM4;
		}
		return EC_SUCCESS;
#endif
	} else
#endif
		if (!strncasecmp(argv[2], "state", 5)) {
		ccprintf("Port C%d CC%d, %s - Role: %s-%s%s "
			 "State: %d(%s), Flags: 0x%04x\n",
			 port, pd[port].polarity + 1,
			 pd_comm_is_enabled(port) ? "Ena" : "Dis",
			 pd[port].power_role == PD_ROLE_SOURCE ? "SRC" : "SNK",
			 pd[port].data_role == PD_ROLE_DFP ? "DFP" : "UFP",
			 (pd[port].flags & PD_FLAGS_VCONN_ON) ? "-VC" : "",
			 pd[port].task_state,
			 debug_level > 0 ? pd_get_task_state_name(port) : "",
			 pd[port].flags);
	} else {
		return EC_ERROR_PARAM1;
	}

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(pd, command_pd,
			"version"
			"|dump"
#ifdef CONFIG_USB_PD_TRY_SRC
			"|trysrc"
#endif
			" [0|1|2]"
#ifdef CONFIG_CMD_PD_DEV_DUMP_INFO
			"|rwhashtable"
#endif
			"\n\t<port> state"
#ifdef CONFIG_USB_PD_DUAL_ROLE
			"|tx|bist_rx|bist_tx|charger|dev"
			"\n\t<port> disable|enable|soft|info|hard|ping"
			"\n\t<port> dualrole [on|off|freeze|sink|source]"
			"\n\t<port> swap [power|data|vconn]"
			"\n\t<port> vdm [ping|curr|vers]"
#ifdef CONFIG_CMD_PD_FLASH
			"\n\t<port> flash [erase|reboot|signature|info|version]"
#endif /* CONFIG_CMD_PD_FLASH */
#endif /* CONFIG_USB_PD_DUAL_ROLE */
			"\n\t<port> srccaps",
			"USB PD");

#ifdef HAS_TASK_HOSTCMD

#ifdef CONFIG_HOSTCMD_FLASHPD
static enum ec_status hc_remote_flash(struct host_cmd_handler_args *args)
{
	const struct ec_params_usb_pd_fw_update *p = args->params;
	int port = p->port;
	const uint32_t *data = &(p->size) + 1;
	int i, size, rv = EC_RES_SUCCESS;
	timestamp_t timeout;

	if (port >= board_get_usb_pd_port_count())
		return EC_RES_INVALID_PARAM;

	if (p->size + sizeof(*p) > args->params_size)
		return EC_RES_INVALID_PARAM;

#if defined(CONFIG_BATTERY) && (defined(CONFIG_BATTERY_PRESENT_CUSTOM) || \
				defined(CONFIG_BATTERY_PRESENT_GPIO))
	/*
	 * Do not allow PD firmware update if no battery and this port
	 * is sinking power, because we will lose power.
	 */
	if (battery_is_present() != BP_YES &&
	    charge_manager_get_active_charge_port() == port)
		return EC_RES_UNAVAILABLE;
#endif

	/*
	 * Busy still with a VDM that host likely generated.  1 deep VDM queue
	 * so just return for retry logic on host side to deal with.
	 */
	if (pd[port].vdm_state > 0)
		return EC_RES_BUSY;

	switch (p->cmd) {
	case USB_PD_FW_REBOOT:
		pd_send_vdm(port, USB_VID_GOOGLE, VDO_CMD_REBOOT, NULL, 0);

		/*
		 * Return immediately to free pending i2c bus.	Host needs to
		 * manage this delay.
		 */
		return EC_RES_SUCCESS;

	case USB_PD_FW_FLASH_ERASE:
		pd_send_vdm(port, USB_VID_GOOGLE, VDO_CMD_FLASH_ERASE, NULL, 0);

		/*
		 * Return immediately.	Host needs to manage delays here which
		 * can be as long as 1.2 seconds on 64KB RW flash.
		 */
		return EC_RES_SUCCESS;

	case USB_PD_FW_ERASE_SIG:
		pd_send_vdm(port, USB_VID_GOOGLE, VDO_CMD_ERASE_SIG, NULL, 0);
		timeout.val = get_time().val + 500 * MSEC;
		break;

	case USB_PD_FW_FLASH_WRITE:
		/* Data size must be a multiple of 4 */
		if (!p->size || p->size % 4)
			return EC_RES_INVALID_PARAM;

		size = p->size / 4;
		for (i = 0; i < size; i += VDO_MAX_SIZE - 1) {
			pd_send_vdm(port, USB_VID_GOOGLE, VDO_CMD_FLASH_WRITE,
				    data + i, MIN(size - i, VDO_MAX_SIZE - 1));
			timeout.val = get_time().val + 500 * MSEC;

			/* Wait until VDM is done */
			while ((pd[port].vdm_state > 0) &&
			       (get_time().val < timeout.val))
				task_wait_event(10 * MSEC);

			if (pd[port].vdm_state > 0)
				return EC_RES_TIMEOUT;
		}
		return EC_RES_SUCCESS;

	default:
		return EC_RES_INVALID_PARAM;
		break;
	}

	/* Wait until VDM is done or timeout */
	while ((pd[port].vdm_state > 0) && (get_time().val < timeout.val))
		task_wait_event(50 * MSEC);

	if ((pd[port].vdm_state > 0) ||
	    (pd[port].vdm_state == VDM_STATE_ERR_TMOUT))
		rv = EC_RES_TIMEOUT;
	else if (pd[port].vdm_state < 0)
		rv = EC_RES_ERROR;

	return rv;
}
DECLARE_HOST_COMMAND(EC_CMD_USB_PD_FW_UPDATE, hc_remote_flash, EC_VER_MASK(0));
#endif /* CONFIG_HOSTCMD_FLASHPD */

#endif /* HAS_TASK_HOSTCMD */
#endif /* CONFIG_COMMON_RUNTIME */

/* LCOV_EXCL_STOP */
