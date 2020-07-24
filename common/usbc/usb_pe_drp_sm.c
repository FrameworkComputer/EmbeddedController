/* Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "atomic.h"
#include "battery.h"
#include "battery_smart.h"
#include "charge_manager.h"
#include "charge_state.h"
#include "common.h"
#include "console.h"
#include "hooks.h"
#include "host_command.h"
#include "stdbool.h"
#include "task.h"
#include "tcpm.h"
#include "util.h"
#include "usb_common.h"
#include "usb_dp_alt_mode.h"
#include "usb_mode.h"
#include "usb_pd_dpm.h"
#include "usb_pd.h"
#include "usb_pd_tcpm.h"
#include "usb_pe_sm.h"
#include "usb_tbt_alt_mode.h"
#include "usb_prl_sm.h"
#include "usb_tc_sm.h"
#include "usb_emsg.h"
#include "usb_sm.h"
#include "usbc_ppc.h"

/*
 * USB Policy Engine Sink / Source module
 *
 * Based on Revision 3.0, Version 1.2 of
 * the USB Power Delivery Specification.
 */

#ifdef CONFIG_COMMON_RUNTIME
#define CPRINTF(format, args...) cprintf(CC_USBPD, format, ## args)
#define CPRINTS(format, args...) cprints(CC_USBPD, format, ## args)
#else
#define CPRINTF(format, args...)
#define CPRINTS(format, args...)
#endif

#define CPRINTF_LX(x, format, args...) \
	do { \
		if (pe_debug_level >= x) \
			CPRINTF(format, ## args); \
	} while (0)
#define CPRINTF_L1(format, args...) CPRINTF_LX(1, format, ## args)
#define CPRINTF_L2(format, args...) CPRINTF_LX(2, format, ## args)
#define CPRINTF_L3(format, args...) CPRINTF_LX(3, format, ## args)

#define CPRINTS_LX(x, format, args...) \
	do { \
		if (pe_debug_level >= x) \
			CPRINTS(format, ## args); \
	} while (0)
#define CPRINTS_L1(format, args...) CPRINTS_LX(1, format, ## args)
#define CPRINTS_L2(format, args...) CPRINTS_LX(2, format, ## args)
#define CPRINTS_L3(format, args...) CPRINTS_LX(3, format, ## args)


#define PE_SET_FLAG(port, flag) atomic_or(&pe[port].flags, (flag))
#define PE_CLR_FLAG(port, flag) atomic_clear(&pe[port].flags, (flag))
#define PE_CHK_FLAG(port, flag) (pe[port].flags & (flag))

/*
 * These macros SET, CLEAR, and CHECK, a DPM (Device Policy Manager)
 * Request. The Requests are listed in usb_pe_sm.h.
 */
#define PE_SET_DPM_REQUEST(port, req) atomic_or(&pe[port].dpm_request,    (req))
#define PE_CLR_DPM_REQUEST(port, req) atomic_clear(&pe[port].dpm_request, (req))
#define PE_CHK_DPM_REQUEST(port, req) (pe[port].dpm_request &             (req))

/*
 * Policy Engine Layer Flags
 * These are reproduced in test/usb_pe.h. If they change here, they must change
 * there.
 */

/* At least one successful PD communication packet received from port partner */
#define PE_FLAGS_PD_CONNECTION               BIT(0)
/* Accept message received from port partner */
#define PE_FLAGS_ACCEPT                      BIT(1)
/* Power Supply Ready message received from port partner */
#define PE_FLAGS_PS_READY                    BIT(2)
/* Protocol Error was determined based on error recovery current state */
#define PE_FLAGS_PROTOCOL_ERROR              BIT(3)
/* Set if we are in Modal Operation */
#define PE_FLAGS_MODAL_OPERATION             BIT(4)
/* A message we requested to be sent has been transmitted */
#define PE_FLAGS_TX_COMPLETE                 BIT(5)
/* A message sent by a port partner has been received */
#define PE_FLAGS_MSG_RECEIVED                BIT(6)
/* A hard reset has been requested but has not been sent, not currently used */
#define PE_FLAGS_HARD_RESET_PENDING          BIT(7)
/* Port partner sent a Wait message. Wait before we resend our message */
#define PE_FLAGS_WAIT                        BIT(8)
/* An explicit contract is in place with our port partner */
#define PE_FLAGS_EXPLICIT_CONTRACT           BIT(9)
/* Waiting for Sink Capabailities timed out.  Used for retry error handling */
#define PE_FLAGS_SNK_WAIT_CAP_TIMEOUT        BIT(10)
/* Power Supply voltage/current transition timed out */
#define PE_FLAGS_PS_TRANSITION_TIMEOUT       BIT(11)
/* Flag to note current Atomic Message Sequence is interruptible */
#define PE_FLAGS_INTERRUPTIBLE_AMS           BIT(12)
/* Flag to note Power Supply reset has completed */
#define PE_FLAGS_PS_RESET_COMPLETE           BIT(13)
/* VCONN swap operation has completed */
#define PE_FLAGS_VCONN_SWAP_COMPLETE         BIT(14)
/* Flag to note no more setup VDMs (discovery, etc.) should be sent */
#define PE_FLAGS_VDM_SETUP_DONE              BIT(15)
/* Flag to note PR Swap just completed for Startup entry */
#define PE_FLAGS_PR_SWAP_COMPLETE	     BIT(16)
/* Flag to note Port Discovery port partner replied with BUSY */
#define PE_FLAGS_VDM_REQUEST_BUSY            BIT(17)
/* Flag to note Port Discovery port partner replied with NAK */
#define PE_FLAGS_VDM_REQUEST_NAKED           BIT(18)
/* Flag to note FRS/PRS context in shared state machine path */
#define PE_FLAGS_FAST_ROLE_SWAP_PATH         BIT(19)
/* Flag to note if FRS listening is enabled */
#define PE_FLAGS_FAST_ROLE_SWAP_ENABLED      BIT(20)
/* Flag to note TCPC passed on FRS signal from port partner */
#define PE_FLAGS_FAST_ROLE_SWAP_SIGNALED     BIT(21)
/* TODO: POLICY decision: Triggers a DR SWAP attempt from UFP to DFP */
#define PE_FLAGS_DR_SWAP_TO_DFP              BIT(22)
/* Flag to trigger a message resend after receiving a WAIT from port partner */
#define PE_FLAGS_WAITING_DR_SWAP             BIT(23)
/* FLAG to track if port partner is dualrole capable */
#define PE_FLAGS_PORT_PARTNER_IS_DUALROLE    BIT(24)
/* FLAG is set when an AMS is initiated locally. ie. AP requested a PR_SWAP */
#define PE_FLAGS_LOCALLY_INITIATED_AMS       BIT(25)
/* Flag to note the first message sent in PE_SRC_READY and PE_SNK_READY */
#define PE_FLAGS_FIRST_MSG                   BIT(26)
/* Flag to continue a VDM request if it was interrupted */
#define PE_FLAGS_VDM_REQUEST_CONTINUE        BIT(27)
/* TODO: POLICY decision: Triggers a Vconn SWAP attempt to on */
#define PE_FLAGS_VCONN_SWAP_TO_ON	     BIT(28)
/* FLAG to track that VDM request to port partner timed out */
#define PE_FLAGS_VDM_REQUEST_TIMEOUT	     BIT(29)
/* FLAG to note message was discarded due to incoming message */
#define PE_FLAGS_MSG_DISCARDED		     BIT(30)

/* Message flags which should not persist on returning to ready state */
#define PE_FLAGS_READY_CLR		     (PE_FLAGS_LOCALLY_INITIATED_AMS \
					     | PE_FLAGS_MSG_DISCARDED \
					     | PE_FLAGS_VDM_REQUEST_TIMEOUT)

/* 6.7.3 Hard Reset Counter */
#define N_HARD_RESET_COUNT 2

/* 6.7.4 Capabilities Counter */
#define N_CAPS_COUNT 25

/* 6.7.5 Discover Identity Counter */
/*
 * NOTE: The Protocol Layer tries to send a message 3 time before giving up,
 * so a Discover Identity SOP' message will be sent 3*6 = 18 times (slightly
 * less than spec maximum of 20).  This counter applies only to cable plug
 * discovery.
 */
#define N_DISCOVER_IDENTITY_COUNT 6
/*
 * ChromeOS policy:
 *   For PD2.0, We must be DFP before sending Discover Identity message
 *   to the port partner. Attempt to DR SWAP from UFP to DFP
 *   N_DR_SWAP_ATTEMPT_COUNT times before giving up on sending a
 *   Discover Identity message.
 */
#define N_DR_SWAP_ATTEMPT_COUNT 5

#define TIMER_DISABLED 0xffffffffffffffff /* Unreachable time in future */

/*
 * The time that we allow the port partner to send any messages after an
 * explicit contract is established.  400ms was chosen somewhat arbitrarily as
 * it should be long enough for sources to decide to send a message if they were
 * going to, but not so long that a "low power charger connected" notification
 * would be shown in the chrome OS UI.
 */
#define SRC_SNK_READY_HOLD_OFF_US (400 * MSEC)

/*
 * Function pointer to a Structured Vendor Defined Message (SVDM) response
 * function defined in the board's usb_pd_policy.c file.
 */
typedef int (*svdm_rsp_func)(int port, uint32_t *payload);

/* List of all Policy Engine level states */
enum usb_pe_state {
	/* Normal States */
	PE_SRC_STARTUP,
	PE_SRC_DISCOVERY,
	PE_SRC_SEND_CAPABILITIES,
	PE_SRC_NEGOTIATE_CAPABILITY,
	PE_SRC_TRANSITION_SUPPLY,
	PE_SRC_READY,
	PE_SRC_DISABLED,
	PE_SRC_CAPABILITY_RESPONSE,
	PE_SRC_HARD_RESET,
	PE_SRC_HARD_RESET_RECEIVED,
	PE_SRC_TRANSITION_TO_DEFAULT,
	PE_SNK_STARTUP,
	PE_SNK_DISCOVERY,
	PE_SNK_WAIT_FOR_CAPABILITIES,
	PE_SNK_EVALUATE_CAPABILITY,
	PE_SNK_SELECT_CAPABILITY,
	PE_SNK_READY,
	PE_SNK_HARD_RESET,
	PE_SNK_TRANSITION_TO_DEFAULT,
	PE_SNK_GIVE_SINK_CAP,
	PE_SNK_GET_SOURCE_CAP,
	PE_SNK_TRANSITION_SINK,
	PE_SEND_SOFT_RESET,
	PE_SOFT_RESET,
	PE_SEND_NOT_SUPPORTED,
	PE_SRC_PING,
	PE_DRS_EVALUATE_SWAP,
	PE_DRS_CHANGE,
	PE_DRS_SEND_SWAP,
	PE_PRS_SRC_SNK_EVALUATE_SWAP,
	PE_PRS_SRC_SNK_TRANSITION_TO_OFF,
	PE_PRS_SRC_SNK_ASSERT_RD,
	PE_PRS_SRC_SNK_WAIT_SOURCE_ON,
	PE_PRS_SRC_SNK_SEND_SWAP,
	PE_PRS_SNK_SRC_EVALUATE_SWAP,
	PE_PRS_SNK_SRC_TRANSITION_TO_OFF,
	PE_PRS_SNK_SRC_ASSERT_RP,
	PE_PRS_SNK_SRC_SOURCE_ON,
	PE_PRS_SNK_SRC_SEND_SWAP,
	PE_VCS_EVALUATE_SWAP,
	PE_VCS_SEND_SWAP,
	PE_VCS_WAIT_FOR_VCONN_SWAP,
	PE_VCS_TURN_ON_VCONN_SWAP,
	PE_VCS_TURN_OFF_VCONN_SWAP,
	PE_VCS_SEND_PS_RDY_SWAP,
	PE_VDM_SEND_REQUEST,
	PE_VDM_IDENTITY_REQUEST_CBL,
	PE_INIT_PORT_VDM_IDENTITY_REQUEST,
	PE_INIT_VDM_SVIDS_REQUEST,
	PE_INIT_VDM_MODES_REQUEST,
	PE_VDM_REQUEST_DPM,
	PE_VDM_RESPONSE,
	PE_HANDLE_CUSTOM_VDM_REQUEST,
	PE_WAIT_FOR_ERROR_RECOVERY,
	PE_BIST_TX,
	PE_BIST_RX,
	PE_DEU_SEND_ENTER_USB,
	PE_DR_SNK_GET_SINK_CAP,

	/* AMS Start parent - runs SenderResponseTimer */
	PE_SENDER_RESPONSE,

	/* PD3.0 only states below here*/
	PE_FRS_SNK_SRC_START_AMS,
	PE_GIVE_BATTERY_CAP,
	PE_GIVE_BATTERY_STATUS,
	PE_SEND_ALERT,

#ifdef CONFIG_USB_PD_REV30
	/* Super States */
	PE_PRS_FRS_SHARED,
#endif /* CONFIG_USB_PD_REV30 */
};

/*
 * The result of a previously sent DPM request; used by PE_VDM_SEND_REQUEST to
 * indicate to child states when they need to handle a response.
 */
enum vdm_response_result {
	/* The parent state is still waiting for a response. */
	VDM_RESULT_WAITING,
	/*
	 * The parent state parsed a message, but there is nothing for the child
	 * to handle, e.g. BUSY.
	 */
	VDM_RESULT_NO_ACTION,
	/* The parent state processed an ACK response. */
	VDM_RESULT_ACK,
	/*
	 * The parent state processed a NAK-like response (NAK, Not Supported,
	 * or response timeout.
	 */
	VDM_RESULT_NAK,
};

/* Forward declare the full list of states. This is indexed by usb_pe_state */
static const struct usb_state pe_states[];

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

#ifdef USB_PD_DEBUG_LABELS
/* List of human readable state names for console debugging */
static const char * const pe_state_names[] = {
	[PE_SRC_STARTUP] = "PE_SRC_Startup",
	[PE_SRC_DISCOVERY] = "PE_SRC_Discovery",
	[PE_SRC_SEND_CAPABILITIES] = "PE_SRC_Send_Capabilities",
	[PE_SRC_NEGOTIATE_CAPABILITY] = "PE_SRC_Negotiate_Capability",
	[PE_SRC_TRANSITION_SUPPLY] = "PE_SRC_Transition_Supply",
	[PE_SRC_READY] = "PE_SRC_Ready",
	[PE_SRC_DISABLED] = "PE_SRC_Disabled",
	[PE_SRC_CAPABILITY_RESPONSE] = "PE_SRC_Capability_Response",
	[PE_SRC_HARD_RESET] = "PE_SRC_Hard_Reset",
	[PE_SRC_HARD_RESET_RECEIVED] = "PE_SRC_Hard_Reset_Received",
	[PE_SRC_TRANSITION_TO_DEFAULT] = "PE_SRC_Transition_to_default",
	[PE_SNK_STARTUP] = "PE_SNK_Startup",
	[PE_SNK_DISCOVERY] = "PE_SNK_Discovery",
	[PE_SNK_WAIT_FOR_CAPABILITIES] = "PE_SNK_Wait_for_Capabilities",
	[PE_SNK_EVALUATE_CAPABILITY] = "PE_SNK_Evaluate_Capability",
	[PE_SNK_SELECT_CAPABILITY] = "PE_SNK_Select_Capability",
	[PE_SNK_READY] = "PE_SNK_Ready",
	[PE_SNK_HARD_RESET] = "PE_SNK_Hard_Reset",
	[PE_SNK_TRANSITION_TO_DEFAULT] = "PE_SNK_Transition_to_default",
	[PE_SNK_GIVE_SINK_CAP] = "PE_SNK_Give_Sink_Cap",
	[PE_SNK_GET_SOURCE_CAP] = "PE_SNK_Get_Source_Cap",
	[PE_SNK_TRANSITION_SINK] = "PE_SNK_Transition_Sink",
	[PE_SEND_SOFT_RESET] = "PE_Send_Soft_Reset",
	[PE_SOFT_RESET] = "PE_Soft_Reset",
	[PE_SEND_NOT_SUPPORTED] = "PE_Send_Not_Supported",
	[PE_SRC_PING] = "PE_SRC_Ping",
	[PE_DRS_EVALUATE_SWAP] = "PE_DRS_Evaluate_Swap",
	[PE_DRS_CHANGE] = "PE_DRS_Change",
	[PE_DRS_SEND_SWAP] = "PE_DRS_Send_Swap",
	[PE_PRS_SRC_SNK_EVALUATE_SWAP] = "PE_PRS_SRC_SNK_Evaluate_Swap",
	[PE_PRS_SRC_SNK_TRANSITION_TO_OFF] = "PE_PRS_SRC_SNK_Transition_To_Off",
	[PE_PRS_SRC_SNK_ASSERT_RD] = "PE_PRS_SRC_SNK_Assert_Rd",
	[PE_PRS_SRC_SNK_WAIT_SOURCE_ON] = "PE_PRS_SRC_SNK_Wait_Source_On",
	[PE_PRS_SRC_SNK_SEND_SWAP] = "PE_PRS_SRC_SNK_Send_Swap",
	[PE_PRS_SNK_SRC_EVALUATE_SWAP] = "PE_PRS_SNK_SRC_Evaluate_Swap",
	[PE_PRS_SNK_SRC_TRANSITION_TO_OFF] = "PE_PRS_SNK_SRC_Transition_To_Off",
	[PE_PRS_SNK_SRC_ASSERT_RP] = "PE_PRS_SNK_SRC_Assert_Rp",
	[PE_PRS_SNK_SRC_SOURCE_ON] = "PE_PRS_SNK_SRC_Source_On",
	[PE_PRS_SNK_SRC_SEND_SWAP] = "PE_PRS_SNK_SRC_Send_Swap",
	[PE_VCS_EVALUATE_SWAP] = "PE_VCS_Evaluate_Swap",
	[PE_VCS_SEND_SWAP] = "PE_VCS_Send_Swap",
	[PE_VCS_WAIT_FOR_VCONN_SWAP] = "PE_VCS_Wait_For_Vconn_Swap",
	[PE_VCS_TURN_ON_VCONN_SWAP] = "PE_VCS_Turn_On_Vconn_Swap",
	[PE_VCS_TURN_OFF_VCONN_SWAP] = "PE_VCS_Turn_Off_Vconn_Swap",
	[PE_VCS_SEND_PS_RDY_SWAP] = "PE_VCS_Send_Ps_Rdy_Swap",
	[PE_VDM_SEND_REQUEST] = "PE_VDM_Send_Request",
	[PE_VDM_IDENTITY_REQUEST_CBL] = "PE_VDM_Identity_Request_Cbl",
	[PE_INIT_PORT_VDM_IDENTITY_REQUEST] =
					   "PE_INIT_PORT_VDM_Identity_Request",
	[PE_INIT_VDM_SVIDS_REQUEST] = "PE_INIT_VDM_SVIDs_Request",
	[PE_INIT_VDM_MODES_REQUEST] = "PE_INIT_VDM_Modes_Request",
	[PE_VDM_REQUEST_DPM] = "PE_VDM_Request_DPM",
	[PE_VDM_RESPONSE] = "PE_VDM_Response",
	[PE_HANDLE_CUSTOM_VDM_REQUEST] = "PE_Handle_Custom_Vdm_Request",
	[PE_WAIT_FOR_ERROR_RECOVERY] = "PE_Wait_For_Error_Recovery",
	[PE_BIST_TX] = "PE_Bist_TX",
	[PE_BIST_RX] = "PE_Bist_RX",
	[PE_DEU_SEND_ENTER_USB]  = "PE_DEU_Send_Enter_USB",
	[PE_DR_SNK_GET_SINK_CAP] = "PE_DR_SNK_Get_Sink_Cap",

	[PE_SENDER_RESPONSE] = "PE_SENDER_RESPONSE",

	/* PD3.0 only states below here*/
#ifdef CONFIG_USB_PD_REV30
	[PE_FRS_SNK_SRC_START_AMS] = "PE_FRS_SNK_SRC_Start_Ams",
	[PE_GIVE_BATTERY_CAP] = "PE_Give_Battery_Cap",
	[PE_GIVE_BATTERY_STATUS] = "PE_Give_Battery_Status",
	[PE_SEND_ALERT] = "PE_Send_Alert",

	/* Super States */
	[PE_PRS_FRS_SHARED] = "SS:PE_PRS_FRS_SHARED",
#endif /* CONFIG_USB_PD_REV30 */
};
#else
/*
 * Reference so IS_ENABLED section below that references the names
 * will compile and the optimizer will remove it.
 */
extern const char **pe_state_names;
#endif

#ifndef CONFIG_USBC_VCONN
enum usb_pe_state PE_VCS_EVALUATE_SWAP_NOT_SUPPORTED;
enum usb_pe_state PE_VCS_SEND_SWAP_NOT_SUPPORTED;
enum usb_pe_state PE_VCS_WAIT_FOR_VCONN_SWAP_NOT_SUPPORTED;
enum usb_pe_state PE_VCS_TURN_ON_VCONN_SWAP_NOT_SUPPORTED;
enum usb_pe_state PE_VCS_TURN_OFF_VCONN_SWAP_NOT_SUPPORTED;
enum usb_pe_state PE_VCS_SEND_PS_RDY_SWAP_NOT_SUPPORTED;
#define PE_VCS_EVALUATE_SWAP PE_VCS_EVALUATE_SWAP_NOT_SUPPORTED
#define PE_VCS_SEND_SWAP PE_VCS_SEND_SWAP_NOT_SUPPORTED
#define PE_VCS_WAIT_FOR_VCONN_SWAP PE_VCS_WAIT_FOR_VCONN_SWAP_NOT_SUPPORTED
#define PE_VCS_TURN_ON_VCONN_SWAP PE_VCS_TURN_ON_VCONN_SWAP_NOT_SUPPORTED
#define PE_VCS_TURN_OFF_VCONN_SWAP PE_VCS_TURN_OFF_VCONN_SWAP_NOT_SUPPORTED
#define PE_VCS_SEND_PS_RDY_SWAP PE_VCS_SEND_PS_RDY_SWAP_NOT_SUPPORTED
#endif /* CONFIG_USBC_VCONN */

/*
 * Ensure that Invalid states don't link properly. This let's us use guard
 * code with IS_ENABLED instead of ifdefs and still save flash space
 */
#ifndef CONFIG_USB_PD_REV30
extern enum usb_pe_state PE_FRS_SNK_SRC_START_AMS_NOT_SUPPORTED;
extern enum usb_pe_state PE_GIVE_BATTERY_CAP_NOT_SUPPORTED;
extern enum usb_pe_state PE_GIVE_BATTERY_STATUS_NOT_SUPPORTED;
extern enum usb_pe_state PE_PRS_FRS_SHARE_NOT_SUPPORTED;
#define PE_FRS_SNK_SRC_START_AMS PE_FRS_SNK_SRC_START_AMS_NOT_SUPPORTED
#define PE_GIVE_BATTERY_CAP PE_GIVE_BATTERY_CAP_NOT_SUPPORTED
#define PE_GIVE_BATTERY_STATUS PE_GIVE_BATTERY_STATUS_NOT_SUPPORTED
#define PE_PRS_FRS_SHARED PE_PRS_FRS_SHARED_NOT_SUPPORTED
void pe_set_frs_enable(int port, int enable);
#endif /* CONFIG_USB_PD_REV30 */

/*
 * This enum is used to implement a state machine consisting of at most
 * 3 states, inside a Policy Engine State.
 */
enum sub_state {
	PE_SUB0,
	PE_SUB1,
	PE_SUB2
};

static enum sm_local_state local_state[CONFIG_USB_PD_PORT_MAX_COUNT];

/* Debug log level - higher number == more log */
#ifdef CONFIG_USB_PD_DEBUG_LEVEL
static const enum debug_level pe_debug_level = CONFIG_USB_PD_DEBUG_LEVEL;
#else
static enum debug_level pe_debug_level = DEBUG_LEVEL_1;
#endif

/*
 * Policy Engine State Machine Object
 */
static struct policy_engine {
	/* state machine context */
	struct sm_ctx ctx;
	/* current port power role (SOURCE or SINK) */
	enum pd_power_role power_role;
	/* current port data role (DFP or UFP) */
	enum pd_data_role data_role;
	/* state machine flags */
	uint32_t flags;
	/* Device Policy Manager Request */
	uint32_t dpm_request;
	/* state timeout timer */
	uint64_t timeout;
	/* last requested voltage PDO index */
	int requested_idx;

	/* port address where soft resets are sent */
	enum tcpm_transmit_type soft_reset_sop;

	/* Current limit / voltage based on the last request message */
	uint32_t curr_limit;
	uint32_t supply_voltage;

	/* state specific state machine variable */
	enum sub_state sub;

	/* PD_VDO_INVALID is used when there is an invalid VDO */
	int32_t ama_vdo;
	int32_t vpd_vdo;
	/* Alternate mode discovery results */
	struct pd_discovery discovery[DISCOVERY_TYPE_COUNT];
	/* Active alternate modes */
	struct partner_active_modes partner_amodes[AMODE_TYPE_COUNT];

	/* Partner type to send */
	enum tcpm_transmit_type tx_type;

	/* VDM - used to send information to shared VDM Request state */
	uint32_t vdm_cnt;
	uint32_t vdm_data[VDO_HDR_SIZE + VDO_MAX_SIZE];
	uint8_t vdm_ack_min_data_objects;

	/* Timers */

	/*
	 * The NoResponseTimer is used by the Policy Engine in a Source
	 * to determine that its Port Partner is not responding after a
	 * Hard Reset.
	 */
	uint64_t no_response_timer;

	/*
	 * Prior to a successful negotiation, a Source Shall use the
	 * SourceCapabilityTimer to periodically send out a
	 * Source_Capabilities Message.
	 */
	uint64_t source_cap_timer;

	/*
	 * This timer is started when a request for a new Capability has been
	 * accepted and will timeout after PD_T_PS_TRANSITION if a PS_RDY
	 * Message has not been received.
	 */
	uint64_t ps_transition_timer;

	/*
	 * This timer is used to ensure that a Message requesting a response
	 * (e.g. Get_Source_Cap Message) is responded to within a bounded time
	 * of PD_T_SENDER_RESPONSE.
	 */
	uint64_t sender_response_timer;

	/*
	 * This timer is used during an Explicit Contract when discovering
	 * whether a Port Partner is PD Capable using SOP'.
	 */
	uint64_t discover_identity_timer;

	/*
	 * This timer is used in a Source to ensure that the Sink has had
	 * sufficient time to process Hard Reset Signaling before turning
	 * off its power supply to VBUS.
	 */
	uint64_t ps_hard_reset_timer;

	/*
	 * This timer is used to ensure that the time before the next Sink
	 * Request Message, after a Wait Message has been received from the
	 * Source in response to a Sink Request Message.
	 */
	uint64_t sink_request_timer;

	/*
	 * This timer combines the PSSourceOffTimer and PSSourceOnTimer timers.
	 * For PSSourceOffTimer, when this DRP device is currently acting as a
	 * Sink, this timer times out on a PS_RDY Message during a Power Role
	 * Swap sequence.
	 *
	 * For PSSourceOnTimer, when this DRP device is currently acting as a
	 * Source that has just stopped sourcing power and is waiting to start
	 * sinking power to timeout on a PS_RDY Message during a Power Role
	 * Swap.
	 */
	uint64_t ps_source_timer;

	/*
	 * In BIST_TX mode, this timer is used by a UUT to ensure that a
	 * Continuous BIST Mode (i.e. BIST Carrier Mode) is exited in a timely
	 * fashion.
	 *
	 * In BIST_RX mode, this timer is used to give the port partner time
	 * to respond.
	 */
	uint64_t bist_cont_mode_timer;
	/*
	 * This timer is used by the new Source, after a Power Role Swap or
	 * Fast Role Swap, to ensure that it does not send Source_Capabilities
	 * Message before the new Sink is ready to receive the
	 * Source_Capabilities Message.
	 */
	uint64_t swap_source_start_timer;

	/*
	 * This timer is used by the Initiator’s Policy Engine to ensure that
	 * a Structured VDM Command request needing a response (e.g. Discover
	 * Identity Command request) is responded to within a bounded time of
	 * tVDMSenderResponse.
	 */
	uint64_t vdm_response_timer;

	/*
	 * This timer is used during a VCONN Swap.
	 */
	uint64_t vconn_on_timer;

	/*
	 * For PD2.0, this timer is used to wait 400ms and add some
	 * jitter of up to 100ms before sending a message.
	 * NOTE: This timer is not part of the TypeC/PD spec.
	 */
	uint64_t wait_and_add_jitter_timer;

	/* Counters */

	/*
	 * This counter is used to retry the Hard Reset whenever there is no
	 * response from the remote device.
	 */
	uint32_t hard_reset_counter;

	/*
	 * This counter is used to count the number of Source_Capabilities
	 * Messages which have been sent by a Source at power up or after a
	 * Hard Reset.
	 */
	uint32_t caps_counter;

	/*
	 * This counter maintains a count of Discover Identity Messages sent
	 * to a cable.  If no GoodCRC messages are received after
	 * nDiscoverIdentityCount, the port shall not send any further
	 * SOP'/SOP'' messages.
	 */
	uint32_t discover_identity_counter;
	/*
	 * For PD2.0, we need to be a DFP before sending a discovery identity
	 * message to our port partner. This counter keeps track of how
	 * many attempts to DR SWAP from UFP to DFP.
	 */
	uint32_t dr_swap_attempt_counter;

	/* Last received source cap */
	uint32_t src_caps[PDO_MAX_OBJECTS];
	int src_cap_cnt;

	/* Attached ChromeOS device id, RW hash, and current RO / RW image */
	uint16_t dev_id;
	uint32_t dev_rw_hash[PD_RW_HASH_SIZE/4];
	enum ec_image current_image;
} pe[CONFIG_USB_PD_PORT_MAX_COUNT];

test_export_static enum usb_pe_state get_state_pe(const int port);
test_export_static void set_state_pe(const int port,
				     const enum usb_pe_state new_state);
/*
 * The spec. revision is used to index into this array.
 *  PD 1.0 (VDO 1.0) - return VDM_VER10
 *  PD 2.0 (VDO 1.0) - return VDM_VER10
 *  PD 3.0 (VDO 2.0) - return VDM_VER20
 */
static const uint8_t vdo_ver[] = {
	[PD_REV10] = VDM_VER10,
	[PD_REV20] = VDM_VER10,
	[PD_REV30] = VDM_VER20,
};

int pd_get_vdo_ver(int port, enum tcpm_transmit_type type)
{
	enum pd_rev_type rev = prl_get_rev(port, type);

	if (rev < PD_REV30)
		return vdo_ver[rev];
	else
		return VDM_VER20;
}

static void pe_set_ready_state(int port)
{
	if (pe[port].power_role == PD_ROLE_SOURCE)
		set_state_pe(port, PE_SRC_READY);
	else
		set_state_pe(port, PE_SNK_READY);
}

static inline void send_data_msg(int port, enum tcpm_transmit_type type,
				 enum pd_data_msg_type msg)
{
	/* Clear any previous TX status before sending a new message */
	PE_CLR_FLAG(port, PE_FLAGS_TX_COMPLETE);
	prl_send_data_msg(port, type, msg);
}

static __maybe_unused inline void send_ext_data_msg(
	int port, enum tcpm_transmit_type type, enum pd_ext_msg_type msg)
{
	/* Clear any previous TX status before sending a new message */
	PE_CLR_FLAG(port, PE_FLAGS_TX_COMPLETE);
	prl_send_ext_data_msg(port, type, msg);
}

static inline void send_ctrl_msg(int port, enum tcpm_transmit_type type,
				 enum pd_ctrl_msg_type msg)
{
	/* Clear any previous TX status before sending a new message */
	PE_CLR_FLAG(port, PE_FLAGS_TX_COMPLETE);
	prl_send_ctrl_msg(port, type, msg);
}

/* Compile-time insurance to ensure this code does not call into prl directly */
#define prl_send_data_msg DO_NOT_USE
#define prl_send_ext_data_msg DO_NOT_USE
#define prl_send_ctrl_msg DO_NOT_USE


static void pe_init(int port)
{
	pe[port].flags = 0;
	pe[port].dpm_request = 0;
	pe[port].source_cap_timer = TIMER_DISABLED;
	pe[port].no_response_timer = TIMER_DISABLED;
	pe[port].data_role = pd_get_data_role(port);
	pe[port].tx_type = TCPC_TX_INVALID;

	tc_pd_connection(port, 0);

	if (pd_get_power_role(port) == PD_ROLE_SOURCE)
		set_state_pe(port, PE_SRC_STARTUP);
	else
		set_state_pe(port, PE_SNK_STARTUP);
}

int pe_is_running(int port)
{
	return local_state[port] == SM_RUN;
}

bool pe_in_local_ams(int port)
{
	return !!PE_CHK_FLAG(port, PE_FLAGS_LOCALLY_INITIATED_AMS);
}

void pe_set_debug_level(enum debug_level debug_level)
{
#ifndef CONFIG_USB_PD_DEBUG_LEVEL
	pe_debug_level = debug_level;
#endif
}

void pe_run(int port, int evt, int en)
{
	switch (local_state[port]) {
	case SM_PAUSED:
		if (!en)
			break;
		/* fall through */
	case SM_INIT:
		pe_init(port);
		local_state[port] = SM_RUN;
		/* fall through */
	case SM_RUN:
		if (!en) {
			local_state[port] = SM_PAUSED;
			/*
			 * While we are paused, exit all states and wait until
			 * initialized again.
			 */
			set_state(port, &pe[port].ctx, NULL);
			break;
		}

		/*
		 * Check for Fast Role Swap signal
		 * This is not a typical pattern for adding state changes.
		 * I added this here because FRS SIGNALED can happen at any
		 * state once we are listening for the signal and we want to
		 * make sure to handle it immediately.
		 */
		if (IS_ENABLED(CONFIG_USB_PD_REV30) &&
		PE_CHK_FLAG(port, PE_FLAGS_FAST_ROLE_SWAP_SIGNALED)) {
			PE_CLR_FLAG(port, PE_FLAGS_FAST_ROLE_SWAP_SIGNALED);
			set_state_pe(port, PE_FRS_SNK_SRC_START_AMS);
		}

		/* Run state machine */
		run_state(port, &pe[port].ctx);
		break;
	}
}

int pe_is_explicit_contract(int port)
{
	return PE_CHK_FLAG(port, PE_FLAGS_EXPLICIT_CONTRACT);
}

void pe_message_received(int port)
{
	/* This should only be called from the PD task */
	assert(port == TASK_ID_TO_PD_PORT(task_get_current()));

	PE_SET_FLAG(port, PE_FLAGS_MSG_RECEIVED);
}

void pe_hard_reset_sent(int port)
{
	/* This should only be called from the PD task */
	assert(port == TASK_ID_TO_PD_PORT(task_get_current()));

	PE_CLR_FLAG(port, PE_FLAGS_HARD_RESET_PENDING);
}

void pe_got_hard_reset(int port)
{
	/* This should only be called from the PD task */
	assert(port == TASK_ID_TO_PD_PORT(task_get_current()));

	/*
	 * Transition from any state to the PE_SRC_Hard_Reset_Received or
	 *  PE_SNK_Transition_to_default state when:
	 *  1) Hard Reset Signaling is detected.
	 */
	pe[port].power_role = pd_get_power_role(port);

	if (pe[port].power_role == PD_ROLE_SOURCE)
		set_state_pe(port, PE_SRC_HARD_RESET_RECEIVED);
	else
		set_state_pe(port, PE_SNK_TRANSITION_TO_DEFAULT);
}

#ifdef CONFIG_USB_PD_REV30
/*
 * pd_got_frs_signal
 *
 * Called by the handler that detects the FRS signal in order to
 * switch PE states to complete the FRS that the hardware has
 * started.
 */
void pd_got_frs_signal(int port)
{
	PE_SET_FLAG(port, PE_FLAGS_FAST_ROLE_SWAP_SIGNALED);
	task_set_event(PD_PORT_TO_TASK_ID(port), TASK_EVENT_WAKE, 0);
}

/*
 * PE_Set_FRS_Enable
 *
 * This function should be called every time an explicit contract
 * is disabled, to disable FRS.
 *
 * Enabling an explicit contract is not enough to enable FRS, it
 * also requires a Sink Capability power requirement from a Source
 * that supports FRS so we can determine if this is something we
 * can handle.
 */
static void pe_set_frs_enable(int port, int enable)
{
	/* This should only be called from the PD task */
	assert(port == TASK_ID_TO_PD_PORT(task_get_current()));

	if (IS_ENABLED(CONFIG_USB_PD_FRS)) {
		int current = PE_CHK_FLAG(port,
					  PE_FLAGS_FAST_ROLE_SWAP_ENABLED);

		/* Request an FRS change, only if the state has changed */
		if (!!current != !!enable) {
			pd_set_frs_enable(port, enable);
			if (enable)
				PE_SET_FLAG(port,
					    PE_FLAGS_FAST_ROLE_SWAP_ENABLED);
			else
				PE_CLR_FLAG(port,
					    PE_FLAGS_FAST_ROLE_SWAP_ENABLED);
		}
	}
}
#endif /* CONFIG_USB_PD_REV30 */

void pe_set_explicit_contract(int port)
{
	PE_SET_FLAG(port, PE_FLAGS_EXPLICIT_CONTRACT);

	/* Set Rp for collision avoidance */
	if (IS_ENABLED(CONFIG_USB_PD_REV30))
		typec_update_cc(port);
}

void pe_invalidate_explicit_contract(int port)
{
	if (IS_ENABLED(CONFIG_USB_PD_REV30))
		pe_set_frs_enable(port, 0);

	PE_CLR_FLAG(port, PE_FLAGS_EXPLICIT_CONTRACT);

	/* Set Rp for current limit */
	if (IS_ENABLED(CONFIG_USB_PD_REV30))
		typec_update_cc(port);
}

/*
 * Determine if this port may communicate with the cable plug.
 *
 * In both PD 2.0 and 3.0 (2.5.4 SOP'/SOP'' Communication with Cable Plugs):
 *
 * When no Contract or an Implicit Contract is in place (e.g. after a Power Role
 * Swap or Fast Role Swap) only the Source port that is supplying Vconn is
 * allowed to send packets to a Cable Plug
 *
 * When in an explicit contract, PD 3.0 requires that a port be Vconn source to
 * communicate with the cable.  PD 2.0 requires that a port be DFP to
 * communicate with the cable plug, with an implication that it must be Vconn
 * source as well (6.3.11 VCONN_Swap Message).
 */
static bool pe_can_send_sop_prime(int port)
{
	if (IS_ENABLED(CONFIG_USBC_VCONN)) {
		if (PE_CHK_FLAG(port, PE_FLAGS_EXPLICIT_CONTRACT)) {
			if (prl_get_rev(port, TCPC_TX_SOP) == PD_REV20)
				return tc_is_vconn_src(port) &&
					pe[port].data_role == PD_ROLE_DFP;
			else
				return tc_is_vconn_src(port);
		} else {
			return tc_is_vconn_src(port) &&
				pe[port].power_role == PD_ROLE_SOURCE;
		}
	} else {
		return false;
	}
}

/*
 * Determine if this port may send the given VDM type
 *
 * For PD 2.0, "Only the DFP Shall be an Initrator of Structured VDMs except for
 * the Attention Command that Shall only be initiated by the UFP"
 *
 * For PD 3.0, "Either port May be an Initiator of Structured VDMs except for
 * the Enter Mode and Exit Mode Commands which shall only be initiated by the
 * DFP" (6.4.4.2 Structured VDM)
 *
 * In both revisions, VDMs may only be initiated while in an explicit contract,
 * with the only exception being for cable plug discovery.
 */
static bool pe_can_send_sop_vdm(int port, int vdm_cmd)
{
	if (PE_CHK_FLAG(port, PE_FLAGS_EXPLICIT_CONTRACT)) {
		if (prl_get_rev(port, TCPC_TX_SOP) == PD_REV20) {
			if (pe[port].data_role == PD_ROLE_UFP &&
			    vdm_cmd != CMD_ATTENTION) {
				return false;
			}
		} else {
			if (pe[port].data_role == PD_ROLE_UFP &&
			    (vdm_cmd == CMD_ENTER_MODE ||
			     vdm_cmd == CMD_EXIT_MODE)) {
				return false;
			}
		}
		return true;
	}

	return false;
}

static void pe_send_soft_reset(const int port, enum tcpm_transmit_type type)
{
	pe[port].soft_reset_sop = type;
	set_state_pe(port, PE_SEND_SOFT_RESET);
}

void pe_report_discard(int port)
{
	/*
	 * Clear local AMS indicator as our AMS message was discarded, and flag
	 * the discard for the PE
	 */
	PE_CLR_FLAG(port, PE_FLAGS_LOCALLY_INITIATED_AMS);
	PE_SET_FLAG(port, PE_FLAGS_MSG_DISCARDED);

	/* TODO(b/157228506): Ensure all states are checking discard */
}

void pe_report_error(int port, enum pe_error e, enum tcpm_transmit_type type)
{
	/* This should only be called from the PD task */
	assert(port == TASK_ID_TO_PD_PORT(task_get_current()));

	/*
	 * Generate Hard Reset if Protocol Error occurred
	 * while in PE_Send_Soft_Reset state.
	 */
	if (get_state_pe(port) == PE_SEND_SOFT_RESET) {
		if (pe[port].power_role == PD_ROLE_SINK)
			set_state_pe(port, PE_SNK_HARD_RESET);
		else
			set_state_pe(port, PE_SRC_HARD_RESET);
		return;
	}

	/*
	 * The following states require custom handling of protocol errors,
	 * because they either need special handling of the no GoodCRC case
	 * (cable identity request, send capabilities), occur before explicit
	 * contract (discovery), or happen during a power transition.
	 *
	 * TODO(b/150774779): TCPMv2: Improve pe_error documentation
	 */
	if ((get_state_pe(port) == PE_SRC_SEND_CAPABILITIES ||
			get_state_pe(port) == PE_SRC_TRANSITION_SUPPLY ||
			get_state_pe(port) == PE_PRS_SRC_SNK_WAIT_SOURCE_ON ||
			get_state_pe(port) == PE_SRC_DISABLED ||
			get_state_pe(port) == PE_SRC_DISCOVERY ||
			get_state_pe(port) == PE_VDM_IDENTITY_REQUEST_CBL) ||
			(IS_ENABLED(CONFIG_USBC_VCONN) &&
				get_state_pe(port) == PE_VCS_SEND_PS_RDY_SWAP)
			) {
		PE_SET_FLAG(port, PE_FLAGS_PROTOCOL_ERROR);
		return;
	}

	/*
	 * See section 8.3.3.4.1.1 PE_SRC_Send_Soft_Reset State:
	 *
	 * The PE_Send_Soft_Reset state shall be entered from
	 * any state when a Protocol Error is detected by
	 * Protocol Layer during a Non-Interruptible AMS or when
	 * Message has not been sent after retries. When an explicit
	 * contract is not in effect.  Otherwise go to PE_Snk/Src_Ready
	 */
	if (!PE_CHK_FLAG(port, PE_FLAGS_EXPLICIT_CONTRACT) &&
			(!PE_CHK_FLAG(port, PE_FLAGS_INTERRUPTIBLE_AMS)
			 || (e == ERR_TCH_XMIT))) {
		pe_send_soft_reset(port, type);
	}
	/*
	 * Transition to PE_Snk_Ready or PE_Src_Ready by a Protocol
	 * Error during an Interruptible AMS.
	 */
	else {
		pe_set_ready_state(port);
	}
}

void pe_got_soft_reset(int port)
{
	/* This should only be called from the PD task */
	assert(port == TASK_ID_TO_PD_PORT(task_get_current()));

	/*
	 * The PE_SRC_Soft_Reset state Shall be entered from any state when a
	 * Soft_Reset Message is received from the Protocol Layer.
	 */
	set_state_pe(port, PE_SOFT_RESET);
}

void pe_dpm_request(int port, enum pe_dpm_request req)
{
	PE_SET_DPM_REQUEST(port, req);
}

void pe_vconn_swap_complete(int port)
{
	/* This should only be called from the PD task */
	assert(port == TASK_ID_TO_PD_PORT(task_get_current()));

	PE_SET_FLAG(port, PE_FLAGS_VCONN_SWAP_COMPLETE);
}

void pe_ps_reset_complete(int port)
{
	/* This should only be called from the PD task */
	assert(port == TASK_ID_TO_PD_PORT(task_get_current()));

	PE_SET_FLAG(port, PE_FLAGS_PS_RESET_COMPLETE);
}

void pe_message_sent(int port)
{
	/* This should only be called from the PD task */
	assert(port == TASK_ID_TO_PD_PORT(task_get_current()));

	PE_SET_FLAG(port, PE_FLAGS_TX_COMPLETE);
}

void pd_send_vdm(int port, uint32_t vid, int cmd, const uint32_t *data,
						int count)
{
	/* Copy VDM Header */
	pe[port].vdm_data[0] = VDO(vid, ((vid & USB_SID_PD) == USB_SID_PD) ?
				1 : (PD_VDO_CMD(cmd) <= CMD_ATTENTION),
				VDO_SVDM_VERS(pd_get_vdo_ver(port, TCPC_TX_SOP))
				| cmd);

	/* Copy Data after VDM Header */
	memcpy((pe[port].vdm_data + 1), data, count);

	pe[port].vdm_cnt = count + 1;

	task_wake(PD_PORT_TO_TASK_ID(port));
}

static void pe_handle_detach(void)
{
	const int port = TASK_ID_TO_PD_PORT(task_get_current());

	/*
	 * PD 3.0 Section 8.3.3.3.8
	 * Note: The HardResetCounter is reset on a power cycle or Detach.
	 */
	pe[port].hard_reset_counter = 0;
}
DECLARE_HOOK(HOOK_USB_PD_DISCONNECT, pe_handle_detach, HOOK_PRIO_DEFAULT);

/*
 * Private functions
 */

/* Set the TypeC state machine to a new state. */
test_export_static void set_state_pe(const int port,
				     const enum usb_pe_state new_state)
{
	set_state(port, &pe[port].ctx, &pe_states[new_state]);
}

/* Get the current TypeC state. */
test_export_static enum usb_pe_state get_state_pe(const int port)
{
	return pe[port].ctx.current - &pe_states[0];
}

static bool common_src_snk_dpm_requests(int port)
{
	if (IS_ENABLED(CONFIG_USB_PD_REV30) &&
			PE_CHK_DPM_REQUEST(port, DPM_REQUEST_SEND_ALERT)) {
		PE_CLR_DPM_REQUEST(port, DPM_REQUEST_SEND_ALERT);
		set_state_pe(port, PE_SEND_ALERT);
		return true;
	} else if (IS_ENABLED(CONFIG_USBC_VCONN) &&
			PE_CHK_DPM_REQUEST(port, DPM_REQUEST_VCONN_SWAP)) {
		PE_CLR_DPM_REQUEST(port, DPM_REQUEST_VCONN_SWAP);
		set_state_pe(port, PE_VCS_SEND_SWAP);
		return true;
	} else if (PE_CHK_DPM_REQUEST(port,
					DPM_REQUEST_BIST_RX)) {
		PE_CLR_DPM_REQUEST(port, DPM_REQUEST_BIST_RX);
		set_state_pe(port, PE_BIST_RX);
		return true;
	} else if (PE_CHK_DPM_REQUEST(port,
					DPM_REQUEST_BIST_TX)) {
		PE_CLR_DPM_REQUEST(port, DPM_REQUEST_BIST_TX);
		set_state_pe(port, PE_BIST_TX);
		return true;
	} else if (PE_CHK_DPM_REQUEST(port,
					DPM_REQUEST_SNK_STARTUP)) {
		PE_CLR_DPM_REQUEST(port,
					DPM_REQUEST_SNK_STARTUP);
		set_state_pe(port, PE_SNK_STARTUP);
		return true;
	} else if (PE_CHK_DPM_REQUEST(port,
					DPM_REQUEST_SRC_STARTUP)) {
		PE_CLR_DPM_REQUEST(port,
					DPM_REQUEST_SRC_STARTUP);
		set_state_pe(port, PE_SRC_STARTUP);
		return true;
	} else if (PE_CHK_DPM_REQUEST(port,
					DPM_REQUEST_SOFT_RESET_SEND)) {
		PE_CLR_DPM_REQUEST(port,
					DPM_REQUEST_SOFT_RESET_SEND);
		/* Currently only support sending soft reset to SOP */
		pe_send_soft_reset(port, TCPC_TX_SOP);
		return true;
	} else if (PE_CHK_DPM_REQUEST(port,
					DPM_REQUEST_PORT_DISCOVERY)) {
		PE_CLR_DPM_REQUEST(port,
					DPM_REQUEST_PORT_DISCOVERY);
		if (!PE_CHK_FLAG(port, PE_FLAGS_MODAL_OPERATION)) {
			/*
			 * Clear counters and reset timer to trigger a
			 * port discovery.
			 */
			PE_CLR_FLAG(port, PE_FLAGS_VDM_SETUP_DONE);
			pd_dfp_discovery_init(port);
			pe[port].dr_swap_attempt_counter = 0;
			pe[port].discover_identity_counter = 0;
			pe[port].discover_identity_timer = get_time().val +
						PD_T_DISCOVER_IDENTITY;
		}
		return true;
	} else if (PE_CHK_DPM_REQUEST(port, DPM_REQUEST_VDM)) {
		PE_CLR_DPM_REQUEST(port, DPM_REQUEST_VDM);

		/* Send previously set up SVDM. */
		set_state_pe(port, PE_VDM_REQUEST_DPM);
		return true;
	} else if (PE_CHK_DPM_REQUEST(port, DPM_REQUEST_ENTER_USB)) {
		PE_CLR_DPM_REQUEST(port, DPM_REQUEST_ENTER_USB);

		set_state_pe(port, PE_DEU_SEND_ENTER_USB);
		return true;
	}
	return false;
}

/* Get the previous TypeC state. */
static enum usb_pe_state get_last_state_pe(const int port)
{
	return pe[port].ctx.previous - &pe_states[0];
}

static void print_current_state(const int port)
{
	const char *mode = "";

	if (IS_ENABLED(CONFIG_USB_PD_REV30) &&
			PE_CHK_FLAG(port, PE_FLAGS_FAST_ROLE_SWAP_PATH))
		mode = " FRS-MODE";

	if (IS_ENABLED(USB_PD_DEBUG_LABELS))
		CPRINTS_L1("C%d: %s%s", port,
			pe_state_names[get_state_pe(port)], mode);
	else
		CPRINTS("C%d: pe-st%d", port, get_state_pe(port));
}

static void send_source_cap(int port)
{
#if defined(CONFIG_USB_PD_DYNAMIC_SRC_CAP) || \
		defined(CONFIG_USB_PD_MAX_SINGLE_SOURCE_CURRENT)
	const uint32_t *src_pdo;
	const int src_pdo_cnt = charge_manager_get_source_pdo(&src_pdo, port);
#else
	const uint32_t *src_pdo = pd_src_pdo;
	const int src_pdo_cnt = pd_src_pdo_cnt;
#endif

	if (src_pdo_cnt == 0) {
		/* No source capabilities defined, sink only */
		send_ctrl_msg(port, TCPC_TX_SOP, PD_CTRL_REJECT);
	}

	tx_emsg[port].len = src_pdo_cnt * 4;
	memcpy(tx_emsg[port].buf, (uint8_t *)src_pdo, tx_emsg[port].len);

	send_data_msg(port, TCPC_TX_SOP, PD_DATA_SOURCE_CAP);
}

/*
 * Request desired charge voltage from source.
 */
static void pe_send_request_msg(int port)
{
	uint32_t rdo;
	uint32_t curr_limit;
	uint32_t supply_voltage;

	/* Build and send request RDO */
	pd_build_request(pe[port].vpd_vdo, &rdo, &curr_limit,
			&supply_voltage, port);

	CPRINTF("C%d: Req [%d] %dmV %dmA", port, RDO_POS(rdo),
					supply_voltage, curr_limit);
	if (rdo & RDO_CAP_MISMATCH)
		CPRINTF(" Mismatch");
	CPRINTF("\n");

	pe[port].curr_limit = curr_limit;
	pe[port].supply_voltage = supply_voltage;

	tx_emsg[port].len = 4;

	memcpy(tx_emsg[port].buf, (uint8_t *)&rdo, tx_emsg[port].len);
	send_data_msg(port, TCPC_TX_SOP, PD_DATA_REQUEST);
}

static void pe_update_pdo_flags(int port, uint32_t pdo)
{
#ifdef CONFIG_CHARGE_MANAGER
#ifdef CONFIG_USB_PD_ALT_MODE_DFP
	int charge_whitelisted =
		(pd_get_power_role(port) == PD_ROLE_SINK &&
			pd_charge_from_device(pd_get_identity_vid(port),
			pd_get_identity_pid(port)));
#else
	const int charge_whitelisted = 0;
#endif
#endif

	/* can only parse PDO flags if type is fixed */
	if ((pdo & PDO_TYPE_MASK) != PDO_TYPE_FIXED)
		return;

	if (pdo & PDO_FIXED_DUAL_ROLE)
		tc_partner_dr_power(port, 1);
	else
		tc_partner_dr_power(port, 0);

	if (pdo & PDO_FIXED_UNCONSTRAINED)
		tc_partner_unconstrainedpower(port, 1);
	else
		tc_partner_unconstrainedpower(port, 0);

	if (pdo & PDO_FIXED_COMM_CAP)
		tc_partner_usb_comm(port, 1);
	else
		tc_partner_usb_comm(port, 0);

	if (pdo & PDO_FIXED_DATA_SWAP)
		tc_partner_dr_data(port, 1);
	else
		tc_partner_dr_data(port, 0);

#ifdef CONFIG_CHARGE_MANAGER
	/*
	 * Treat device as a dedicated charger (meaning we should charge
	 * from it) if it does not support power swap, or if it is unconstrained
	 * power, or if we are a sink and the device identity matches a
	 * charging white-list.
	 */
	if (!(pdo & PDO_FIXED_DUAL_ROLE) || (pdo & PDO_FIXED_UNCONSTRAINED) ||
		charge_whitelisted) {
		PE_CLR_FLAG(port, PE_FLAGS_PORT_PARTNER_IS_DUALROLE);
		charge_manager_update_dualrole(port, CAP_DEDICATED);
	} else {
		PE_SET_FLAG(port, PE_FLAGS_PORT_PARTNER_IS_DUALROLE);
		charge_manager_update_dualrole(port, CAP_DUALROLE);
	}
#endif
}

void pd_request_power_swap(int port)
{
	pe_dpm_request(port, DPM_REQUEST_PR_SWAP);
}

int pd_is_port_partner_dualrole(int port)
{
	return PE_CHK_FLAG(port, PE_FLAGS_PORT_PARTNER_IS_DUALROLE);
}

static void pe_prl_execute_hard_reset(int port)
{
	prl_execute_hard_reset(port);
}

#ifdef CONFIG_USB_PD_ALT_MODE_DFP
/*
 * Run discovery at our leisure from PE_SNK_Ready or PE_SRC_Ready, after
 * attempting to get into the desired default policy of DFP/Vconn source
 *
 * Return indicates whether set_state was called, in which case the calling
 * function should return as well.
 */
static bool pe_attempt_port_discovery(int port)
{
	/*
	 * DONE set once modal entry is successful, discovery completes, or
	 * discovery results in a NAK
	 */
	if (PE_CHK_FLAG(port, PE_FLAGS_VDM_SETUP_DONE))
		return false;

	/*
	 * TODO: POLICY decision: move policy functionality out to a separate
	 * file.  For now, try once to become DFP/Vconn source
	 */
	if (PE_CHK_FLAG(port, PE_FLAGS_DR_SWAP_TO_DFP)) {
		PE_CLR_FLAG(port, PE_FLAGS_DR_SWAP_TO_DFP);

		if (pe[port].data_role == PD_ROLE_UFP) {
			PE_SET_FLAG(port, PE_FLAGS_LOCALLY_INITIATED_AMS);
			set_state_pe(port, PE_DRS_SEND_SWAP);
			return true;
		}
	}

	if (IS_ENABLED(CONFIG_USBC_VCONN) &&
			PE_CHK_FLAG(port, PE_FLAGS_VCONN_SWAP_TO_ON)) {
		PE_CLR_FLAG(port, PE_FLAGS_VCONN_SWAP_TO_ON);

		if (!tc_is_vconn_src(port)) {
			PE_SET_FLAG(port, PE_FLAGS_LOCALLY_INITIATED_AMS);
			set_state_pe(port, PE_VCS_SEND_SWAP);
			return true;
		}
	}

	/* If mode entry was successful, disable the timer */
	if (PE_CHK_FLAG(port, PE_FLAGS_VDM_SETUP_DONE)) {
		pe[port].discover_identity_timer = TIMER_DISABLED;
		return false;
	}

	/*
	 * Run discovery functions when the timer indicating either cable
	 * discovery spacing or BUSY spacing runs out.
	 */
	if (get_time().val > pe[port].discover_identity_timer) {
		if (pd_get_identity_discovery(port, TCPC_TX_SOP_PRIME) ==
				PD_DISC_NEEDED && pe_can_send_sop_prime(port)) {
			pe[port].tx_type = TCPC_TX_SOP_PRIME;
			set_state_pe(port, PE_VDM_IDENTITY_REQUEST_CBL);
			return true;
		} else if (pd_get_identity_discovery(port, TCPC_TX_SOP) ==
				PD_DISC_NEEDED &&
				pe_can_send_sop_vdm(port, CMD_DISCOVER_IDENT)) {
			pe[port].tx_type = TCPC_TX_SOP;
			set_state_pe(port,
				     PE_INIT_PORT_VDM_IDENTITY_REQUEST);
			return true;
		} else if (pd_get_svids_discovery(port, TCPC_TX_SOP) ==
				PD_DISC_NEEDED &&
				pe_can_send_sop_vdm(port, CMD_DISCOVER_SVID)) {
			pe[port].tx_type = TCPC_TX_SOP;
			set_state_pe(port, PE_INIT_VDM_SVIDS_REQUEST);
			return true;
		} else if (pd_get_modes_discovery(port, TCPC_TX_SOP) ==
				PD_DISC_NEEDED &&
				pe_can_send_sop_vdm(port, CMD_DISCOVER_MODES)) {
			pe[port].tx_type = TCPC_TX_SOP;
			set_state_pe(port, PE_INIT_VDM_MODES_REQUEST);
			return true;
		} else if (pd_get_svids_discovery(port, TCPC_TX_SOP_PRIME)
				== PD_DISC_NEEDED &&
				pe_can_send_sop_prime(port)) {
			pe[port].tx_type = TCPC_TX_SOP_PRIME;
			set_state_pe(port, PE_INIT_VDM_SVIDS_REQUEST);
			return true;
		} else if (pd_get_modes_discovery(port, TCPC_TX_SOP_PRIME) ==
				PD_DISC_NEEDED &&
				pe_can_send_sop_prime(port)) {
			pe[port].tx_type = TCPC_TX_SOP_PRIME;
			set_state_pe(port, PE_INIT_VDM_MODES_REQUEST);
			return true;
		}
	}

	return false;
}
#endif

bool pd_setup_vdm_request(int port, enum tcpm_transmit_type tx_type,
		uint32_t *vdm, uint32_t vdo_cnt)
{
	if (vdo_cnt < VDO_HDR_SIZE || vdo_cnt > VDO_MAX_SIZE)
		return false;

	pe[port].tx_type = tx_type;
	memcpy(pe[port].vdm_data, vdm, vdo_cnt * sizeof(*vdm));
	pe[port].vdm_cnt = vdo_cnt;

	return true;
}

int pd_dev_store_rw_hash(int port, uint16_t dev_id, uint32_t *rw_hash,
					uint32_t current_image)
{
	pe[port].dev_id = dev_id;
	memcpy(pe[port].dev_rw_hash, rw_hash, PD_RW_HASH_SIZE);
#ifdef CONFIG_CMD_PD_DEV_DUMP_INFO
	pd_dev_dump_info(dev_id, rw_hash);
#endif
	pe[port].current_image = current_image;

	if (IS_ENABLED(CONFIG_USB_PD_HOST_CMD)) {
		int i;

		/* Search table for matching device / hash */
		for (i = 0; i < RW_HASH_ENTRIES; i++)
			if (dev_id == rw_hash_table[i].dev_id)
				return !memcmp(rw_hash,
					       rw_hash_table[i].dev_rw_hash,
					       PD_RW_HASH_SIZE);
	}

	return 0;
}

void pd_dev_get_rw_hash(int port, uint16_t *dev_id, uint8_t *rw_hash,
			 uint32_t *current_image)
{
	*dev_id = pe[port].dev_id;
	*current_image = pe[port].current_image;
	if (*dev_id)
		memcpy(rw_hash, pe[port].dev_rw_hash, PD_RW_HASH_SIZE);
}

/*
 * This function must only be called from the PE_SNK_READY entry and
 * PE_SRC_READY entry State.
 */
static void pe_update_wait_and_add_jitter_timer(int port)
{
	/*
	 * In PD2.0 Mode
	 *
	 * For Source:
	 * Give the sink some time to send any messages
	 * before we may send messages of our own.  Add
	 * some jitter of up to ~345ms, to prevent
	 * multiple collisions. This delay also allows
	 * the sink device to request power role swap
	 * and allow the the accept message to be sent
	 * prior to CMD_DISCOVER_IDENT being sent in the
	 * SRC_READY state.
	 *
	 * For Sink:
	 * Give the source some time to send any messages before
	 * we start our interrogation.  Add some jitter of up to
	 * ~345ms to prevent multiple collisions.
	 */
	if (prl_get_rev(port, TCPC_TX_SOP) == PD_REV20 &&
			PE_CHK_FLAG(port, PE_FLAGS_FIRST_MSG)) {
		pe[port].wait_and_add_jitter_timer = get_time().val +
				SRC_SNK_READY_HOLD_OFF_US +
				(get_time().le.lo & 0xf) * 23 * MSEC;
	}
}

/**
 * PE_SRC_Startup
 */
static void pe_src_startup_entry(int port)
{
	print_current_state(port);

	/* Reset CapsCounter */
	pe[port].caps_counter = 0;

	/* Reset the protocol layer */
	prl_reset(port);

	/* Set initial data role */
	pe[port].data_role = pd_get_data_role(port);

	/* Set initial power role */
	pe[port].power_role = PD_ROLE_SOURCE;

	/* Clear explicit contract. */
	pe_invalidate_explicit_contract(port);

	if (PE_CHK_FLAG(port, PE_FLAGS_PR_SWAP_COMPLETE)) {
		PE_CLR_FLAG(port, PE_FLAGS_PR_SWAP_COMPLETE);

		/* Start SwapSourceStartTimer */
		pe[port].swap_source_start_timer = get_time().val +
			PD_T_SWAP_SOURCE_START;
	} else {
		/*
		 * SwapSourceStartTimer delay is not needed, so trigger now.
		 * We can't use set_state_pe here, since we need to ensure that
		 * the protocol layer is running again (done in run function).
		 */
		pe[port].swap_source_start_timer = get_time().val;

		/*
		 * Set DiscoverIdentityTimer to trigger when we enter
		 * src_discovery for the first time.  After initial startup
		 * set, vdm_identity_request_cbl will handle the timer updates.
		 */
		pe[port].discover_identity_timer = get_time().val;

		/* Clear port discovery flags */
		PE_CLR_FLAG(port, PE_FLAGS_VDM_SETUP_DONE);
		pd_dfp_discovery_init(port);
		pe[port].ama_vdo = PD_VDO_INVALID;
		pe[port].vpd_vdo = PD_VDO_INVALID;
		pe[port].discover_identity_counter = 0;

		/* Reset dr swap attempt counter */
		pe[port].dr_swap_attempt_counter = 0;
	}
}

static void pe_src_startup_run(int port)
{
	/* Wait until protocol layer is running */
	if (!prl_is_running(port))
		return;

	if (get_time().val > pe[port].swap_source_start_timer)
		set_state_pe(port, PE_SRC_SEND_CAPABILITIES);
}

/**
 * PE_SRC_Discovery
 */
static void pe_src_discovery_entry(int port)
{
	print_current_state(port);

	/*
	 * Initialize and run the SourceCapabilityTimer in order
	 * to trigger sending a Source_Capabilities Message.
	 *
	 * The SourceCapabilityTimer Shall continue to run during
	 * identity discover and Shall Not be initialized on re-entry
	 * to PE_SRC_Discovery.
	 *
	 * Note: Cable identity is the only valid VDM to probe before a contract
	 * is in place.  All other probing must happen from ready states.
	 */
	if (get_last_state_pe(port) != PE_VDM_IDENTITY_REQUEST_CBL)
		pe[port].source_cap_timer =
				get_time().val + PD_T_SEND_SOURCE_CAP;
}

static void pe_src_discovery_run(int port)
{
	/*
	 * Transition to the PE_SRC_Send_Capabilities state when:
	 *   1) The SourceCapabilityTimer times out and
	 *      CapsCounter ≤ nCapsCount.
	 *
	 * Transition to the PE_SRC_Disabled state when:
	 *   1) The Port Partners are not presently PD Connected
	 *   2) And the SourceCapabilityTimer times out
	 *   3) And CapsCounter > nCapsCount.
	 *
	 * Transition to the PE_SRC_VDM_Identity_request state when:
	 *   1) DPM requests the identity of the cable plug and
	 *   2) DiscoverIdentityCounter < nDiscoverIdentityCount
	 */
	if (get_time().val > pe[port].source_cap_timer) {
		if (pe[port].caps_counter <= N_CAPS_COUNT) {
			set_state_pe(port, PE_SRC_SEND_CAPABILITIES);
			return;
		} else if (!PE_CHK_FLAG(port, PE_FLAGS_PD_CONNECTION)) {
			set_state_pe(port, PE_SRC_DISABLED);
			return;
		}
	}

	/*
	 * Note: While the DiscoverIdentityTimer is only required in an explicit
	 * contract, we use it here to ensure we space any potential BUSY
	 * requests properly.
	 */
	if (pd_get_identity_discovery(port, TCPC_TX_SOP_PRIME) == PD_DISC_NEEDED
			&& get_time().val > pe[port].discover_identity_timer
			&& pe_can_send_sop_prime(port)) {
		pe[port].tx_type = TCPC_TX_SOP_PRIME;
		set_state_pe(port, PE_VDM_IDENTITY_REQUEST_CBL);
		return;
	}

	/*
	 * Transition to the PE_SRC_Disabled state when:
	 *   1) The Port Partners have not been PD Connected.
	 *   2) And the NoResponseTimer times out.
	 *   3) And the HardResetCounter > nHardResetCount.
	 */
	if (!PE_CHK_FLAG(port, PE_FLAGS_PD_CONNECTION) &&
			get_time().val > pe[port].no_response_timer &&
			pe[port].hard_reset_counter > N_HARD_RESET_COUNT) {
		set_state_pe(port, PE_SRC_DISABLED);
		return;
	}
}

/**
 * PE_SRC_Send_Capabilities
 */
static void pe_src_send_capabilities_entry(int port)
{
	print_current_state(port);

	/* Send PD Capabilities message */
	send_source_cap(port);

	/* Increment CapsCounter */
	pe[port].caps_counter++;

	/* Stop sender response timer */
	pe[port].sender_response_timer = TIMER_DISABLED;
}

static void pe_src_send_capabilities_run(int port)
{
	/*
	 * If a GoodCRC Message is received then the Policy Engine Shall:
	 *  1) Stop the NoResponseTimer.
	 *  2) Reset the HardResetCounter and CapsCounter to zero.
	 *  3) Initialize and run the SenderResponseTimer.
	 */
	if (PE_CHK_FLAG(port, PE_FLAGS_TX_COMPLETE) &&
		pe[port].sender_response_timer == TIMER_DISABLED) {
		PE_CLR_FLAG(port, PE_FLAGS_TX_COMPLETE);

		/* Stop the NoResponseTimer */
		pe[port].no_response_timer = TIMER_DISABLED;

		/* Reset the HardResetCounter to zero */
		pe[port].hard_reset_counter = 0;

		/* Reset the CapsCounter to zero */
		pe[port].caps_counter = 0;

		/* Initialize and run the SenderResponseTimer */
		pe[port].sender_response_timer = get_time().val +
							PD_T_SENDER_RESPONSE;
	}

	/*
	 * Transition to the PE_SRC_Negotiate_Capability state when:
	 *  1) A Request Message is received from the Sink
	 */
	if (pe[port].sender_response_timer != TIMER_DISABLED &&
			PE_CHK_FLAG(port, PE_FLAGS_MSG_RECEIVED)) {
		PE_CLR_FLAG(port, PE_FLAGS_MSG_RECEIVED);

		/*
		 * Request Message Received?
		 */
		if (PD_HEADER_CNT(rx_emsg[port].header) > 0 &&
			PD_HEADER_TYPE(rx_emsg[port].header) ==
							PD_DATA_REQUEST) {

			/*
			 * Set to highest revision supported by both
			 * ports.
			 */
			prl_set_rev(port, TCPC_TX_SOP,
			MIN(PD_REVISION, PD_HEADER_REV(rx_emsg[port].header)));

			/*
			 * If port partner runs PD 2.0, cable communication must
			 * also be PD 2.0
			 */
			if (prl_get_rev(port, TCPC_TX_SOP) == PD_REV20)
				prl_set_rev(port, TCPC_TX_SOP_PRIME, PD_REV20);

			/* We are PD connected */
			PE_SET_FLAG(port, PE_FLAGS_PD_CONNECTION);
			tc_pd_connection(port, 1);

			/*
			 * Handle the Sink Request in
			 * PE_SRC_Negotiate_Capability state
			 */
			set_state_pe(port, PE_SRC_NEGOTIATE_CAPABILITY);
			return;
		}

		/*
		 * We have a Protocol Error.
		 *	PE_SNK/SRC_READY if explicit contract
		 *	PE_SEND_SOFT_RESET otherwise
		 */
		if (PE_CHK_FLAG(port, PE_FLAGS_EXPLICIT_CONTRACT))
			pe_set_ready_state(port);
		else
			pe_send_soft_reset(port, TCPC_TX_SOP);

		return;
	}

	/*
	 * Transition to the PE_SRC_Discovery state when:
	 *  1) The Protocol Layer indicates that the Message has not been sent
	 *     and we are presently not Connected
	 *
	 * NOTE: The PE_FLAGS_PROTOCOL_ERROR is set if a GoodCRC Message
	 *       is not received.
	 */
	if (PE_CHK_FLAG(port, PE_FLAGS_PROTOCOL_ERROR) &&
			!PE_CHK_FLAG(port, PE_FLAGS_PD_CONNECTION)) {
		PE_CLR_FLAG(port, PE_FLAGS_PROTOCOL_ERROR);

		set_state_pe(port, PE_SRC_DISCOVERY);
		return;
	}

	/*
	 * Transition to the PE_SRC_Disabled state when:
	 *  1) The Port Partners have not been PD Connected
	 *  2) The NoResponseTimer times out
	 *  3) And the HardResetCounter > nHardResetCount.
	 *
	 * Transition to the Error Recovery state when:
	 *  1) The Port Partners have previously been PD Connected
	 *  2) The NoResponseTimer times out
	 *  3) And the HardResetCounter > nHardResetCount.
	 */
	if (get_time().val > pe[port].no_response_timer) {
		if (pe[port].hard_reset_counter <= N_HARD_RESET_COUNT)
			set_state_pe(port, PE_SRC_HARD_RESET);
		else if (PE_CHK_FLAG(port, PE_FLAGS_PD_CONNECTION))
			set_state_pe(port, PE_WAIT_FOR_ERROR_RECOVERY);
		else
			set_state_pe(port, PE_SRC_DISABLED);
		return;
	}

	/*
	 * Transition to the PE_SRC_Hard_Reset state when:
	 *  1) The SenderResponseTimer times out.
	 */
	if (get_time().val > pe[port].sender_response_timer) {
		set_state_pe(port, PE_SRC_HARD_RESET);
		return;
	}
}

/**
 * PE_SRC_Negotiate_Capability
 */
static void pe_src_negotiate_capability_entry(int port)
{
	uint32_t payload;

	print_current_state(port);

	/* Get message payload */
	payload = *(uint32_t *)(&rx_emsg[port].buf);

	/*
	 * Evaluate the Request from the Attached Sink
	 */

	/*
	 * Transition to the PE_SRC_Capability_Response state when:
	 *  1) The Request cannot be met.
	 *  2) Or the Request can be met later from the Power Reserve
	 *
	 * Transition to the PE_SRC_Transition_Supply state when:
	 *  1) The Request can be met
	 *
	 */
	if (pd_check_requested_voltage(payload, port) != EC_SUCCESS) {
		set_state_pe(port, PE_SRC_CAPABILITY_RESPONSE);
	} else {
		PE_SET_FLAG(port, PE_FLAGS_ACCEPT);
		pe[port].requested_idx = RDO_POS(payload);
		set_state_pe(port, PE_SRC_TRANSITION_SUPPLY);
	}
}

/**
 * PE_SRC_Transition_Supply
 */
static void pe_src_transition_supply_entry(int port)
{
	print_current_state(port);

	/* Transition Power Supply */
	pd_transition_voltage(pe[port].requested_idx);

	/* Send a GotoMin Message or otherwise an Accept Message */
	if (PE_CHK_FLAG(port, PE_FLAGS_ACCEPT)) {
		PE_CLR_FLAG(port, PE_FLAGS_ACCEPT);
		send_ctrl_msg(port, TCPC_TX_SOP, PD_CTRL_ACCEPT);
	} else {
		send_ctrl_msg(port, TCPC_TX_SOP, PD_CTRL_GOTO_MIN);
	}

}

static void pe_src_transition_supply_run(int port)
{
	/*
	 * Transition to the PE_SRC_Ready state when:
	 *  1) The power supply is ready.
	 *
	 *  NOTE: This code block is executed twice:
	 *        First Pass)
	 *            When PE_FLAGS_TX_COMPLETE is set due to the
	 *            PD_CTRL_ACCEPT or PD_CTRL_GOTO_MIN messages
	 *            being sent.
	 *
	 *        Second Pass)
	 *            When PE_FLAGS_TX_COMPLETE is set due to the
	 *            PD_CTRL_PS_RDY message being sent.
	 */
	if (PE_CHK_FLAG(port, PE_FLAGS_TX_COMPLETE)) {
		PE_CLR_FLAG(port, PE_FLAGS_TX_COMPLETE);

		/*
		 * NOTE: If a message was received,
		 * pe_src_ready state will handle it.
		 */

		if (PE_CHK_FLAG(port, PE_FLAGS_PS_READY)) {
			PE_CLR_FLAG(port, PE_FLAGS_PS_READY);
			/* NOTE: Second pass through this code block */
			/* Explicit Contract is now in place */
			pe_set_explicit_contract(port);

			/*
			 * Set first message flag to trigger a wait and add
			 * jitter delay when operating in PD2.0 mode.
			 */
			PE_SET_FLAG(port, PE_FLAGS_FIRST_MSG);

			set_state_pe(port, PE_SRC_READY);
		} else {
			/* NOTE: First pass through this code block */
			/* Send PS_RDY message */
			send_ctrl_msg(port, TCPC_TX_SOP, PD_CTRL_PS_RDY);
			PE_SET_FLAG(port, PE_FLAGS_PS_READY);
		}

		return;
	}

	/*
	 * Transition to the PE_SRC_Hard_Reset state when:
	 *  1) A Protocol Error occurs.
	 */
	if (PE_CHK_FLAG(port, PE_FLAGS_PROTOCOL_ERROR)) {
		PE_CLR_FLAG(port, PE_FLAGS_PROTOCOL_ERROR);
		set_state_pe(port, PE_SRC_HARD_RESET);
	}
}

/**
 * PE_SRC_Ready
 */
static void pe_src_ready_entry(int port)
{
	print_current_state(port);

	/* Ensure any message send flags are cleaned up */
	PE_CLR_FLAG(port, PE_FLAGS_READY_CLR);

	/*
	 * Wait and add jitter if we are operating in PD2.0 mode and no messages
	 * have been sent since enter this state.
	 */
	pe_update_wait_and_add_jitter_timer(port);
}

static void pe_src_ready_run(int port)
{
	uint32_t payload;
	uint8_t type;
	uint8_t cnt;
	uint8_t ext;

	/*
	 * Don't delay handling a hard reset from the device policy manager.
	 */
	if (PE_CHK_DPM_REQUEST(port, DPM_REQUEST_HARD_RESET_SEND)) {
		PE_CLR_DPM_REQUEST(port, DPM_REQUEST_HARD_RESET_SEND);
		set_state_pe(port, PE_SRC_HARD_RESET);
		return;
	}

	/*
	 * Handle incoming messages before discovery and DPMs other than hard
	 * reset
	 */
	if (PE_CHK_FLAG(port, PE_FLAGS_MSG_RECEIVED)) {
		PE_CLR_FLAG(port, PE_FLAGS_MSG_RECEIVED);

		type = PD_HEADER_TYPE(rx_emsg[port].header);
		cnt = PD_HEADER_CNT(rx_emsg[port].header);
		ext = PD_HEADER_EXT(rx_emsg[port].header);
		payload = *(uint32_t *)rx_emsg[port].buf;

		/* Extended Message Requests */
		if (ext > 0) {
			switch (type) {
#if defined(CONFIG_USB_PD_REV30) && defined(CONFIG_BATTERY)
			case PD_EXT_GET_BATTERY_CAP:
				set_state_pe(port, PE_GIVE_BATTERY_CAP);
				break;
			case PD_EXT_GET_BATTERY_STATUS:
				set_state_pe(port, PE_GIVE_BATTERY_STATUS);
				break;
#endif /* CONFIG_USB_PD_REV30 && CONFIG_BATTERY*/
			default:
				set_state_pe(port, PE_SEND_NOT_SUPPORTED);
			}
			return;
		}
		/* Data Message Requests */
		else if (cnt > 0) {
			switch (type) {
			case PD_DATA_REQUEST:
				set_state_pe(port, PE_SRC_NEGOTIATE_CAPABILITY);
				return;
			case PD_DATA_SINK_CAP:
				break;
			case PD_DATA_VENDOR_DEF:
				if (PD_HEADER_TYPE(rx_emsg[port].header) ==
							PD_DATA_VENDOR_DEF) {
					if (PD_VDO_SVDM(payload)) {
						set_state_pe(port,
							PE_VDM_RESPONSE);
					} else
						set_state_pe(port,
						PE_HANDLE_CUSTOM_VDM_REQUEST);
				}
				return;
			case PD_DATA_BIST:
				set_state_pe(port, PE_BIST_TX);
				return;
			default:
				set_state_pe(port, PE_SEND_NOT_SUPPORTED);
				return;
			}
		}
		/* Control Message Requests */
		else {
			switch (type) {
			case PD_CTRL_GOOD_CRC:
				break;
			case PD_CTRL_NOT_SUPPORTED:
				break;
			case PD_CTRL_PING:
				break;
			case PD_CTRL_GET_SOURCE_CAP:
				set_state_pe(port, PE_SRC_SEND_CAPABILITIES);
				return;
			case PD_CTRL_GET_SINK_CAP:
				set_state_pe(port, PE_SNK_GIVE_SINK_CAP);
				return;
			case PD_CTRL_GOTO_MIN:
				break;
			case PD_CTRL_PR_SWAP:
				set_state_pe(port,
					PE_PRS_SRC_SNK_EVALUATE_SWAP);
				return;
			case PD_CTRL_DR_SWAP:
				if (PE_CHK_FLAG(port,
						PE_FLAGS_MODAL_OPERATION)) {
					set_state_pe(port, PE_SRC_HARD_RESET);
					return;
				}

				set_state_pe(port, PE_DRS_EVALUATE_SWAP);
				return;
			case PD_CTRL_VCONN_SWAP:
				set_state_pe(port, PE_VCS_EVALUATE_SWAP);
				return;
			default:
				set_state_pe(port, PE_SEND_NOT_SUPPORTED);
				return;
			}
		}
	} else if (PE_CHK_FLAG(port, PE_FLAGS_VDM_REQUEST_CONTINUE)) {
		PE_CLR_FLAG(port, PE_FLAGS_VDM_REQUEST_CONTINUE);
		set_state_pe(port, PE_VDM_REQUEST_DPM);
		return;
	}

	if (pe[port].wait_and_add_jitter_timer == TIMER_DISABLED ||
		get_time().val > pe[port].wait_and_add_jitter_timer) {

		PE_CLR_FLAG(port, PE_FLAGS_FIRST_MSG);
		pe[port].wait_and_add_jitter_timer = TIMER_DISABLED;

		/*
		 * Attempt discovery if possible, and return if state was
		 * changed for that discovery.
		 */
		if (pe_attempt_port_discovery(port))
			return;

		/*
		 * Handle Device Policy Manager Requests
		 */

		/*
		 * Ignore sink specific request:
		 *   DPM_REQUEST_NEW_POWER_LEVEL
		 *   DPM_REQUEST_SOURCE_CAP
		 */

		PE_CLR_DPM_REQUEST(port, DPM_REQUEST_NEW_POWER_LEVEL |
					DPM_REQUEST_SOURCE_CAP);

		if (pe[port].dpm_request) {
			uint32_t dpm_request = pe[port].dpm_request;

			PE_SET_FLAG(port, PE_FLAGS_LOCALLY_INITIATED_AMS);

			if (PE_CHK_DPM_REQUEST(port, DPM_REQUEST_DR_SWAP)) {
				PE_CLR_DPM_REQUEST(port, DPM_REQUEST_DR_SWAP);
				if (PE_CHK_FLAG(port, PE_FLAGS_MODAL_OPERATION))
					set_state_pe(port, PE_SRC_HARD_RESET);
				else
					set_state_pe(port, PE_DRS_SEND_SWAP);
			} else if (PE_CHK_DPM_REQUEST(port,
						DPM_REQUEST_PR_SWAP)) {
				PE_CLR_DPM_REQUEST(port, DPM_REQUEST_PR_SWAP);
				set_state_pe(port, PE_PRS_SRC_SNK_SEND_SWAP);
			} else if (PE_CHK_DPM_REQUEST(port,
							DPM_REQUEST_GOTO_MIN)) {
				PE_CLR_DPM_REQUEST(port, DPM_REQUEST_GOTO_MIN);
				set_state_pe(port, PE_SRC_TRANSITION_SUPPLY);
			} else if (PE_CHK_DPM_REQUEST(port,
						DPM_REQUEST_SRC_CAP_CHANGE)) {
				PE_CLR_DPM_REQUEST(port,
						DPM_REQUEST_SRC_CAP_CHANGE);
				set_state_pe(port, PE_SRC_SEND_CAPABILITIES);
			} else if (PE_CHK_DPM_REQUEST(port,
						DPM_REQUEST_SEND_PING)) {
				PE_CLR_DPM_REQUEST(port, DPM_REQUEST_SEND_PING);
				set_state_pe(port, PE_SRC_PING);
			} else if (!common_src_snk_dpm_requests(port)) {
				CPRINTF("Unhandled DPM Request %x received\n",
					dpm_request);
				PE_CLR_DPM_REQUEST(port, dpm_request);
				PE_CLR_FLAG(port,
					PE_FLAGS_LOCALLY_INITIATED_AMS);
			}

			return;
		}

		/* No DPM requests; attempt mode entry/exit if needed */
		dpm_run(port);
	}
}

/**
 * PE_SRC_Disabled
 */
static void pe_src_disabled_entry(int port)
{
	print_current_state(port);

	if ((pe[port].vpd_vdo >= 0) && VPD_VDO_CTS(pe[port].vpd_vdo)) {
		/*
		 * Inform the Device Policy Manager that a Charge-Through VCONN
		 * Powered Device was detected.
		 */
		tc_ctvpd_detected(port);
	}

	/*
	 * Unresponsive to USB Power Delivery messaging, but not to Hard Reset
	 * Signaling. See pe_got_hard_reset
	 */
}

/**
 * PE_SRC_Capability_Response
 */
static void pe_src_capability_response_entry(int port)
{
	print_current_state(port);

	/* NOTE: Wait messaging should be implemented. */

	send_ctrl_msg(port, TCPC_TX_SOP, PD_CTRL_REJECT);
}

static void pe_src_capability_response_run(int port)
{
	/*
	 * Transition to the PE_SRC_Ready state when:
	 *  1) There is an Explicit Contract and
	 *  2) A Reject Message has been sent and the present Contract is still
	 *     Valid or
	 *  3) A Wait Message has been sent.
	 *
	 * Transition to the PE_SRC_Hard_Reset state when:
	 *  1) There is an Explicit Contract and
	 *  2) The Reject Message has been sent and the present
	 *     Contract is Invalid
	 *
	 * Transition to the PE_SRC_Wait_New_Capabilities state when:
	 *  1) There is no Explicit Contract and
	 *  2) A Reject Message has been sent or
	 *  3) A Wait Message has been sent.
	 */
	if (PE_CHK_FLAG(port, PE_FLAGS_TX_COMPLETE)) {
		PE_CLR_FLAG(port, PE_FLAGS_TX_COMPLETE);

		if (PE_CHK_FLAG(port, PE_FLAGS_EXPLICIT_CONTRACT))
			/*
			 * NOTE: The src capabilities listed in
			 *       board/xxx/usb_pd_policy.c will not
			 *       change so the present contract will
			 *       never be invalid.
			 */
			set_state_pe(port, PE_SRC_READY);
		else
			/*
			 * NOTE: The src capabilities listed in
			 *       board/xxx/usb_pd_policy.c will not
			 *       change, so no need to resending them
			 *       again. Transition to disabled state.
			 */
			set_state_pe(port, PE_SRC_DISABLED);
	}
}

/**
 * PE_SRC_Hard_Reset
 */
static void pe_src_hard_reset_entry(int port)
{
	print_current_state(port);

	/* Generate Hard Reset Signal */
	prl_execute_hard_reset(port);

	/* Increment the HardResetCounter */
	pe[port].hard_reset_counter++;

	/* Start NoResponseTimer */
	pe[port].no_response_timer = get_time().val + PD_T_NO_RESPONSE;

	/* Start PSHardResetTimer */
	pe[port].ps_hard_reset_timer = get_time().val + PD_T_PS_HARD_RESET;

	/* Clear error flags */
	PE_CLR_FLAG(port, PE_FLAGS_VDM_REQUEST_NAKED |
			  PE_FLAGS_PROTOCOL_ERROR |
			  PE_FLAGS_VDM_REQUEST_BUSY);
}

static void pe_src_hard_reset_run(int port)
{
	/*
	 * Transition to the PE_SRC_Transition_to_default state when:
	 *  1) The PSHardResetTimer times out.
	 */
	if (get_time().val > pe[port].ps_hard_reset_timer)
		set_state_pe(port, PE_SRC_TRANSITION_TO_DEFAULT);
}

/**
 * PE_SRC_Hard_Reset_Received
 */
static void pe_src_hard_reset_received_entry(int port)
{
	print_current_state(port);

	/* Start NoResponseTimer */
	pe[port].no_response_timer = get_time().val + PD_T_NO_RESPONSE;

	/* Start PSHardResetTimer */
	pe[port].ps_hard_reset_timer = get_time().val + PD_T_PS_HARD_RESET;
}

static void pe_src_hard_reset_received_run(int port)
{
	/*
	 * Transition to the PE_SRC_Transition_to_default state when:
	 *  1) The PSHardResetTimer times out.
	 */
	if (get_time().val > pe[port].ps_hard_reset_timer)
		set_state_pe(port, PE_SRC_TRANSITION_TO_DEFAULT);
}

/**
 * PE_SRC_Transition_To_Default
 */
static void pe_src_transition_to_default_entry(int port)
{
	print_current_state(port);

	/* Reset flags */
	pe[port].flags = 0;

	/* Reset DPM Request */
	pe[port].dpm_request = 0;

	/*
	 * Request Device Policy Manager to request power
	 * supply Hard Resets to vSafe5V via vSafe0V
	 * Reset local HW
	 * Request Device Policy Manager to set Port Data
	 * Role to DFP and turn off VCONN
	 */
	tc_hard_reset_request(port);
}

static void pe_src_transition_to_default_run(int port)
{
	/*
	 * Transition to the PE_SRC_Startup state when:
	 *   1) The power supply has reached the default level.
	 */
	if (PE_CHK_FLAG(port, PE_FLAGS_PS_RESET_COMPLETE)) {
		PE_CLR_FLAG(port, PE_FLAGS_PS_RESET_COMPLETE);
		/* Inform the Protocol Layer that the Hard Reset is complete */
		prl_hard_reset_complete(port);
		set_state_pe(port, PE_SRC_STARTUP);
	}
}

/**
 * PE_SNK_Startup State
 */
static void pe_snk_startup_entry(int port)
{
	print_current_state(port);

	/* Reset the protocol layer */
	prl_reset(port);

	/* Set initial data role */
	pe[port].data_role = pd_get_data_role(port);

	/* Set initial power role */
	pe[port].power_role = PD_ROLE_SINK;

	/* Invalidate explicit contract */
	pe_invalidate_explicit_contract(port);

	if (PE_CHK_FLAG(port, PE_FLAGS_PR_SWAP_COMPLETE)) {
		PE_CLR_FLAG(port, PE_FLAGS_PR_SWAP_COMPLETE);
	} else {
		/*
		 * Set DiscoverIdentityTimer to trigger when we enter
		 * snk_ready for the first time.
		 */
		pe[port].discover_identity_timer = get_time().val;

		/* Clear port discovery flags */
		PE_CLR_FLAG(port, PE_FLAGS_VDM_SETUP_DONE);
		pd_dfp_discovery_init(port);
		pe[port].discover_identity_counter = 0;

		/* Reset dr swap attempt counter */
		pe[port].dr_swap_attempt_counter = 0;

		/*
		 * TODO: POLICY decision:
		 * Mark that we'd like to try being Vconn source and DFP
		 */
		PE_SET_FLAG(port, PE_FLAGS_DR_SWAP_TO_DFP);
		PE_SET_FLAG(port, PE_FLAGS_VCONN_SWAP_TO_ON);
	}
}

static void pe_snk_startup_run(int port)
{
	/* Wait until protocol layer is running */
	if (!prl_is_running(port))
		return;

	/*
	 * Once the reset process completes, the Policy Engine Shall
	 * transition to the PE_SNK_Discovery state
	 */
	set_state_pe(port, PE_SNK_DISCOVERY);
}

/**
 * PE_SNK_Discovery State
 */
static void pe_snk_discovery_entry(int port)
{
	print_current_state(port);
}

static void pe_snk_discovery_run(int port)
{
	/*
	 * Transition to the PE_SNK_Wait_for_Capabilities state when:
	 *   1) VBUS has been detected
	 */
	if (pd_is_vbus_present(port))
		set_state_pe(port, PE_SNK_WAIT_FOR_CAPABILITIES);
}

/**
 * PE_SNK_Wait_For_Capabilities State
 */
static void pe_snk_wait_for_capabilities_entry(int port)
{
	print_current_state(port);

	/* Initialize and start the SinkWaitCapTimer */
	pe[port].timeout = get_time().val + PD_T_SINK_WAIT_CAP;
}

static void pe_snk_wait_for_capabilities_run(int port)
{
	uint8_t type;
	uint8_t cnt;
	uint8_t ext;

	/*
	 * Transition to the PE_SNK_Evaluate_Capability state when:
	 *  1) A Source_Capabilities Message is received.
	 */
	if (PE_CHK_FLAG(port, PE_FLAGS_MSG_RECEIVED)) {
		PE_CLR_FLAG(port, PE_FLAGS_MSG_RECEIVED);

		type = PD_HEADER_TYPE(rx_emsg[port].header);
		cnt = PD_HEADER_CNT(rx_emsg[port].header);
		ext = PD_HEADER_EXT(rx_emsg[port].header);

		if ((ext == 0) && (cnt > 0) && (type == PD_DATA_SOURCE_CAP)) {
			set_state_pe(port, PE_SNK_EVALUATE_CAPABILITY);
			return;
		}
	}

	/* When the SinkWaitCapTimer times out, perform a Hard Reset. */
	if (get_time().val > pe[port].timeout) {
		PE_SET_FLAG(port, PE_FLAGS_SNK_WAIT_CAP_TIMEOUT);
		set_state_pe(port, PE_SNK_HARD_RESET);
	}
}

/**
 * PE_SNK_Evaluate_Capability State
 */
static void pe_snk_evaluate_capability_entry(int port)
{
	uint32_t *pdo = (uint32_t *)rx_emsg[port].buf;
	uint32_t num = rx_emsg[port].len >> 2;
	int i;

	print_current_state(port);

	/* Reset Hard Reset counter to zero */
	pe[port].hard_reset_counter = 0;

	/* Set to highest revision supported by both ports. */
	prl_set_rev(port, TCPC_TX_SOP,
			MIN(PD_REVISION, PD_HEADER_REV(rx_emsg[port].header)));

	/*
	 * If port partner runs PD 2.0, cable communication must
	 * also be PD 2.0
	 */
	if (prl_get_rev(port, TCPC_TX_SOP) == PD_REV20)
		prl_set_rev(port, TCPC_TX_SOP_PRIME, PD_REV20);

	pe[port].src_cap_cnt = num;

	for (i = 0; i < num; i++)
		pe[port].src_caps[i] = pdo[i];

	/* src cap 0 should be fixed PDO */
	pe_update_pdo_flags(port, pdo[0]);

	/* Evaluate the options based on supplied capabilities */
	pd_process_source_cap(port, pe[port].src_cap_cnt, pe[port].src_caps);

	/* Device Policy Response Received */
	set_state_pe(port, PE_SNK_SELECT_CAPABILITY);
}

/**
 * PE_SNK_Select_Capability State
 */
static void pe_snk_select_capability_entry(int port)
{
	print_current_state(port);

	/* Send Request */
	pe_send_request_msg(port);

	/* We are PD Connected */
	PE_SET_FLAG(port, PE_FLAGS_PD_CONNECTION);
	tc_pd_connection(port, 1);
	pe[port].sender_response_timer = TIMER_DISABLED;
}

static void pe_snk_select_capability_run(int port)
{
	uint8_t type;
	uint8_t cnt;
	enum tcpm_transmit_type sop;

	/* Wait until message is sent */
	if (pe[port].sender_response_timer == TIMER_DISABLED) {
		if (PE_CHK_FLAG(port, PE_FLAGS_TX_COMPLETE)) {
			PE_CLR_FLAG(port, PE_FLAGS_TX_COMPLETE);
			/* Initialize and run SenderResponseTimer */
			pe[port].sender_response_timer =
					get_time().val + PD_T_SENDER_RESPONSE;
		} else {
			return;
		}
	}

	if (PE_CHK_FLAG(port, PE_FLAGS_MSG_RECEIVED)) {
		PE_CLR_FLAG(port, PE_FLAGS_MSG_RECEIVED);
		type = PD_HEADER_TYPE(rx_emsg[port].header);
		cnt = PD_HEADER_CNT(rx_emsg[port].header);
		sop = PD_HEADER_GET_SOP(rx_emsg[port].header);

		/*
		 * Transition to the PE_SNK_Transition_Sink state when:
		 *  1) An Accept Message is received from the Source.
		 *
		 * Transition to the PE_SNK_Wait_for_Capabilities state when:
		 *  1) There is no Explicit Contract in place and
		 *  2) A Reject Message is received from the Source or
		 *  3) A Wait Message is received from the Source.
		 *
		 * Transition to the PE_SNK_Ready state when:
		 *  1) There is an Explicit Contract in place and
		 *  2) A Reject Message is received from the Source or
		 *  3) A Wait Message is received from the Source.
		 *
		 * Transition to the PE_SNK_Hard_Reset state when:
		 *  1) A SenderResponseTimer timeout occurs.
		 */

		/* Only look at control messages */
		if (cnt == 0) {
			/*
			 * Accept Message Received
			 */
			if (type == PD_CTRL_ACCEPT) {
				/* explicit contract is now in place */
				pe_set_explicit_contract(port);

				set_state_pe(port, PE_SNK_TRANSITION_SINK);

				/*
				 * Setup to get Device Policy Manager to
				 * request Sink Capabilities for possible FRS
				 */
				pe_dpm_request(port, DPM_REQUEST_GET_SNK_CAPS);
				return;
			}
			/*
			 * Reject or Wait Message Received
			 */
			else if (type == PD_CTRL_REJECT ||
							type == PD_CTRL_WAIT) {
				if (type == PD_CTRL_WAIT)
					PE_SET_FLAG(port, PE_FLAGS_WAIT);

				/*
				 * We had a previous explicit contract, so
				 * transition to PE_SNK_Ready
				 */
				if (PE_CHK_FLAG(port,
						PE_FLAGS_EXPLICIT_CONTRACT))
					set_state_pe(port, PE_SNK_READY);
				/*
				 * No previous explicit contract, so transition
				 * to PE_SNK_Wait_For_Capabilities
				 */
				else
					set_state_pe(port,
						PE_SNK_WAIT_FOR_CAPABILITIES);
				return;
			}
			/*
			 * Unexpected Control Message Received
			 */
			else {
				/* Send Soft Reset */
				pe_send_soft_reset(port, sop);
				return;
			}
		}
		/*
		 * Unexpected Data Message
		 */
		else {
			/* Send Soft Reset */
			pe_send_soft_reset(port, sop);
			return;
		}
	}

	/* SenderResponsetimer timeout */
	if (get_time().val > pe[port].sender_response_timer)
		set_state_pe(port, PE_SNK_HARD_RESET);
}

/**
 * PE_SNK_Transition_Sink State
 */
static void pe_snk_transition_sink_entry(int port)
{
	print_current_state(port);

	/* Initialize and run PSTransitionTimer */
	pe[port].ps_transition_timer = get_time().val + PD_T_PS_TRANSITION;
}

static void pe_snk_transition_sink_run(int port)
{
	/*
	 * Transition to the PE_SNK_Ready state when:
	 *  1) A PS_RDY Message is received from the Source.
	 *
	 * Transition to the PE_SNK_Hard_Reset state when:
	 *  1) A Protocol Error occurs.
	 */

	if (PE_CHK_FLAG(port, PE_FLAGS_MSG_RECEIVED)) {
		PE_CLR_FLAG(port, PE_FLAGS_MSG_RECEIVED);

		/*
		 * PS_RDY message received
		 */
		if ((PD_HEADER_CNT(rx_emsg[port].header) == 0) &&
			   (PD_HEADER_TYPE(rx_emsg[port].header) ==
			   PD_CTRL_PS_RDY)) {
			/*
			 * Set first message flag to trigger a wait and add
			 * jitter delay when operating in PD2.0 mode.
			 */
			PE_SET_FLAG(port, PE_FLAGS_FIRST_MSG);

			set_state_pe(port, PE_SNK_READY);
			return;
		}

		/*
		 * Protocol Error
		 */
		set_state_pe(port, PE_SNK_HARD_RESET);
	}

	/*
	 * Timeout will lead to a Hard Reset
	 */
	if (get_time().val > pe[port].ps_transition_timer &&
			pe[port].hard_reset_counter <= N_HARD_RESET_COUNT) {
		PE_SET_FLAG(port, PE_FLAGS_PS_TRANSITION_TIMEOUT);

		set_state_pe(port, PE_SNK_HARD_RESET);
	}
}

static void pe_snk_transition_sink_exit(int port)
{
	/* Transition Sink's power supply to the new power level */
	pd_set_input_current_limit(port,
				pe[port].curr_limit, pe[port].supply_voltage);

	if (IS_ENABLED(CONFIG_CHARGE_MANAGER))
		/* Set ceiling based on what's negotiated */
		charge_manager_set_ceil(port,
				CEIL_REQUESTOR_PD, pe[port].curr_limit);
}


/**
 * PE_SNK_Ready State
 */
static void pe_snk_ready_entry(int port)
{
	print_current_state(port);

	/* Ensure any message send flags are cleaned up */
	PE_CLR_FLAG(port, PE_FLAGS_READY_CLR);
	/*
	 * On entry to the PE_SNK_Ready state as the result of a wait,
	 * then do the following:
	 *   1) Initialize and run the SinkRequestTimer
	 */
	if (PE_CHK_FLAG(port, PE_FLAGS_WAIT)) {
		PE_CLR_FLAG(port, PE_FLAGS_WAIT);
		pe[port].sink_request_timer =
				get_time().val + PD_T_SINK_REQUEST;
	} else {
		pe[port].sink_request_timer = TIMER_DISABLED;
	}

	/*
	 * Wait and add jitter if we are operating in PD2.0 mode and no messages
	 * have been sent since enter this state.
	 */
	pe_update_wait_and_add_jitter_timer(port);
}

static void pe_snk_ready_run(int port)
{
	uint32_t payload;
	uint8_t type;
	uint8_t cnt;
	uint8_t ext;

	/*
	 * Don't delay handling a hard reset from the device policy manager.
	 */
	if (PE_CHK_DPM_REQUEST(port, DPM_REQUEST_HARD_RESET_SEND)) {
		PE_CLR_DPM_REQUEST(port, DPM_REQUEST_HARD_RESET_SEND);
		set_state_pe(port, PE_SNK_HARD_RESET);
		return;
	}

	/*
	 * Handle incoming messages before discovery and DPMs other than hard
	 * reset
	 */
	if (PE_CHK_FLAG(port, PE_FLAGS_MSG_RECEIVED)) {
		PE_CLR_FLAG(port, PE_FLAGS_MSG_RECEIVED);

		type = PD_HEADER_TYPE(rx_emsg[port].header);
		cnt = PD_HEADER_CNT(rx_emsg[port].header);
		ext = PD_HEADER_EXT(rx_emsg[port].header);
		payload = *(uint32_t *)rx_emsg[port].buf;

		/* Extended Message Request */
		if (ext > 0) {
			switch (type) {
#if defined(CONFIG_USB_PD_REV30) && defined(CONFIG_BATTERY)
			case PD_EXT_GET_BATTERY_CAP:
				set_state_pe(port, PE_GIVE_BATTERY_CAP);
				break;
			case PD_EXT_GET_BATTERY_STATUS:
				set_state_pe(port, PE_GIVE_BATTERY_STATUS);
				break;
#endif /* CONFIG_USB_PD_REV30 && CONFIG_BATTERY */
			default:
				set_state_pe(port, PE_SEND_NOT_SUPPORTED);
			}
			return;
		}
		/* Data Messages */
		else if (cnt > 0) {
			switch (type) {
			case PD_DATA_SOURCE_CAP:
				set_state_pe(port,
					PE_SNK_EVALUATE_CAPABILITY);
				break;
			case PD_DATA_VENDOR_DEF:
				if (PD_HEADER_TYPE(rx_emsg[port].header) ==
							PD_DATA_VENDOR_DEF) {
					if (PD_VDO_SVDM(payload))
						set_state_pe(port,
							PE_VDM_RESPONSE);
					else
						set_state_pe(port,
						PE_HANDLE_CUSTOM_VDM_REQUEST);
				}
				break;
			case PD_DATA_BIST:
				set_state_pe(port, PE_BIST_TX);
				break;
			default:
				set_state_pe(port, PE_SEND_NOT_SUPPORTED);
			}
			return;
		}
		/* Control Messages */
		else {
			switch (type) {
			case PD_CTRL_GOOD_CRC:
				/* Do nothing */
				break;
			case PD_CTRL_PING:
				/* Do nothing */
				break;
			case PD_CTRL_GET_SOURCE_CAP:
				set_state_pe(port, PE_SNK_GET_SOURCE_CAP);
				return;
			case PD_CTRL_GET_SINK_CAP:
				set_state_pe(port, PE_SNK_GIVE_SINK_CAP);
				return;
			case PD_CTRL_GOTO_MIN:
				set_state_pe(port, PE_SNK_TRANSITION_SINK);
				return;
			case PD_CTRL_PR_SWAP:
				set_state_pe(port,
						PE_PRS_SNK_SRC_EVALUATE_SWAP);
				return;
			case PD_CTRL_DR_SWAP:
				if (PE_CHK_FLAG(port, PE_FLAGS_MODAL_OPERATION))
					set_state_pe(port, PE_SNK_HARD_RESET);
				else
					set_state_pe(port,
							PE_DRS_EVALUATE_SWAP);
				return;
			case PD_CTRL_VCONN_SWAP:
				set_state_pe(port, PE_VCS_EVALUATE_SWAP);
				return;
			case PD_CTRL_NOT_SUPPORTED:
				/* Do nothing */
				break;
			default:
				set_state_pe(port, PE_SEND_NOT_SUPPORTED);
				return;
			}
		}
	} else if (PE_CHK_FLAG(port, PE_FLAGS_VDM_REQUEST_CONTINUE)) {
		PE_CLR_FLAG(port, PE_FLAGS_VDM_REQUEST_CONTINUE);
		set_state_pe(port, PE_VDM_REQUEST_DPM);
		return;
	}

	if (pe[port].wait_and_add_jitter_timer == TIMER_DISABLED ||
		get_time().val > pe[port].wait_and_add_jitter_timer) {
		PE_CLR_FLAG(port, PE_FLAGS_FIRST_MSG);
		pe[port].wait_and_add_jitter_timer = TIMER_DISABLED;

		if (get_time().val > pe[port].sink_request_timer) {
			set_state_pe(port, PE_SNK_SELECT_CAPABILITY);
			return;
		}

		/*
		 * Attempt discovery if possible, and return if state was
		 * changed for that discovery.
		 */
		if (pe_attempt_port_discovery(port))
			return;

		/*
		 * Handle Device Policy Manager Requests
		 */
		/*
		 * Ignore source specific requests:
		 *   DPM_REQUEST_GOTO_MIN
		 *   DPM_REQUEST_SRC_CAP_CHANGE,
		 *   DPM_REQUEST_SEND_PING
		 */
		PE_CLR_DPM_REQUEST(port, DPM_REQUEST_GOTO_MIN |
					DPM_REQUEST_SRC_CAP_CHANGE |
					DPM_REQUEST_SEND_PING);

		if (pe[port].dpm_request) {
			uint32_t dpm_request = pe[port].dpm_request;

			PE_SET_FLAG(port, PE_FLAGS_LOCALLY_INITIATED_AMS);

			if (PE_CHK_DPM_REQUEST(port, DPM_REQUEST_DR_SWAP)) {
				PE_CLR_DPM_REQUEST(port, DPM_REQUEST_DR_SWAP);
				if (PE_CHK_FLAG(port, PE_FLAGS_MODAL_OPERATION))
					set_state_pe(port, PE_SNK_HARD_RESET);
				else
					set_state_pe(port, PE_DRS_SEND_SWAP);
			} else if (PE_CHK_DPM_REQUEST(port,
							DPM_REQUEST_PR_SWAP)) {
				PE_CLR_DPM_REQUEST(port, DPM_REQUEST_PR_SWAP);
				set_state_pe(port, PE_PRS_SNK_SRC_SEND_SWAP);
			} else if (PE_CHK_DPM_REQUEST(port,
						DPM_REQUEST_SOURCE_CAP)) {
				PE_CLR_DPM_REQUEST(port,
							DPM_REQUEST_SOURCE_CAP);
				set_state_pe(port, PE_SNK_GET_SOURCE_CAP);
			} else if (PE_CHK_DPM_REQUEST(port,
					DPM_REQUEST_NEW_POWER_LEVEL)) {
				PE_CLR_DPM_REQUEST(port,
						DPM_REQUEST_NEW_POWER_LEVEL);
				set_state_pe(port, PE_SNK_SELECT_CAPABILITY);
			} else if (PE_CHK_DPM_REQUEST(port,
					      DPM_REQUEST_GET_SNK_CAPS)) {
				PE_CLR_DPM_REQUEST(port,
						DPM_REQUEST_GET_SNK_CAPS);
				set_state_pe(port, PE_DR_SNK_GET_SINK_CAP);
			} else if (!common_src_snk_dpm_requests(port)) {
				CPRINTF("Unhandled DPM Request %x received\n",
					dpm_request);
				PE_CLR_DPM_REQUEST(port, dpm_request);
				PE_CLR_FLAG(port,
					PE_FLAGS_LOCALLY_INITIATED_AMS);
			}

			return;
		}

		/* No DPM requests; attempt mode entry/exit if needed */
		dpm_run(port);

	}
}

/**
 * PE_SNK_Hard_Reset
 */
static void pe_snk_hard_reset_entry(int port)
{
	print_current_state(port);

	/*
	 * Note: If the SinkWaitCapTimer times out and the HardResetCounter is
	 *       greater than nHardResetCount the Sink Shall assume that the
	 *       Source is non-responsive.
	 */
	if (PE_CHK_FLAG(port, PE_FLAGS_SNK_WAIT_CAP_TIMEOUT) &&
			pe[port].hard_reset_counter > N_HARD_RESET_COUNT) {
		set_state_pe(port, PE_SRC_DISABLED);
	}

	PE_CLR_FLAG(port, PE_FLAGS_SNK_WAIT_CAP_TIMEOUT |
			  PE_FLAGS_VDM_REQUEST_NAKED |
			  PE_FLAGS_PROTOCOL_ERROR |
			  PE_FLAGS_VDM_REQUEST_BUSY);

	/* Request the generation of Hard Reset Signaling by the PHY Layer */
	pe_prl_execute_hard_reset(port);

	/* Increment the HardResetCounter */
	pe[port].hard_reset_counter++;

	/*
	 * Transition the Sink’s power supply to the new power level if
	 * PSTransistionTimer timeout occurred.
	 */
	if (PE_CHK_FLAG(port, PE_FLAGS_PS_TRANSITION_TIMEOUT)) {
		PE_CLR_FLAG(port, PE_FLAGS_PS_TRANSITION_TIMEOUT);

		/* Transition Sink's power supply to the new power level */
		pd_set_input_current_limit(port, pe[port].curr_limit,
						pe[port].supply_voltage);
		if (IS_ENABLED(CONFIG_CHARGE_MANAGER))
			/* Set ceiling based on what's negotiated */
			charge_manager_set_ceil(port, CEIL_REQUESTOR_PD,
							pe[port].curr_limit);
	}
}

static void pe_snk_hard_reset_run(int port)
{
	/*
	 * Transition to the PE_SNK_Transition_to_default state when:
	 *  1) The Hard Reset is complete.
	 */
	if (PE_CHK_FLAG(port, PE_FLAGS_HARD_RESET_PENDING))
		return;

	set_state_pe(port, PE_SNK_TRANSITION_TO_DEFAULT);
}

/**
 * PE_SNK_Transition_to_default
 */
static void pe_snk_transition_to_default_entry(int port)
{
	print_current_state(port);

	/* Inform the TC Layer of Hard Reset */
	tc_hard_reset_request(port);
}

static void pe_snk_transition_to_default_run(int port)
{
	if (PE_CHK_FLAG(port, PE_FLAGS_PS_RESET_COMPLETE)) {
		/* PE_SNK_Startup clears all flags */

		/* Inform the Protocol Layer that the Hard Reset is complete */
		prl_hard_reset_complete(port);
		set_state_pe(port, PE_SNK_STARTUP);
	}
}

/**
 * PE_SNK_Get_Source_Cap
 */
static void pe_snk_get_source_cap_entry(int port)
{
	print_current_state(port);

	/* Send a Get_Source_Cap Message */
	tx_emsg[port].len = 0;
	send_ctrl_msg(port, TCPC_TX_SOP, PD_CTRL_GET_SOURCE_CAP);
}

static void pe_snk_get_source_cap_run(int port)
{
	if (PE_CHK_FLAG(port, PE_FLAGS_TX_COMPLETE)) {
		PE_CLR_FLAG(port, PE_FLAGS_TX_COMPLETE);

		set_state_pe(port, PE_SNK_READY);
	}
}

/**
 * PE_SNK_Send_Soft_Reset and PE_SRC_Send_Soft_Reset
 */
static void pe_send_soft_reset_entry(int port)
{
	print_current_state(port);

	/* Reset Protocol Layer (softly) */
	prl_reset_soft(port);

	pe[port].sender_response_timer = TIMER_DISABLED;
}

static void pe_send_soft_reset_run(int port)
{
	int type;
	int cnt;
	int ext;

	/* Wait until protocol layer is running */
	if (!prl_is_running(port))
		return;

	if (pe[port].sender_response_timer == TIMER_DISABLED) {
		/*
		 * TODO(b/150614211): Soft reset type should match
		 * unexpected incoming message type
		 */
		/* Send Soft Reset message */
		send_ctrl_msg(port,
			pe[port].soft_reset_sop, PD_CTRL_SOFT_RESET);

		/* Initialize and run SenderResponseTimer */
		pe[port].sender_response_timer =
					get_time().val + PD_T_SENDER_RESPONSE;
	}

	/*
	 * Transition to the PE_SNK_Send_Capabilities or
	 * PE_SRC_Send_Capabilities state when:
	 *   1) An Accept Message has been received.
	 */
	if (PE_CHK_FLAG(port, PE_FLAGS_MSG_RECEIVED)) {
		PE_CLR_FLAG(port, PE_FLAGS_MSG_RECEIVED);

		type = PD_HEADER_TYPE(rx_emsg[port].header);
		cnt = PD_HEADER_CNT(rx_emsg[port].header);
		ext = PD_HEADER_EXT(rx_emsg[port].header);

		if ((ext == 0) && (cnt == 0) && (type == PD_CTRL_ACCEPT)) {
			if (pe[port].power_role == PD_ROLE_SINK)
				set_state_pe(port,
						PE_SNK_WAIT_FOR_CAPABILITIES);
			else
				set_state_pe(port,
						PE_SRC_SEND_CAPABILITIES);
			return;
		}
	}

	/*
	 * Transition to PE_SNK_Hard_Reset or PE_SRC_Hard_Reset on Sender
	 * Response Timer Timeout or Protocol Layer or Protocol Error
	 */
	if (get_time().val > pe[port].sender_response_timer ||
			PE_CHK_FLAG(port, PE_FLAGS_PROTOCOL_ERROR)) {
		PE_CLR_FLAG(port, PE_FLAGS_PROTOCOL_ERROR);

		if (pe[port].power_role == PD_ROLE_SINK)
			set_state_pe(port, PE_SNK_HARD_RESET);
		else
			set_state_pe(port, PE_SRC_HARD_RESET);
		return;
	}

}

/**
 * PE_SNK_Soft_Reset and PE_SNK_Soft_Reset
 */
static void pe_soft_reset_entry(int port)
{
	print_current_state(port);

	pe[port].sender_response_timer = TIMER_DISABLED;

	send_ctrl_msg(port, TCPC_TX_SOP, PD_CTRL_ACCEPT);
}

static void  pe_soft_reset_run(int port)
{
	if (PE_CHK_FLAG(port, PE_FLAGS_TX_COMPLETE)) {
		PE_CLR_FLAG(port, PE_FLAGS_TX_COMPLETE);

		if (pe[port].power_role == PD_ROLE_SINK)
			set_state_pe(port, PE_SNK_WAIT_FOR_CAPABILITIES);
		else
			set_state_pe(port, PE_SRC_SEND_CAPABILITIES);
	} else if (PE_CHK_FLAG(port, PE_FLAGS_PROTOCOL_ERROR)) {
		PE_CLR_FLAG(port, PE_FLAGS_PROTOCOL_ERROR);

		if (pe[port].power_role == PD_ROLE_SINK)
			set_state_pe(port, PE_SNK_HARD_RESET);
		else
			set_state_pe(port, PE_SRC_HARD_RESET);
	}
}

/**
 * PE_SRC_Not_Supported and PE_SNK_Not_Supported
 *
 * 6.7.1 Soft Reset and Protocol Error (Revision 2.0, Version 1.3)
 * An unrecognized or unsupported Message (except for a Structured VDM),
 * received in the PE_SNK_Ready or PE_SRC_Ready states, Shall Not cause
 * a Soft_Reset Message to be generated but instead a Reject Message
 * Shall be generated.
 */
static void pe_send_not_supported_entry(int port)
{
	print_current_state(port);

	/* Request the Protocol Layer to send a Not_Supported Message. */
	if (prl_get_rev(port, TCPC_TX_SOP) > PD_REV20)
		send_ctrl_msg(port, TCPC_TX_SOP, PD_CTRL_NOT_SUPPORTED);
	else
		send_ctrl_msg(port, TCPC_TX_SOP, PD_CTRL_REJECT);
}

static void pe_send_not_supported_run(int port)
{
	if (PE_CHK_FLAG(port, PE_FLAGS_TX_COMPLETE)) {
		PE_CLR_FLAG(port, PE_FLAGS_TX_COMPLETE);
		pe_set_ready_state(port);

	}
}

/**
 * PE_SRC_Ping
 */
static void pe_src_ping_entry(int port)
{
	print_current_state(port);
	send_ctrl_msg(port, TCPC_TX_SOP, PD_CTRL_PING);
}

static void pe_src_ping_run(int port)
{
	if (PE_CHK_FLAG(port, PE_FLAGS_TX_COMPLETE)) {
		PE_CLR_FLAG(port, PE_FLAGS_TX_COMPLETE);
		set_state_pe(port, PE_SRC_READY);
	}
}

#ifdef CONFIG_USB_PD_REV30
/**
 * PE_Give_Battery_Cap
 */
static void pe_give_battery_cap_entry(int port)
{
	uint32_t payload = *(uint32_t *)(&rx_emsg[port].buf);
	uint16_t *msg = (uint16_t *)tx_emsg[port].buf;

	if (!IS_ENABLED(CONFIG_BATTERY))
		return;
	print_current_state(port);

	/* msg[0] - extended header is set by Protocol Layer */

	/* Set VID */
	msg[1] = USB_VID_GOOGLE;

	/* Set PID */
	msg[2] = CONFIG_USB_PID;

	if (battery_is_present()) {
		/*
		 * We only have one fixed battery,
		 * so make sure batt cap ref is 0.
		 */
		if (BATT_CAP_REF(payload) != 0) {
			/* Invalid battery reference */
			msg[3] = 0;
			msg[4] = 0;
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

	/* Extended Battery Cap data is 9 bytes */
	tx_emsg[port].len = 9;

	send_ext_data_msg(port, TCPC_TX_SOP, PD_EXT_BATTERY_CAP);
}

static void pe_give_battery_cap_run(int port)
{
	if (PE_CHK_FLAG(port, PE_FLAGS_TX_COMPLETE)) {
		PE_CLR_FLAG(port, PE_FLAGS_TX_COMPLETE);
		pe_set_ready_state(port);
	}
}

/**
 * PE_Give_Battery_Status
 */
static void pe_give_battery_status_entry(int port)
{
	uint32_t payload = *(uint32_t *)(&rx_emsg[port].buf);
	uint32_t *msg = (uint32_t *)tx_emsg[port].buf;

	if (!IS_ENABLED(CONFIG_BATTERY))
		return;
	print_current_state(port);

	if (battery_is_present()) {
		/*
		 * We only have one fixed battery,
		 * so make sure batt cap ref is 0.
		 */
		if (BATT_CAP_REF(payload) != 0) {
			/* Invalid battery reference */
			*msg |= BSDO_INVALID;
		} else {
			uint32_t v;
			uint32_t c;

			if (battery_design_voltage(&v) != 0 ||
					battery_remaining_capacity(&c) != 0) {
				*msg |= BSDO_CAP(BSDO_CAP_UNKNOWN);
			} else {
				/*
				 * Wh = (c * v) / 1000000
				 * 10th of a Wh = Wh * 10
				 */
				*msg |= BSDO_CAP(DIV_ROUND_NEAREST((c * v),
								100000));
			}

			/* Battery is present */
			*msg |= BSDO_PRESENT;

			/*
			 * For drivers that are not smart battery compliant,
			 * battery_status() returns EC_ERROR_UNIMPLEMENTED and
			 * the battery is assumed to be idle.
			 */
			if (battery_status(&c) != 0) {
				*msg |= BSDO_IDLE; /* assume idle */
			} else {
				if (c & STATUS_FULLY_CHARGED)
					/* Fully charged */
					*msg |= BSDO_IDLE;
				else if (c & STATUS_DISCHARGING)
					/* Discharging */
					*msg |= BSDO_DISCHARGING;
				/* else battery is charging.*/
			}
		}
	} else {
		*msg = BSDO_CAP(BSDO_CAP_UNKNOWN);
	}

	/* Battery Status data is 4 bytes */
	tx_emsg[port].len = 4;

	send_data_msg(port, TCPC_TX_SOP, PD_DATA_BATTERY_STATUS);
}

static void pe_give_battery_status_run(int port)
{
	if (PE_CHK_FLAG(port, PE_FLAGS_TX_COMPLETE)) {
		PE_CLR_FLAG(port, PE_FLAGS_TX_COMPLETE);
		set_state_pe(port, PE_SRC_READY);
	}
}

/**
 * PE_SRC_Send_Source_Alert and
 * PE_SNK_Send_Sink_Alert
 */
static void pe_send_alert_entry(int port)
{
	uint32_t *msg = (uint32_t *)tx_emsg[port].buf;
	uint32_t *len = &tx_emsg[port].len;

	print_current_state(port);

	if (pd_build_alert_msg(msg, len, pe[port].power_role) != EC_SUCCESS)
		pe_set_ready_state(port);

	/* Request the Protocol Layer to send Alert Message. */
	send_data_msg(port, TCPC_TX_SOP, PD_DATA_ALERT);
}

static void pe_send_alert_run(int port)
{
	if (PE_CHK_FLAG(port, PE_FLAGS_TX_COMPLETE)) {
		PE_CLR_FLAG(port, PE_FLAGS_TX_COMPLETE);
		pe_set_ready_state(port);
	}
}
#endif /* CONFIG_USB_PD_REV30 */

/**
 * PE_DRS_Evaluate_Swap
 */
static void pe_drs_evaluate_swap_entry(int port)
{
	print_current_state(port);

	/* Get evaluation of Data Role Swap request from DPM */
	if (pd_check_data_swap(port, pe[port].data_role)) {
		PE_SET_FLAG(port, PE_FLAGS_ACCEPT);
		/*
		 * PE_DRS_UFP_DFP_Evaluate_Swap and
		 * PE_DRS_DFP_UFP_Evaluate_Swap states embedded here.
		 */
		send_ctrl_msg(port, TCPC_TX_SOP, PD_CTRL_ACCEPT);
	} else {
		/*
		 * PE_DRS_UFP_DFP_Reject_Swap and PE_DRS_DFP_UFP_Reject_Swap
		 * states embedded here.
		 */
		send_ctrl_msg(port, TCPC_TX_SOP, PD_CTRL_REJECT);
	}
}

static void pe_drs_evaluate_swap_run(int port)
{
	if (PE_CHK_FLAG(port, PE_FLAGS_TX_COMPLETE)) {
		PE_CLR_FLAG(port, PE_FLAGS_TX_COMPLETE);

		/* Accept Message sent. Transtion to PE_DRS_Change */
		if (PE_CHK_FLAG(port, PE_FLAGS_ACCEPT)) {
			PE_CLR_FLAG(port, PE_FLAGS_ACCEPT);
			set_state_pe(port, PE_DRS_CHANGE);
		} else {
			/*
			 * Message sent. Transition back to PE_SRC_Ready or
			 * PE_SNK_Ready.
			 */
			pe_set_ready_state(port);
		}
	}
}

/**
 * PE_DRS_Change
 */
static void pe_drs_change_entry(int port)
{
	print_current_state(port);

	/*
	 * PE_DRS_UFP_DFP_Change_to_DFP and PE_DRS_DFP_UFP_Change_to_UFP
	 * states embedded here.
	 */
	/* Request DPM to change port data role */
	pd_request_data_swap(port);
}

static void pe_drs_change_run(int port)
{
	/* Wait until the data role is changed */
	if (pe[port].data_role == pd_get_data_role(port))
		return;

	/* Update the data role */
	pe[port].data_role = pd_get_data_role(port);

	if (pe[port].data_role == PD_ROLE_DFP)
		PE_CLR_FLAG(port, PE_FLAGS_DR_SWAP_TO_DFP);

	/*
	 * Port changed. Transition back to PE_SRC_Ready or
	 * PE_SNK_Ready.
	 */
	pe_set_ready_state(port);
}

/**
 * PE_DRS_Send_Swap
 */
static void pe_drs_send_swap_entry(int port)
{
	print_current_state(port);

	/*
	 * PE_DRS_UFP_DFP_Send_Swap and PE_DRS_DFP_UFP_Send_Swap
	 * states embedded here.
	 */
	/* Request the Protocol Layer to send a DR_Swap Message */
	send_ctrl_msg(port, TCPC_TX_SOP, PD_CTRL_DR_SWAP);
}

static void pe_drs_send_swap_run(int port)
{
	int type;
	int cnt;
	int ext;

	/*
	 * Transition to PE_DRS_Change when:
	 *   1) An Accept Message is received.
	 *
	 * Transition to PE_SRC_Ready or PE_SNK_Ready state when:
	 *   1) A Reject Message is received.
	 *   2) Or a Wait Message is received.
	 */
	if (PE_CHK_FLAG(port, PE_FLAGS_MSG_RECEIVED)) {
		PE_CLR_FLAG(port, PE_FLAGS_MSG_RECEIVED);

		type = PD_HEADER_TYPE(rx_emsg[port].header);
		cnt = PD_HEADER_CNT(rx_emsg[port].header);
		ext = PD_HEADER_EXT(rx_emsg[port].header);

		if ((ext == 0) && (cnt == 0)) {
			if (type == PD_CTRL_ACCEPT) {
				set_state_pe(port, PE_DRS_CHANGE);
				return;
			} else if ((type == PD_CTRL_REJECT) ||
					(type == PD_CTRL_WAIT) ||
					(type == PD_CTRL_NOT_SUPPORTED)) {
				if (type == PD_CTRL_WAIT)
					PE_SET_FLAG(port,
						PE_FLAGS_WAITING_DR_SWAP);

				pe_set_ready_state(port);
				return;
			}
		}
	}

	/*
	 * Transition to PE_SRC_Ready or PE_SNK_Ready state when:
	 *   1) the SenderResponseTimer times out.
	 */
	if (get_time().val > pe[port].sender_response_timer) {
		pe_set_ready_state(port);
		return;
	}
}

/**
 * PE_PRS_SRC_SNK_Evaluate_Swap
 */
static void pe_prs_src_snk_evaluate_swap_entry(int port)
{
	print_current_state(port);

	if (!pd_check_power_swap(port)) {
		/* PE_PRS_SRC_SNK_Reject_PR_Swap state embedded here */
		send_ctrl_msg(port, TCPC_TX_SOP, PD_CTRL_REJECT);
	} else {
		tc_request_power_swap(port);
		/* PE_PRS_SRC_SNK_Accept_Swap state embedded here */
		PE_SET_FLAG(port, PE_FLAGS_ACCEPT);
		send_ctrl_msg(port, TCPC_TX_SOP, PD_CTRL_ACCEPT);
	}
}

static void pe_prs_src_snk_evaluate_swap_run(int port)
{
	if (PE_CHK_FLAG(port, PE_FLAGS_TX_COMPLETE)) {
		PE_CLR_FLAG(port, PE_FLAGS_TX_COMPLETE);

		if (PE_CHK_FLAG(port, PE_FLAGS_ACCEPT)) {
			PE_CLR_FLAG(port, PE_FLAGS_ACCEPT);

			/*
			 * Power Role Swap OK, transition to
			 * PE_PRS_SRC_SNK_Transition_to_off
			 */
			set_state_pe(port, PE_PRS_SRC_SNK_TRANSITION_TO_OFF);
		} else {
			/* Message sent, return to PE_SRC_Ready */
			set_state_pe(port, PE_SRC_READY);
		}
	}
}

/**
 * PE_PRS_SRC_SNK_Transition_To_Off
 */
static void pe_prs_src_snk_transition_to_off_entry(int port)
{
	print_current_state(port);

	/* Contract is invalid */
	pe_invalidate_explicit_contract(port);

	/* Tell TypeC to power off the source */
	tc_src_power_off(port);

	pe[port].ps_source_timer =
			get_time().val + PD_POWER_SUPPLY_TURN_OFF_DELAY;
}

static void pe_prs_src_snk_transition_to_off_run(int port)
{
	/* Give time for supply to power off */
	if (get_time().val > pe[port].ps_source_timer &&
	    pd_check_vbus_level(port, VBUS_SAFE0V))
		set_state_pe(port, PE_PRS_SRC_SNK_ASSERT_RD);
}

/**
 * PE_PRS_SRC_SNK_Assert_Rd
 */
static void pe_prs_src_snk_assert_rd_entry(int port)
{
	print_current_state(port);

	/* Tell TypeC to swap from Attached.SRC to Attached.SNK */
	tc_prs_src_snk_assert_rd(port);
}

static void pe_prs_src_snk_assert_rd_run(int port)
{
	/* Wait until Rd is asserted */
	if (tc_is_attached_snk(port))
		set_state_pe(port, PE_PRS_SRC_SNK_WAIT_SOURCE_ON);
}

/**
 * PE_PRS_SRC_SNK_Wait_Source_On
 */
static void pe_prs_src_snk_wait_source_on_entry(int port)
{
	print_current_state(port);
	send_ctrl_msg(port, TCPC_TX_SOP, PD_CTRL_PS_RDY);
	pe[port].ps_source_timer = TIMER_DISABLED;
}

static void pe_prs_src_snk_wait_source_on_run(int port)
{
	int type;
	int cnt;
	int ext;

	if (pe[port].ps_source_timer == TIMER_DISABLED &&
			PE_CHK_FLAG(port, PE_FLAGS_TX_COMPLETE)) {
		PE_CLR_FLAG(port, PE_FLAGS_TX_COMPLETE);

		/* Update pe power role */
		pe[port].power_role = pd_get_power_role(port);
		pe[port].ps_source_timer = get_time().val + PD_T_PS_SOURCE_ON;
	}

	/*
	 * Transition to PE_SNK_Startup when:
	 *   1) A PS_RDY Message is received.
	 */
	if (pe[port].ps_source_timer != TIMER_DISABLED &&
			PE_CHK_FLAG(port, PE_FLAGS_MSG_RECEIVED)) {
		PE_CLR_FLAG(port, PE_FLAGS_MSG_RECEIVED);

		type = PD_HEADER_TYPE(rx_emsg[port].header);
		cnt = PD_HEADER_CNT(rx_emsg[port].header);
		ext = PD_HEADER_EXT(rx_emsg[port].header);

		if ((ext == 0) && (cnt == 0) && (type == PD_CTRL_PS_RDY)) {
			pe[port].ps_source_timer = TIMER_DISABLED;

			PE_SET_FLAG(port, PE_FLAGS_PR_SWAP_COMPLETE);
			set_state_pe(port, PE_SNK_STARTUP);
			return;
		}
	}

	/*
	 * Transition to ErrorRecovery state when:
	 *   1) The PSSourceOnTimer times out.
	 *   2) PS_RDY not sent after retries.
	 */
	if (get_time().val > pe[port].ps_source_timer ||
	    PE_CHK_FLAG(port, PE_FLAGS_PROTOCOL_ERROR)) {
		PE_CLR_FLAG(port, PE_FLAGS_PROTOCOL_ERROR);

		set_state_pe(port, PE_WAIT_FOR_ERROR_RECOVERY);
		return;
	}
}

static void pe_prs_src_snk_wait_source_on_exit(int port)
{
	tc_pr_swap_complete(port,
			    PE_CHK_FLAG(port, PE_FLAGS_PR_SWAP_COMPLETE));
}

/**
 * PE_PRS_SRC_SNK_Send_Swap
 */
static void pe_prs_src_snk_send_swap_entry(int port)
{
	print_current_state(port);

	/* Request the Protocol Layer to send a PR_Swap Message. */
	send_ctrl_msg(port, TCPC_TX_SOP, PD_CTRL_PR_SWAP);
}

static void pe_prs_src_snk_send_swap_run(int port)
{
	int type;
	int cnt;
	int ext;

	/*
	 * Transition to PE_SRC_Ready state when:
	 *   1) Or the SenderResponseTimer times out.
	 */
	if (get_time().val > pe[port].sender_response_timer) {
		set_state_pe(port, PE_SRC_READY);
		return;
	}

	/*
	 * Transition to PE_PRS_SRC_SNK_Transition_To_Off when:
	 *   1) An Accept Message is received.
	 *
	 * Transition to PE_SRC_Ready state when:
	 *   1) A Reject Message is received.
	 *   2) Or a Wait Message is received.
	 */
	if (PE_CHK_FLAG(port, PE_FLAGS_MSG_RECEIVED)) {
		PE_CLR_FLAG(port, PE_FLAGS_MSG_RECEIVED);

		type = PD_HEADER_TYPE(rx_emsg[port].header);
		cnt = PD_HEADER_CNT(rx_emsg[port].header);
		ext = PD_HEADER_EXT(rx_emsg[port].header);

		if ((ext == 0) && (cnt == 0)) {
			if (type == PD_CTRL_ACCEPT) {
				tc_request_power_swap(port);
				set_state_pe(port,
					PE_PRS_SRC_SNK_TRANSITION_TO_OFF);
			} else if ((type == PD_CTRL_REJECT) ||
						(type == PD_CTRL_WAIT))
				set_state_pe(port, PE_SRC_READY);
		}
	}
}

/**
 * PE_PRS_SNK_SRC_Evaluate_Swap
 */
static void pe_prs_snk_src_evaluate_swap_entry(int port)
{
	print_current_state(port);

	if (!pd_check_power_swap(port)) {
		/* PE_PRS_SNK_SRC_Reject_Swap state embedded here */
		send_ctrl_msg(port, TCPC_TX_SOP, PD_CTRL_REJECT);
	} else {
		tc_request_power_swap(port);
		/* PE_PRS_SNK_SRC_Accept_Swap state embedded here */
		PE_SET_FLAG(port, PE_FLAGS_ACCEPT);
		send_ctrl_msg(port, TCPC_TX_SOP, PD_CTRL_ACCEPT);
	}
}

static void pe_prs_snk_src_evaluate_swap_run(int port)
{
	if (PE_CHK_FLAG(port, PE_FLAGS_TX_COMPLETE)) {
		PE_CLR_FLAG(port, PE_FLAGS_TX_COMPLETE);
		if (PE_CHK_FLAG(port, PE_FLAGS_ACCEPT)) {
			PE_CLR_FLAG(port, PE_FLAGS_ACCEPT);

			/*
			 * Accept message sent, transition to
			 * PE_PRS_SNK_SRC_Transition_to_off
			 */
			set_state_pe(port, PE_PRS_SNK_SRC_TRANSITION_TO_OFF);
		} else {
			/* Message sent, return to PE_SNK_Ready */
			set_state_pe(port, PE_SNK_READY);
		}
	}
}

/**
 * PE_PRS_SNK_SRC_Transition_To_Off
 * PE_FRS_SNK_SRC_Transition_To_Off
 *
 * NOTE: Shared action code used for Power Role Swap and Fast Role Swap
 */
static void pe_prs_snk_src_transition_to_off_entry(int port)
{
	print_current_state(port);

	if (!IS_ENABLED(CONFIG_USB_PD_REV30) ||
			!PE_CHK_FLAG(port, PE_FLAGS_FAST_ROLE_SWAP_PATH))
		tc_snk_power_off(port);

	pe[port].ps_source_timer = get_time().val + PD_T_PS_SOURCE_OFF;
}

static void pe_prs_snk_src_transition_to_off_run(int port)
{
	int type;
	int cnt;
	int ext;

	/*
	 * Transition to ErrorRecovery state when:
	 *   1) The PSSourceOffTimer times out.
	 */
	if (get_time().val > pe[port].ps_source_timer)
		set_state_pe(port, PE_WAIT_FOR_ERROR_RECOVERY);

	/*
	 * Transition to PE_PRS_SNK_SRC_Assert_Rp when:
	 *   1) An PS_RDY Message is received.
	 */
	else if (PE_CHK_FLAG(port, PE_FLAGS_MSG_RECEIVED)) {
		PE_CLR_FLAG(port, PE_FLAGS_MSG_RECEIVED);

		type = PD_HEADER_TYPE(rx_emsg[port].header);
		cnt = PD_HEADER_CNT(rx_emsg[port].header);
		ext = PD_HEADER_EXT(rx_emsg[port].header);

		if ((ext == 0) && (cnt == 0) && (type == PD_CTRL_PS_RDY)) {
			/*
			 * FRS: We are always ready to drive vSafe5v, so just
			 * skip PE_FRS_SNK_SRC_Vbus_Applied and go direct to
			 * PE_FRS_SNK_SRC_Assert_Rp
			 */
			set_state_pe(port, PE_PRS_SNK_SRC_ASSERT_RP);
		}
	}
}

/**
 * PE_PRS_SNK_SRC_Assert_Rp
 * PE_FRS_SNK_SRC_Assert_Rp
 *
 * NOTE: Shared action code used for Power Role Swap and Fast Role Swap
 */
static void pe_prs_snk_src_assert_rp_entry(int port)
{
	print_current_state(port);

	/*
	 * Tell TypeC to Power/Fast Role Swap (PRS/FRS) from
	 * Attached.SNK to Attached.SRC
	 */
	tc_prs_snk_src_assert_rp(port);
}

static void pe_prs_snk_src_assert_rp_run(int port)
{
	/* Wait until TypeC is in the Attached.SRC state */
	if (tc_is_attached_src(port)) {
		if (!IS_ENABLED(CONFIG_USB_PD_REV30) ||
			!PE_CHK_FLAG(port, PE_FLAGS_FAST_ROLE_SWAP_PATH)) {
			/* Contract is invalid now */
			pe_invalidate_explicit_contract(port);
		}
		set_state_pe(port, PE_PRS_SNK_SRC_SOURCE_ON);
	}
}

/**
 * PE_PRS_SNK_SRC_Source_On
 * PE_FRS_SNK_SRC_Source_On
 *
 * NOTE: Shared action code used for Power Role Swap and Fast Role Swap
 */
static void pe_prs_snk_src_source_on_entry(int port)
{
	print_current_state(port);

	/*
	 * VBUS was enabled when the TypeC state machine entered
	 * Attached.SRC state
	 */
	pe[port].ps_source_timer = get_time().val +
					PD_POWER_SUPPLY_TURN_ON_DELAY;
}

static void pe_prs_snk_src_source_on_run(int port)
{
	/* Wait until power supply turns on */
	if (pe[port].ps_source_timer != TIMER_DISABLED) {
		if (get_time().val < pe[port].ps_source_timer)
			return;

		/* update pe power role */
		pe[port].power_role = pd_get_power_role(port);
		send_ctrl_msg(port, TCPC_TX_SOP, PD_CTRL_PS_RDY);
		/* reset timer so PD_CTRL_PS_RDY isn't sent again */
		pe[port].ps_source_timer = TIMER_DISABLED;
	}

	/*
	 * Transition to ErrorRecovery state when:
	 *   1) On protocol error
	 */
	else if (PE_CHK_FLAG(port, PE_FLAGS_PROTOCOL_ERROR)) {
		PE_CLR_FLAG(port, PE_FLAGS_PROTOCOL_ERROR);
		set_state_pe(port, PE_WAIT_FOR_ERROR_RECOVERY);
	}

	else if (PE_CHK_FLAG(port, PE_FLAGS_TX_COMPLETE)) {
		PE_CLR_FLAG(port, PE_FLAGS_TX_COMPLETE);

		/* Run swap source timer on entry to pe_src_startup */
		PE_SET_FLAG(port, PE_FLAGS_PR_SWAP_COMPLETE);
		set_state_pe(port, PE_SRC_STARTUP);
	}
}

static void pe_prs_snk_src_source_on_exit(int port)
{
	tc_pr_swap_complete(port,
			    PE_CHK_FLAG(port, PE_FLAGS_PR_SWAP_COMPLETE));
}

/**
 * PE_PRS_SNK_SRC_Send_Swap
 * PE_FRS_SNK_SRC_Send_Swap
 *
 * NOTE: Shared action code used for Power Role Swap and Fast Role Swap
 */
static void pe_prs_snk_src_send_swap_entry(int port)
{
	print_current_state(port);

	/*
	 * PRS_SNK_SRC_SEND_SWAP
	 *     Request the Protocol Layer to send a PR_Swap Message.
	 *
	 * FRS_SNK_SRC_SEND_SWAP
	 *     Hardware should have turned off sink power and started
	 *     bringing Vbus to vSafe5.
	 *     Request the Protocol Layer to send a FR_Swap Message.
	 */
	if (IS_ENABLED(CONFIG_USB_PD_REV30)) {
		send_ctrl_msg(port,
			TCPC_TX_SOP,
			PE_CHK_FLAG(port, PE_FLAGS_FAST_ROLE_SWAP_PATH)
				? PD_CTRL_FR_SWAP
				: PD_CTRL_PR_SWAP);
	} else {
		send_ctrl_msg(port, TCPC_TX_SOP, PD_CTRL_PR_SWAP);
	}

	/* Start the SenderResponseTimer */
	pe[port].sender_response_timer =
		get_time().val + PD_T_SENDER_RESPONSE;
}

static void pe_prs_snk_src_send_swap_run(int port)
{
	int type;
	int cnt;
	int ext;

	/*
	 * PRS: Transition to PE_SNK_Ready state when:
	 * FRS: Transition to ErrorRecovery state when:
	 *   1) The SenderResponseTimer times out.
	 */
	if (get_time().val > pe[port].sender_response_timer)
		if (IS_ENABLED(CONFIG_USB_PD_REV30))
			set_state_pe(port,
				PE_CHK_FLAG(port, PE_FLAGS_FAST_ROLE_SWAP_PATH)
				? PE_WAIT_FOR_ERROR_RECOVERY
				: PE_SNK_READY);
		else
			set_state_pe(port, PE_SNK_READY);

	/*
	 * Transition to PE_PRS_SNK_SRC_Transition_to_off when:
	 *   1) An Accept Message is received.
	 *
	 * PRS: Transition to PE_SNK_Ready state when:
	 * FRS: Transition to ErrorRecovery state when:
	 *   1) A Reject Message is received.
	 *   2) Or a Wait Message is received.
	 */
	else if (PE_CHK_FLAG(port, PE_FLAGS_MSG_RECEIVED)) {
		PE_CLR_FLAG(port, PE_FLAGS_MSG_RECEIVED);

		type = PD_HEADER_TYPE(rx_emsg[port].header);
		cnt = PD_HEADER_CNT(rx_emsg[port].header);
		ext = PD_HEADER_EXT(rx_emsg[port].header);

		if ((ext == 0) && (cnt == 0)) {
			if (type == PD_CTRL_ACCEPT) {
				tc_request_power_swap(port);
				set_state_pe(port,
					     PE_PRS_SNK_SRC_TRANSITION_TO_OFF);
			} else if ((type == PD_CTRL_REJECT) ||
						(type == PD_CTRL_WAIT)) {
				if (IS_ENABLED(CONFIG_USB_PD_REV30))
					set_state_pe(port,
						PE_CHK_FLAG(port,
						PE_FLAGS_FAST_ROLE_SWAP_PATH)
					   ? PE_WAIT_FOR_ERROR_RECOVERY
					   : PE_SNK_READY);
				else
					set_state_pe(port, PE_SNK_READY);
			}
		}
	}
}

#ifdef CONFIG_USB_PD_REV30
/**
 * PE_FRS_SNK_SRC_Start_AMS
 */
static void pe_frs_snk_src_start_ams_entry(int port)
{
	print_current_state(port);

	/* Contract is invalid now */
	pe_invalidate_explicit_contract(port);

	/* Inform Protocol Layer this is start of AMS */
	PE_SET_FLAG(port, PE_FLAGS_LOCALLY_INITIATED_AMS);

	/* Shared PRS/FRS code, indicate FRS path */
	PE_SET_FLAG(port, PE_FLAGS_FAST_ROLE_SWAP_PATH);
	set_state_pe(port, PE_PRS_SNK_SRC_SEND_SWAP);
}

/**
 * PE_PRS_FRS_SHARED
 */
static void pe_prs_frs_shared_entry(int port)
{
	/*
	 * Shared PRS/FRS code, assume PRS path
	 *
	 * This is the super state entry. It will be called before
	 * the first entry state to get into the PRS/FRS path.
	 * For FRS, PE_FRS_SNK_SRC_START_AMS entry will be called
	 * after this and that will set for the FRS path.
	 */
	PE_CLR_FLAG(port, PE_FLAGS_FAST_ROLE_SWAP_PATH);
}

static void pe_prs_frs_shared_exit(int port)
{
	/*
	 * Shared PRS/FRS code, when not in shared path
	 * indicate PRS path
	 */
	PE_CLR_FLAG(port, PE_FLAGS_FAST_ROLE_SWAP_PATH);
}
#endif /* CONFIG_USB_PD_REV30 */

/**
 * BIST TX
 */
static void pe_bist_tx_entry(int port)
{
	uint32_t *payload = (uint32_t *)rx_emsg[port].buf;
	uint8_t mode = BIST_MODE(payload[0]);

	print_current_state(port);

	/*
	 * See section 6.4.3.6 BIST Carrier Mode 2:
	 * With a BIST Carrier Mode 2 BIST Data Object, the UUT Shall send out
	 * a continuous string of alternating "1"s and “0”s.
	 * The UUT Shall exit the Continuous BIST Mode within tBISTContMode of
	 * this Continuous BIST Mode being enabled.
	 */
	if (mode == BIST_CARRIER_MODE_2) {
		send_ctrl_msg(port, TCPC_TX_BIST_MODE_2, 0);
		pe[port].bist_cont_mode_timer =
					get_time().val + PD_T_BIST_CONT_MODE;
	}
	/*
	 * See section 6.4.3.9 BIST Test Data:
	 * With a BIST Test Data BIST Data Object, the UUT Shall return a
	 * GoodCRC Message and Shall enter a test mode in which it sends no
	 * further Messages except for GoodCRC Messages in response to received
	 * Messages.
	 */
	else if (mode == BIST_TEST_DATA)
		pe[port].bist_cont_mode_timer = TIMER_DISABLED;
}

static void pe_bist_tx_run(int port)
{
	if (get_time().val > pe[port].bist_cont_mode_timer) {

		if (pe[port].power_role == PD_ROLE_SOURCE)
			set_state_pe(port, PE_SRC_TRANSITION_TO_DEFAULT);
		else
			set_state_pe(port, PE_SNK_TRANSITION_TO_DEFAULT);
	} else {
		/*
		 * We are in test data mode and no further Messages except for
		 * GoodCRC Messages in response to received Messages will
		 * be sent.
		 */
		if (PE_CHK_FLAG(port, PE_FLAGS_MSG_RECEIVED))
			PE_CLR_FLAG(port, PE_FLAGS_MSG_RECEIVED);
	}
}

/**
 * BIST RX
 */
static void pe_bist_rx_entry(int port)
{
	/* currently only support bist carrier 2 */
	uint32_t bdo = BDO(BDO_MODE_CARRIER2, 0);

	print_current_state(port);

	tx_emsg[port].len = sizeof(bdo);
	memcpy(tx_emsg[port].buf, (uint8_t *)&bdo, tx_emsg[port].len);
	send_data_msg(port, TCPC_TX_SOP, PD_DATA_BIST);

	/* Delay at least enough for partner to finish BIST */
	pe[port].bist_cont_mode_timer =
				get_time().val + PD_T_BIST_RECEIVE;
}

static void pe_bist_rx_run(int port)
{
	if (get_time().val < pe[port].bist_cont_mode_timer)
		return;

	if (pe[port].power_role == PD_ROLE_SOURCE)
		set_state_pe(port, PE_SRC_TRANSITION_TO_DEFAULT);
	else
		set_state_pe(port, PE_SNK_TRANSITION_TO_DEFAULT);
}

/**
 * Give_Sink_Cap Message
 */
static void pe_snk_give_sink_cap_entry(int port)
{
	print_current_state(port);

	/* Send a Sink_Capabilities Message */
	tx_emsg[port].len = pd_snk_pdo_cnt * 4;
	memcpy(tx_emsg[port].buf, (uint8_t *)pd_snk_pdo, tx_emsg[port].len);
	send_data_msg(port, TCPC_TX_SOP, PD_DATA_SINK_CAP);
}

static void pe_snk_give_sink_cap_run(int port)
{
	if (PE_CHK_FLAG(port, PE_FLAGS_TX_COMPLETE)) {
		PE_CLR_FLAG(port, PE_FLAGS_TX_COMPLETE);
		pe_set_ready_state(port);
	}
}

/**
 * Wait For Error Recovery
 */
static void pe_wait_for_error_recovery_entry(int port)
{
	print_current_state(port);
	tc_start_error_recovery(port);
}

static void pe_wait_for_error_recovery_run(int port)
{
	/* Stay here until error recovery is complete */
}

/**
 * PE_Handle_Custom_Vdm_Request
 */
static void pe_handle_custom_vdm_request_entry(int port)
{
	/* Get the message */
	uint32_t *payload = (uint32_t *)rx_emsg[port].buf;
	int cnt = PD_HEADER_CNT(rx_emsg[port].header);
	int sop = PD_HEADER_GET_SOP(rx_emsg[port].header);
	int rlen = 0;
	uint32_t *rdata;

	print_current_state(port);

	/* This is an Interruptible AMS */
	PE_SET_FLAG(port, PE_FLAGS_INTERRUPTIBLE_AMS);

	rlen = pd_custom_vdm(port, cnt, payload, &rdata);
	if (rlen > 0) {
		tx_emsg[port].len = rlen * 4;
		memcpy(tx_emsg[port].buf, (uint8_t *)rdata, tx_emsg[port].len);
		send_data_msg(port, sop, PD_DATA_VENDOR_DEF);
	}
}

static void pe_handle_custom_vdm_request_run(int port)
{
	/* Wait for ACCEPT, WAIT or Reject message to send. */
	if (PE_CHK_FLAG(port, PE_FLAGS_TX_COMPLETE)) {
		PE_CLR_FLAG(port, PE_FLAGS_TX_COMPLETE);

		/*
		 * Message sent. Transition back to
		 * PE_SRC_Ready or PE_SINK_Ready
		 */
		pe_set_ready_state(port);
	}
}

static void pe_handle_custom_vdm_request_exit(int port)
{
	PE_CLR_FLAG(port, PE_FLAGS_INTERRUPTIBLE_AMS);
}

static enum vdm_response_result parse_vdm_response_common(int port)
{
	/* Retrieve the message information */
	uint32_t *payload;
	int sop;
	uint8_t type;
	uint8_t cnt;
	uint8_t ext;

	if (!PE_CHK_FLAG(port, PE_FLAGS_MSG_RECEIVED))
		return VDM_RESULT_WAITING;

	payload = (uint32_t *)rx_emsg[port].buf;
	sop = PD_HEADER_GET_SOP(rx_emsg[port].header);
	type = PD_HEADER_TYPE(rx_emsg[port].header);
	cnt = PD_HEADER_CNT(rx_emsg[port].header);
	ext = PD_HEADER_EXT(rx_emsg[port].header);

	if (sop == pe[port].tx_type && type == PD_DATA_VENDOR_DEF && cnt >= 1
			&& ext == 0) {
		if (PD_VDO_CMDT(payload[0]) == CMDT_RSP_ACK &&
				cnt >= pe[port].vdm_ack_min_data_objects) {
			/* Handle ACKs in state-specific code. */
			return VDM_RESULT_ACK;
		} else if (PD_VDO_CMDT(payload[0]) == CMDT_RSP_NAK) {
			/* Handle NAKs in state-specific code. */
			return VDM_RESULT_NAK;
		} else if (PD_VDO_CMDT(payload[0]) == CMDT_RSP_BUSY) {
			/*
			 * Don't fill in the discovery field so we re-probe in
			 * tVDMBusy
			 */
			CPRINTS("C%d: Partner BUSY, request will be retried",
					port);
			pe[port].discover_identity_timer =
					get_time().val + PD_T_VDM_BUSY;

			return VDM_RESULT_NO_ACTION;
		}

		/*
		 * Partner gave us an incorrect size or command; mark discovery
		 * as failed.
		 */
		CPRINTS("C%d: Unexpected VDM response: 0x%04x 0x%04x",
				port, rx_emsg[port].header, payload[0]);
		return VDM_RESULT_NAK;
	} else if (sop == pe[port].tx_type && ext == 0 && cnt == 0 &&
			type == PD_CTRL_NOT_SUPPORTED) {
		/*
		 * A NAK would be more expected here, but Not Supported is still
		 * allowed with the same meaning.
		 */
		return VDM_RESULT_NAK;
	}

	/* Unexpected Message Received. Src.Ready or Snk.Ready can handle it. */
	PE_SET_FLAG(port, PE_FLAGS_MSG_RECEIVED);
	return VDM_RESULT_NO_ACTION;
}

/**
 * PE_VDM_SEND_REQUEST
 * Shared parent to manage VDM timer and other shared parts of the VDM request
 * process
 */
static void pe_vdm_send_request_entry(int port)
{
	if (pe[port].tx_type == TCPC_TX_INVALID) {
		if (IS_ENABLED(USB_PD_DEBUG_LABELS))
			CPRINTS("C%d: %s: Tx type expected to be set, "
				"returning",
				port, pe_state_names[get_state_pe(port)]);
		set_state_pe(port, get_last_state_pe(port));
		return;
	}

	/* All VDM sequences are Interruptible */
	PE_SET_FLAG(port, PE_FLAGS_LOCALLY_INITIATED_AMS |
			PE_FLAGS_INTERRUPTIBLE_AMS);

	pe[port].vdm_response_timer = TIMER_DISABLED;
}

static void pe_vdm_send_request_run(int port)
{
	if (pe[port].vdm_response_timer == TIMER_DISABLED &&
			PE_CHK_FLAG(port, PE_FLAGS_TX_COMPLETE)) {
		/* Message was sent */
		PE_CLR_FLAG(port, PE_FLAGS_TX_COMPLETE);

		/* Start no response timer */
		/* TODO(b/155890173): Support DPM-supplied timeout */
		pe[port].vdm_response_timer =
			get_time().val + PD_T_VDM_SNDR_RSP;
	}

	if (PE_CHK_FLAG(port, PE_FLAGS_MSG_DISCARDED)) {
		/*
		 * Go back to ready on first AMS message discard
		 * (ready states will clear the discard flag)
		 */
		pe_set_ready_state(port);
		return;
	}

	/*
	 * Check the VDM timer, child will be responsible for processing
	 * messages and reacting appropriately to unexpected messages.
	 */
	if (get_time().val > pe[port].vdm_response_timer) {
		CPRINTF("VDM %s Response Timeout\n",
				pe[port].tx_type == TCPC_TX_SOP ?
				"Port" : "Cable");
		/*
		 * Flag timeout so child state can mark appropriate discovery
		 * item as failed.
		 */
		PE_SET_FLAG(port, PE_FLAGS_VDM_REQUEST_TIMEOUT);

		set_state_pe(port, get_last_state_pe(port));
	}
}

static void pe_vdm_send_request_exit(int port)
{
	/*
	 * Clear TX complete in case child called set_state_pe() before parent
	 * could process transmission
	 */
	PE_CLR_FLAG(port, PE_FLAGS_INTERRUPTIBLE_AMS);

	/* Invalidate TX type so it must be set before next call */
	pe[port].tx_type = TCPC_TX_INVALID;
}

/**
 * PE_VDM_IDENTITY_REQUEST_CBL
 * Combination of PE_INIT_PORT_VDM_Identity_Request State specific to the
 * cable and PE_SRC_VDM_Identity_Request State.
 * pe[port].tx_type must be set (to SOP') prior to entry.
 */
static void pe_vdm_identity_request_cbl_entry(int port)
{
	uint32_t *msg = (uint32_t *)tx_emsg[port].buf;

	print_current_state(port);

	msg[0] = VDO(USB_SID_PD, 1,
			VDO_SVDM_VERS(pd_get_vdo_ver(port, pe[port].tx_type)) |
			CMD_DISCOVER_IDENT);
	tx_emsg[port].len = sizeof(uint32_t);

	send_data_msg(port, pe[port].tx_type, PD_DATA_VENDOR_DEF);

	pe[port].discover_identity_counter++;

	/*
	 * Valid DiscoverIdentity responses should have at least 4 objects
	 * (header, ID header, Cert Stat, Product VDO).
	 */
	pe[port].vdm_ack_min_data_objects = 4;
}

static void pe_vdm_identity_request_cbl_run(int port)
{
	/* Retrieve the message information */
	uint32_t *payload = (uint32_t *) rx_emsg[port].buf;
	int sop = PD_HEADER_GET_SOP(rx_emsg[port].header);
	uint8_t type = PD_HEADER_TYPE(rx_emsg[port].header);
	uint8_t cnt = PD_HEADER_CNT(rx_emsg[port].header);
	uint8_t ext = PD_HEADER_EXT(rx_emsg[port].header);

	switch (parse_vdm_response_common(port)) {
	case VDM_RESULT_WAITING:
		/*
		 * The common code didn't parse a message. Handle protocol
		 * errors; otherwise, continue waiting.
		 */
		if (PE_CHK_FLAG(port, PE_FLAGS_PROTOCOL_ERROR)) {
			/*
			 * No Good CRC: See section 6.4.4.3.1 - Discover
			 * Identity.
			 *
			 * Discover Identity Command request sent to SOP' Shall
			 * Not cause a Soft Reset if a GoodCRC Message response
			 * is not returned since this can indicate a non-PD
			 * Capable cable.
			 */
			PE_CLR_FLAG(port, PE_FLAGS_PROTOCOL_ERROR);
			set_state_pe(port, get_last_state_pe(port));
		}
		return;
	case VDM_RESULT_NO_ACTION:
		/*
		 * If the received message doesn't change the discovery state,
		 * there is nothing to do but return to the previous ready
		 * state.
		 */
		if (get_last_state_pe(port) == PE_SRC_DISCOVERY &&
					(sop != pe[port].tx_type ||
					 type != PD_DATA_VENDOR_DEF ||
					 cnt == 0 || ext != 0)) {
			/*
			 * Unexpected non-VDM received: Before an explicit
			 * contract, an unexpected message shall generate a soft
			 * reset using the SOP* of the incoming message.
			 */
			pe_send_soft_reset(port, sop);
			return;
		}
		break;
	case VDM_RESULT_ACK:
		/* PE_INIT_PORT_VDM_Identity_ACKed embedded here */
		dfp_consume_identity(port, sop, cnt, payload);

		/*
		 * Note: If port partner runs PD 2.0, we must use PD 2.0 to
		 * communicate with the cable plug when in an explicit contract.
		 *
		 * PD Spec Table 6-2: Revision Interoperability during an
		 * Explicit Contract
		 */
		if (prl_get_rev(port, TCPC_TX_SOP) != PD_REV20)
			prl_set_rev(port, sop,
					PD_HEADER_REV(rx_emsg[port].header));
		break;
	case VDM_RESULT_NAK:
		/* PE_INIT_PORT_VDM_IDENTITY_NAKed embedded here */
		pd_set_identity_discovery(port, pe[port].tx_type, PD_DISC_FAIL);
		break;
	}

	/* Return to calling state (PE_{SRC,SNK}_Ready or PE_SRC_Discovery) */
	set_state_pe(port, get_last_state_pe(port));
}

static void pe_vdm_identity_request_cbl_exit(int port)
{
	if (pe[port].discover_identity_counter >= N_DISCOVER_IDENTITY_COUNT)
		pd_set_identity_discovery(port, pe[port].tx_type,
				PD_DISC_FAIL);

	/*
	 * Set discover identity timer unless BUSY case already did so
	 *
	 * Note: DiscoverIdentityTimer only applies within an explicit
	 * contract, so we could re-try faster from src_discovery if
	 * desired here
	 */
	if (pd_get_identity_discovery(port, pe[port].tx_type) == PD_DISC_NEEDED
			   && pe[port].discover_identity_timer > get_time().val)
		pe[port].discover_identity_timer = get_time().val +
							PD_T_DISCOVER_IDENTITY;

	/* Do not attempt further discovery if identity discovery failed. */
	if (pd_get_identity_discovery(port, pe[port].tx_type) == PD_DISC_FAIL)
		pd_set_svids_discovery(port, pe[port].tx_type, PD_DISC_FAIL);
}

/**
 * PE_INIT_PORT_VDM_Identity_Request
 *
 * Specific to SOP requests, as cables require additions for the discover
 * identity counter, must tolerate not receiving a GoodCRC, and need to set the
 * cable revision based on response.
 * pe[port].tx_type must be set (to SOP) prior to entry.
 */
static void pe_init_port_vdm_identity_request_entry(int port)
{
	uint32_t *msg = (uint32_t *)tx_emsg[port].buf;

	print_current_state(port);

	msg[0] = VDO(USB_SID_PD, 1,
			VDO_SVDM_VERS(pd_get_vdo_ver(port, pe[port].tx_type)) |
			CMD_DISCOVER_IDENT);
	tx_emsg[port].len = sizeof(uint32_t);

	send_data_msg(port, pe[port].tx_type, PD_DATA_VENDOR_DEF);

	/*
	 * Valid DiscoverIdentity responses should have at least 4 objects
	 * (header, ID header, Cert Stat, Product VDO).
	 */
	pe[port].vdm_ack_min_data_objects = 4;
}

static void pe_init_port_vdm_identity_request_run(int port)
{
	switch (parse_vdm_response_common(port)) {
	case VDM_RESULT_WAITING:
		/* If common code didn't parse a message, continue waiting. */
		return;
	case VDM_RESULT_NO_ACTION:
		/*
		 * If the received message doesn't change the discovery state,
		 * there is nothing to do but return to the previous ready
		 * state.
		 */
		break;
	case VDM_RESULT_ACK: {
		/* Retrieve the message information. */
		uint32_t *payload = (uint32_t *) rx_emsg[port].buf;
		int sop = PD_HEADER_GET_SOP(rx_emsg[port].header);
		uint8_t cnt = PD_HEADER_CNT(rx_emsg[port].header);

		/* PE_INIT_PORT_VDM_Identity_ACKed embedded here */
		dfp_consume_identity(port, sop, cnt, payload);
		break;
		}
	case VDM_RESULT_NAK:
		/* PE_INIT_PORT_VDM_IDENTITY_NAKed embedded here */
		pd_set_identity_discovery(port, pe[port].tx_type, PD_DISC_FAIL);
		break;
	}

	/* Return to calling state (PE_{SRC,SNK}_Ready) */
	set_state_pe(port, get_last_state_pe(port));
}

static void pe_init_port_vdm_identity_request_exit(int port)
{
	if (PE_CHK_FLAG(port, PE_FLAGS_VDM_REQUEST_TIMEOUT)) {
		PE_CLR_FLAG(port, PE_FLAGS_VDM_REQUEST_TIMEOUT);
		/*
		 * Mark failure to respond as discovery failure.
		 *
		 * For PD 2.0 partners (6.10.3 Applicability of Structured VDM
		 * Commands Note 3):
		 *
		 * If Structured VDMs are not supported, a Structured VDM
		 * Command received by a DFP or UFP Shall be Ignored.
		 */
		pd_set_identity_discovery(port, pe[port].tx_type, PD_DISC_FAIL);
	}

	/* Do not attempt further discovery if identity discovery failed. */
	if (pd_get_identity_discovery(port, pe[port].tx_type) == PD_DISC_FAIL)
		pd_set_svids_discovery(port, pe[port].tx_type, PD_DISC_FAIL);
}

/**
 * PE_INIT_VDM_SVIDs_Request
 *
 * Used for SOP and SOP' requests, selected by pe[port].tx_type prior to entry.
 */
static void pe_init_vdm_svids_request_entry(int port)
{
	uint32_t *msg = (uint32_t *)tx_emsg[port].buf;

	print_current_state(port);

	msg[0] = VDO(USB_SID_PD, 1,
			VDO_SVDM_VERS(pd_get_vdo_ver(port, pe[port].tx_type)) |
			CMD_DISCOVER_SVID);
	tx_emsg[port].len = sizeof(uint32_t);

	send_data_msg(port, pe[port].tx_type, PD_DATA_VENDOR_DEF);

	/*
	 * Valid Discover SVIDs ACKs should have at least 2 objects (VDM header
	 * and at least 1 SVID VDO).
	 */
	pe[port].vdm_ack_min_data_objects = 2;
}

static void pe_init_vdm_svids_request_run(int port)
{
	switch (parse_vdm_response_common(port)) {
	case VDM_RESULT_WAITING:
		/* If common code didn't parse a message, continue waiting. */
		return;
	case VDM_RESULT_NO_ACTION:
		/*
		 * If the received message doesn't change the discovery state,
		 * there is nothing to do but return to the previous ready
		 * state.
		 */
		break;
	case VDM_RESULT_ACK: {
		/* Retrieve the message information. */
		uint32_t *payload = (uint32_t *) rx_emsg[port].buf;
		int sop = PD_HEADER_GET_SOP(rx_emsg[port].header);
		uint8_t cnt = PD_HEADER_CNT(rx_emsg[port].header);

		/* PE_INIT_VDM_SVIDs_ACKed embedded here */
		dfp_consume_svids(port, sop, cnt, payload);
		break;
		}
	case VDM_RESULT_NAK:
		/* PE_INIT_VDM_SVIDs_NAKed embedded here */
		pd_set_svids_discovery(port, pe[port].tx_type, PD_DISC_FAIL);
		break;
	}

	/* Return to calling state (PE_{SRC,SNK}_Ready) */
	set_state_pe(port, get_last_state_pe(port));
}

/**
 * PE_INIT_VDM_Modes_Request
 *
 * Used for SOP and SOP' requests, selected by pe[port].tx_type prior to entry.
 */
static void pe_init_vdm_modes_request_entry(int port)
{
	uint32_t *msg = (uint32_t *)tx_emsg[port].buf;
	const struct svid_mode_data *mode_data =
		pd_get_next_mode(port, pe[port].tx_type);
	uint16_t svid;
	/*
	 * The caller should have checked that there was something to discover
	 * before entering this state.
	 */
	assert(mode_data);
	assert(mode_data->discovery == PD_DISC_NEEDED);
	svid = mode_data->svid;

	print_current_state(port);

	msg[0] = VDO((uint16_t) svid, 1,
			VDO_SVDM_VERS(pd_get_vdo_ver(port, pe[port].tx_type)) |
			CMD_DISCOVER_MODES);
	tx_emsg[port].len = sizeof(uint32_t);

	send_data_msg(port, pe[port].tx_type, PD_DATA_VENDOR_DEF);

	/*
	 * Valid Discover Modes responses should have at least 2 objects (VDM
	 * header and at least 1 mode VDO).
	 */
	pe[port].vdm_ack_min_data_objects = 2;
}

static void pe_init_vdm_modes_request_run(int port)
{
	struct svid_mode_data *mode_data;
	uint16_t requested_svid;

	mode_data = pd_get_next_mode(port, pe[port].tx_type);

	assert(mode_data);
	assert(mode_data->discovery == PD_DISC_NEEDED);
	requested_svid = mode_data->svid;

	switch (parse_vdm_response_common(port)) {
	case VDM_RESULT_WAITING:
		/* If common code didn't parse a message, continue waiting. */
		return;
	case VDM_RESULT_NO_ACTION:
		/*
		 * If the received message doesn't change the discovery state,
		 * there is nothing to do but return to the previous ready
		 * state.
		 */
		break;
	case VDM_RESULT_ACK: {
		/* Retrieve the message information. */
		uint32_t *payload = (uint32_t *) rx_emsg[port].buf;
		int sop = PD_HEADER_GET_SOP(rx_emsg[port].header);
		uint8_t cnt = PD_HEADER_CNT(rx_emsg[port].header);

		/* PE_INIT_VDM_Modes_ACKed embedded here */
		dfp_consume_modes(port, sop, cnt, payload);
		break;
		}
	case VDM_RESULT_NAK:
		/* PE_INIT_VDM_Modes_NAKed embedded here */
		pd_set_modes_discovery(port, pe[port].tx_type, requested_svid,
				PD_DISC_FAIL);
		break;
	}

	/* Return to calling state (PE_{SRC,SNK}_Ready) */
	set_state_pe(port, get_last_state_pe(port));
}

/**
 * PE_VDM_REQUEST_DPM
 *
 * Makes a VDM request with contents and SOP* type previously set up by the DPM.
 */

static void pe_vdm_request_dpm_entry(int port)
{
	print_current_state(port);

	/* Copy Vendor Data Objects (VDOs) into message buffer */
	if (pe[port].vdm_cnt > 0) {
		/* Copy data after header */
		memcpy(&tx_emsg[port].buf,
			(uint8_t *)pe[port].vdm_data,
			pe[port].vdm_cnt * 4);
		/* Update len with the number of VDO bytes */
		tx_emsg[port].len = pe[port].vdm_cnt * 4;
	}

	/*
	 * Clear the VDM nak'ed flag so that each request is
	 * treated separately (NAKs are handled by the
	 * DPM layer). Otherwise previous NAKs received will
	 * cause the state to exit early.
	 */
	PE_CLR_FLAG(port, PE_FLAGS_VDM_REQUEST_NAKED);
	send_data_msg(port, pe[port].tx_type, PD_DATA_VENDOR_DEF);

	/*
	 * In general, valid VDM ACKs must have a VDM header. Other than that,
	 * ACKs must be validated based on the command and SVID.
	 */
	pe[port].vdm_ack_min_data_objects = 1;
}

static void pe_vdm_request_dpm_run(int port)
{
	switch (parse_vdm_response_common(port)) {
	case VDM_RESULT_WAITING:
		/* If common code didn't parse a message, continue waiting. */
		return;
	case VDM_RESULT_NO_ACTION:
		/*
		 * If the received message doesn't change the discovery state,
		 * there is nothing to do but return to the previous ready
		 * state.
		 */
		break;
	case VDM_RESULT_ACK: {
		/* Retrieve the message information. */
		uint32_t *payload = (uint32_t *) rx_emsg[port].buf;
		int sop = PD_HEADER_GET_SOP(rx_emsg[port].header);
		uint8_t cnt = PD_HEADER_CNT(rx_emsg[port].header);
		uint16_t svid = PD_VDO_VID(payload[0]);
		uint8_t vdm_cmd = PD_VDO_CMD(payload[0]);

		/*
		 * PE initiator VDM-ACKed state for requested VDM, like
		 * PE_INIT_VDM_FOO_ACKed, embedded here.
		 */
		dpm_vdm_acked(port, sop, cnt, payload);

		if (sop == TCPC_TX_SOP && svid == USB_SID_DISPLAYPORT &&
				vdm_cmd == CMD_DP_CONFIG) {
			PE_SET_FLAG(port, PE_FLAGS_VDM_SETUP_DONE);
		}
		break;
		}
	case VDM_RESULT_NAK:
		/*
		 * PE initiator VDM-NAKed state for requested VDM, like
		 * PE_INIT_VDM_FOO_NAKed, embedded here.
		 */
		PE_SET_FLAG(port, PE_FLAGS_VDM_SETUP_DONE);

		/*
		 * Because Not Supported messages or response timeouts are
		 * treated as NAKs, there may not be a NAK message to parse.
		 * Extract the needed information from the sent VDM.
		 */
		dpm_vdm_naked(port, pe[port].tx_type,
				PD_VDO_VID(pe[port].vdm_data[0]),
				PD_VDO_CMD(pe[port].vdm_data[0]));
		break;
	}

	/* Return to calling state (PE_{SRC,SNK}_Ready) */
	set_state_pe(port, get_last_state_pe(port));
}

static void pe_vdm_request_dpm_exit(int port)
{
	/*
	 * Force Tx type to be reset before reentering a VDM state, unless the
	 * current VDM request will be resumed.
	 */
	if (!PE_CHK_FLAG(port, PE_FLAGS_VDM_REQUEST_CONTINUE))
		pe[port].tx_type = TCPC_TX_INVALID;
}

/**
 * PE_VDM_Response
 */
static void pe_vdm_response_entry(int port)
{
	int ret = 0;
	uint32_t *rx_payload;
	uint32_t *tx_payload;
	uint8_t vdo_cmd;
	int cmd_type;
	svdm_rsp_func func = NULL;

	print_current_state(port);

	/* This is an Interruptible AMS */
	PE_SET_FLAG(port, PE_FLAGS_INTERRUPTIBLE_AMS);

	/* Get the message */
	rx_payload = (uint32_t *)rx_emsg[port].buf;
	vdo_cmd = PD_VDO_CMD(rx_payload[0]);
	cmd_type = PD_VDO_CMDT(rx_payload[0]);
	rx_payload[0] &= ~VDO_CMDT_MASK;

	if (cmd_type != CMDT_INIT) {
		CPRINTF("ERR:CMDT:%d:%d\n", cmd_type, vdo_cmd);

		pe_set_ready_state(port);
		return;
	}

	switch (vdo_cmd) {
	case CMD_DISCOVER_IDENT:
		func = svdm_rsp.identity;
		break;
	case CMD_DISCOVER_SVID:
		func = svdm_rsp.svids;
		break;
	case CMD_DISCOVER_MODES:
		func = svdm_rsp.modes;
		break;
	case CMD_ENTER_MODE:
		func = svdm_rsp.enter_mode;
		break;
	case CMD_DP_STATUS:
		func = svdm_rsp.amode->status;
		break;
	case CMD_DP_CONFIG:
		func = svdm_rsp.amode->config;
		break;
	case CMD_EXIT_MODE:
		func = svdm_rsp.exit_mode;
		break;
#ifdef CONFIG_USB_PD_ALT_MODE_DFP
	case CMD_ATTENTION:
		/*
		 * attention is only SVDM with no response
		 * (just goodCRC) return zero here.
		 */
		dfp_consume_attention(port, rx_payload);
		pe_set_ready_state(port);
		return;
#endif
	default:
		CPRINTF("VDO ERR:CMD:%d\n", vdo_cmd);
	}

	tx_payload = (uint32_t *)tx_emsg[port].buf;

	if (func) {
		ret = func(port, rx_payload);
		if (ret)
			/* ACK */
			tx_payload[0] = VDO(
				USB_VID_GOOGLE,
				1, /* Structured VDM */
				VDO_SVDM_VERS(pd_get_vdo_ver(port, TCPC_TX_SOP))
				| VDO_CMDT(CMDT_RSP_ACK) |
				vdo_cmd);
		else if (!ret)
			/* NAK */
			tx_payload[0] = VDO(
				USB_VID_GOOGLE,
				1, /* Structured VDM */
				VDO_SVDM_VERS(pd_get_vdo_ver(port, TCPC_TX_SOP))
				| VDO_CMDT(CMDT_RSP_NAK) |
				vdo_cmd);
		else
			/* BUSY */
			tx_payload[0] = VDO(
				USB_VID_GOOGLE,
				1, /* Structured VDM */
				VDO_SVDM_VERS(pd_get_vdo_ver(port, TCPC_TX_SOP))
				| VDO_CMDT(CMDT_RSP_BUSY) |
				vdo_cmd);

		if (ret <= 0)
			ret = 4;
	} else {
		/* not supported : NACK it */
		tx_payload[0] = VDO(
			USB_VID_GOOGLE,
			1, /* Structured VDM */
			VDO_SVDM_VERS(pd_get_vdo_ver(port, TCPC_TX_SOP)) |
			VDO_CMDT(CMDT_RSP_NAK) |
			vdo_cmd);
		ret = 4;
	}

	/* Send ACK, NAK, or BUSY */
	tx_emsg[port].len = ret;
	send_data_msg(port, TCPC_TX_SOP, PD_DATA_VENDOR_DEF);
}

static void pe_vdm_response_run(int port)
{
	if (PE_CHK_FLAG(port, PE_FLAGS_TX_COMPLETE) ||
			PE_CHK_FLAG(port, PE_FLAGS_PROTOCOL_ERROR)) {
		PE_CLR_FLAG(port, PE_FLAGS_TX_COMPLETE |
						PE_FLAGS_PROTOCOL_ERROR);

		pe_set_ready_state(port);
	}
}

static void pe_vdm_response_exit(int port)
{
	PE_CLR_FLAG(port, PE_FLAGS_INTERRUPTIBLE_AMS);
}

/**
 * PE_DEU_SEND_ENTER_USB
 */
static void pe_enter_usb_entry(int port)
{
	uint32_t usb4_payload;

	print_current_state(port);

	if (!IS_ENABLED(CONFIG_USB_PD_USB4)) {
		pe_set_ready_state(port);
		return;
	}

	usb4_payload = enter_usb_setup_next_msg(port);

	/* Port is already in USB4 mode, do not send enter USB message again */
	if (usb4_payload < 0) {
		pe_set_ready_state(port);
		return;
	}

	if (!usb4_payload) {
		enter_usb_failed(port);
		pe_set_ready_state(port);
		return;
	}

	tx_emsg[port].len = sizeof(usb4_payload);

	memcpy(tx_emsg[port].buf, &usb4_payload, tx_emsg[port].len);
	send_data_msg(port, TCPC_TX_SOP, PD_DATA_ENTER_USB);

	pe[port].sender_response_timer = TIMER_DISABLED;
}

static void pe_enter_usb_run(int port)
{
	if (!IS_ENABLED(CONFIG_USB_PD_USB4)) {
		pe_set_ready_state(port);
		return;
	}

	/* Wait until message is sent */
	if (pe[port].sender_response_timer == TIMER_DISABLED) {
		if (!PE_CHK_FLAG(port, PE_FLAGS_TX_COMPLETE))
			return;

		PE_CLR_FLAG(port, PE_FLAGS_TX_COMPLETE);
		/* Initialize and run SenderResponseTimer */
		pe[port].sender_response_timer =
				get_time().val + PD_T_SENDER_RESPONSE;
	}

	if (get_time().val > pe[port].sender_response_timer) {
		pe_set_ready_state(port);
		enter_usb_failed(port);
		return;
	}

	if (PE_CHK_FLAG(port, PE_FLAGS_MSG_RECEIVED)) {
		int cnt = PD_HEADER_CNT(rx_emsg[port].header);
		int type = PD_HEADER_TYPE(rx_emsg[port].header);
		int sop = PD_HEADER_GET_SOP(rx_emsg[port].header);

		PE_CLR_FLAG(port, PE_FLAGS_MSG_RECEIVED);

		/* Only look at control messages */
		if (cnt == 0) {
			/* Accept message received */
			if (type == PD_CTRL_ACCEPT) {
				enter_usb_accepted(port, sop);
			} else if (type == PD_CTRL_REJECT) {
				enter_usb_rejected(port, sop);
			} else {
				/*
				 * Unexpected control message received.
				 * Send Soft Reset.
				 */
				pe_send_soft_reset(port, sop);
				return;
			}
		} else {
			/* Unexpected data message received. Send Soft reset */
			pe_send_soft_reset(port, sop);
			return;
		}
		pe_set_ready_state(port);
	}
}

#ifdef CONFIG_USBC_VCONN
/*
 * PE_VCS_Evaluate_Swap
 */
static void pe_vcs_evaluate_swap_entry(int port)
{
	print_current_state(port);

	/*
	 * Request the DPM for an evaluation of the VCONN Swap request.
	 * Note: Ports that are presently the VCONN Source must always
	 * accept a VCONN
	 */

	/*
	 * Transition to the PE_VCS_Accept_Swap state when:
	 *  1) The Device Policy Manager indicates that a VCONN Swap is ok.
	 *
	 * Transition to the PE_VCS_Reject_Swap state when:
	 *  1)  Port is not presently the VCONN Source and
	 *  2) The DPM indicates that a VCONN Swap is not ok or
	 *  3) The DPM indicates that a VCONN Swap cannot be done at this time.
	 */

	/* DPM rejects a VCONN Swap and port is not a VCONN source*/
	if (!tc_check_vconn_swap(port) && tc_is_vconn_src(port) < 1) {
		/* NOTE: PE_VCS_Reject_Swap State embedded here */
		send_ctrl_msg(port, TCPC_TX_SOP, PD_CTRL_REJECT);
	}
	/* Port is not ready to perform a VCONN swap */
	else if (tc_is_vconn_src(port) < 0) {
		/* NOTE: PE_VCS_Reject_Swap State embedded here */
		send_ctrl_msg(port, TCPC_TX_SOP, PD_CTRL_WAIT);
	}
	/* Port is ready to perform a VCONN swap */
	else {
		/* NOTE: PE_VCS_Accept_Swap State embedded here */
		PE_SET_FLAG(port, PE_FLAGS_ACCEPT);
		send_ctrl_msg(port, TCPC_TX_SOP, PD_CTRL_ACCEPT);
	}
}

static void pe_vcs_evaluate_swap_run(int port)
{
	/* Wait for ACCEPT, WAIT or Reject message to send. */
	if (PE_CHK_FLAG(port, PE_FLAGS_TX_COMPLETE)) {
		PE_CLR_FLAG(port, PE_FLAGS_TX_COMPLETE);

		if (PE_CHK_FLAG(port, PE_FLAGS_ACCEPT)) {
			PE_CLR_FLAG(port, PE_FLAGS_ACCEPT);
			/* Accept Message sent and Presently VCONN Source */
			if (tc_is_vconn_src(port))
				set_state_pe(port, PE_VCS_WAIT_FOR_VCONN_SWAP);
			/* Accept Message sent and Not presently VCONN Source */
			else
				set_state_pe(port, PE_VCS_TURN_ON_VCONN_SWAP);
		} else {
			/*
			 * Message sent. Transition back to PE_SRC_Ready or
			 * PE_SINK_Ready
			 */
			pe_set_ready_state(port);
		}
	}
}

/*
 * PE_VCS_Send_Swap
 */
static void pe_vcs_send_swap_entry(int port)
{
	print_current_state(port);

	/* Send a VCONN_Swap Message */
	send_ctrl_msg(port, TCPC_TX_SOP, PD_CTRL_VCONN_SWAP);
}

static void pe_vcs_send_swap_run(int port)
{
	uint8_t type;
	uint8_t cnt;
	enum tcpm_transmit_type sop;

	if (PE_CHK_FLAG(port, PE_FLAGS_MSG_RECEIVED)) {
		PE_CLR_FLAG(port, PE_FLAGS_MSG_RECEIVED);

		type = PD_HEADER_TYPE(rx_emsg[port].header);
		cnt = PD_HEADER_CNT(rx_emsg[port].header);
		sop = PD_HEADER_GET_SOP(rx_emsg[port].header);

		/* Only look at control messages */
		if (cnt == 0) {
			/*
			 * Transition to the PE_VCS_Wait_For_VCONN state when:
			 *   1) Accept Message Received and
			 *   2) The Port is presently the VCONN Source.
			 *
			 * Transition to the PE_VCS_Turn_On_VCONN state when:
			 *   1) Accept Message Received and
			 *   2) The Port is not presently the VCONN Source.
			 */
			if (type == PD_CTRL_ACCEPT) {
				if (tc_is_vconn_src(port))
					set_state_pe(port,
						PE_VCS_WAIT_FOR_VCONN_SWAP);
				else
					set_state_pe(port,
						PE_VCS_TURN_ON_VCONN_SWAP);
				return;
			}
			/*
			 * Transition back to either the PE_SRC_Ready or
			 * PE_SNK_Ready state when:
			 *   2) Reject message is received or
			 *   3) Wait message Received.
			 */
			if (type == PD_CTRL_REJECT || type == PD_CTRL_WAIT) {
				pe_set_ready_state(port);
				return;
			}
		}
		/*
		 * Unexpected Data Message Received
		 */
		else {
			/* Send Soft Reset */
			pe_send_soft_reset(port, sop);
			return;
		}
	}

	/*
	 * Transition back to either the PE_SRC_Ready or
	 * PE_SNK_Ready state when:
	 *   1) SenderResponseTimer Timeout
	 */
	if (get_time().val > pe[port].sender_response_timer)
		pe_set_ready_state(port);
}

/*
 * PE_VCS_Wait_for_VCONN_Swap
 */
static void pe_vcs_wait_for_vconn_swap_entry(int port)
{
	print_current_state(port);

	/* Start the VCONNOnTimer */
	pe[port].vconn_on_timer = get_time().val + PD_T_VCONN_SOURCE_ON;
}

static void pe_vcs_wait_for_vconn_swap_run(int port)
{
	/*
	 * Transition to the PE_VCS_Turn_Off_VCONN state when:
	 *  1) A PS_RDY Message is received.
	 */
	if (PE_CHK_FLAG(port, PE_FLAGS_MSG_RECEIVED)) {
		PE_CLR_FLAG(port, PE_FLAGS_MSG_RECEIVED);
		/*
		 * PS_RDY message received
		 */
		if ((PD_HEADER_CNT(rx_emsg[port].header) == 0) &&
				(PD_HEADER_TYPE(rx_emsg[port].header) ==
						PD_CTRL_PS_RDY)) {
			set_state_pe(port, PE_VCS_TURN_OFF_VCONN_SWAP);
			return;
		}
	}

	/*
	 * Transition to either the PE_SRC_Hard_Reset or
	 * PE_SNK_Hard_Reset state when:
	 *   1) The VCONNOnTimer times out.
	 */
	if (get_time().val > pe[port].vconn_on_timer) {
		if (pe[port].power_role == PD_ROLE_SOURCE)
			set_state_pe(port, PE_SRC_HARD_RESET);
		else
			set_state_pe(port, PE_SNK_HARD_RESET);
	}
}

/*
 * PE_VCS_Turn_On_VCONN_Swap
 */
static void pe_vcs_turn_on_vconn_swap_entry(int port)
{
	print_current_state(port);

	/* Request DPM to turn on VCONN */
	pd_request_vconn_swap_on(port);
	pe[port].timeout = 0;
}

static void pe_vcs_turn_on_vconn_swap_run(int port)
{

	/*
	 * Transition to the PE_VCS_Send_Ps_Rdy state when:
	 *  1) The Port’s VCONN is on.
	 */
	if (pe[port].timeout == 0 &&
			PE_CHK_FLAG(port, PE_FLAGS_VCONN_SWAP_COMPLETE)) {
		PE_CLR_FLAG(port, PE_FLAGS_VCONN_SWAP_COMPLETE);
		pe[port].timeout = get_time().val + PD_VCONN_SWAP_DELAY;
	}

	if (pe[port].timeout > 0 && get_time().val > pe[port].timeout)
		set_state_pe(port, PE_VCS_SEND_PS_RDY_SWAP);
}

/*
 * PE_VCS_Turn_Off_VCONN_Swap
 */
static void pe_vcs_turn_off_vconn_swap_entry(int port)
{
	print_current_state(port);

	/* Request DPM to turn off VCONN */
	pd_request_vconn_swap_off(port);
	pe[port].timeout = 0;
}

static void pe_vcs_turn_off_vconn_swap_run(int port)
{
	/* Wait for VCONN to turn off */
	if (pe[port].timeout == 0 &&
			PE_CHK_FLAG(port, PE_FLAGS_VCONN_SWAP_COMPLETE)) {
		PE_CLR_FLAG(port, PE_FLAGS_VCONN_SWAP_COMPLETE);
		pe[port].timeout = get_time().val + PD_VCONN_SWAP_DELAY;
	}

	if (pe[port].timeout > 0 && get_time().val > pe[port].timeout) {
		/*
		 * A VCONN Swap Shall reset the DiscoverIdentityCounter
		 * to zero
		 */
		pe[port].discover_identity_counter = 0;
		pe[port].dr_swap_attempt_counter = 0;

		if (pe[port].power_role == PD_ROLE_SOURCE)
			set_state_pe(port, PE_SRC_READY);
		else
			set_state_pe(port, PE_SNK_READY);
	}
}

/*
 * PE_VCS_Send_PS_Rdy_Swap
 */
static void pe_vcs_send_ps_rdy_swap_entry(int port)
{
	print_current_state(port);

	/* Send a PS_RDY Message */
	send_ctrl_msg(port, TCPC_TX_SOP, PD_CTRL_PS_RDY);
	pe[port].sub = PE_SUB0;
}

static void pe_vcs_send_ps_rdy_swap_run(int port)
{
	/* TODO(b/152058087): TCPMv2: Break up pe_vcs_send_ps_rdy_swap */
	switch (pe[port].sub) {
	case PE_SUB0:
		/*
		 * After a VCONN Swap the VCONN Source needs to reset
		 * the Cable Plug’s Protocol Layer in order to ensure
		 * MessageID synchronization.
		 */
		if (PE_CHK_FLAG(port, PE_FLAGS_TX_COMPLETE)) {
			PE_CLR_FLAG(port, PE_FLAGS_TX_COMPLETE);

			send_ctrl_msg(port, TCPC_TX_SOP_PRIME,
					  PD_CTRL_SOFT_RESET);
			pe[port].sub = PE_SUB1;
		}
		break;
	case PE_SUB1:
		if (PE_CHK_FLAG(port, PE_FLAGS_TX_COMPLETE)) {
			PE_CLR_FLAG(port, PE_FLAGS_TX_COMPLETE);
			pe[port].sender_response_timer = get_time().val +
							PD_T_SENDER_RESPONSE;
		}

		/* Got ACCEPT or REJECT from Cable Plug */
		if (PE_CHK_FLAG(port, PE_FLAGS_MSG_RECEIVED) ||
		    get_time().val > pe[port].sender_response_timer) {
			PE_CLR_FLAG(port, PE_FLAGS_MSG_RECEIVED);
			/*
			 * A VCONN Swap Shall reset the
			 * DiscoverIdentityCounter to zero
			 */
			pe[port].discover_identity_counter = 0;
			pe[port].dr_swap_attempt_counter = 0;

			if (pe[port].power_role == PD_ROLE_SOURCE)
				set_state_pe(port, PE_SRC_READY);
			else
				set_state_pe(port, PE_SNK_READY);
		}
		break;
	case PE_SUB2:
		/* Do nothing */
		break;
	}

	if (PE_CHK_FLAG(port, PE_FLAGS_PROTOCOL_ERROR)) {
		PE_CLR_FLAG(port, PE_FLAGS_PROTOCOL_ERROR);

		if (pe[port].sub == PE_SUB0) {
			/* PS_RDY didn't send, soft reset */
			pe_send_soft_reset(port, TCPC_TX_SOP);
		} else {
			/*
			 * Cable plug wasn't present,
			 * return to ready state
			 */
			pe_set_ready_state(port);
		}
	}
}
#endif /* CONFIG_USBC_VCONN */

/*
 * PE_DR_SNK_Get_Sink_Cap
 */
static void pe_dr_snk_get_sink_cap_entry(int port)
{
	print_current_state(port);

	/* Send a Get Sink Cap Message */
	send_ctrl_msg(port, TCPC_TX_SOP, PD_CTRL_GET_SINK_CAP);
}

static void pe_dr_snk_get_sink_cap_run(int port)
{
	int type;
	int cnt;
	int ext;
	int rev;

	/*
	 * Determine if FRS is possible based on the returned Sink Caps
	 *
	 * Transition to PE_SNK_Ready when:
	 *   1) A Sink_Capabilities Message is received
	 *   2) Or SenderResponseTimer times out
	 *   3) Or a Reject Message is received.
	 *
	 * Transition to PE_SEND_SOFT_RESET state when:
	 *   1) An unexpected message is received
	 */
	if (PE_CHK_FLAG(port, PE_FLAGS_MSG_RECEIVED)) {
		PE_CLR_FLAG(port, PE_FLAGS_MSG_RECEIVED);

		type = PD_HEADER_TYPE(rx_emsg[port].header);
		cnt = PD_HEADER_CNT(rx_emsg[port].header);
		ext = PD_HEADER_EXT(rx_emsg[port].header);
		rev = PD_HEADER_REV(rx_emsg[port].header);

		if (ext == 0) {
			if ((cnt > 0) && (type == PD_DATA_SINK_CAP)) {
				uint32_t payload =
					*(uint32_t *)rx_emsg[port].buf;

				/*
				 * Check message to see if we can handle
				 * FRS for this connection. Multiple PDOs
				 * may be returned, for FRS only Fixed PDOs
				 * shall be used, and this shall be the 1st
				 * PDO returned.
				 *
				 * TODO(b/14191267): Make sure we can handle
				 * the required current before we enable FRS.
				 */
				if (IS_ENABLED(CONFIG_USB_PD_REV30) &&
					(rev > PD_REV20) &&
					(payload & PDO_FIXED_DUAL_ROLE)) {
					switch (payload &
						PDO_FIXED_FRS_CURR_MASK) {
					case PDO_FIXED_FRS_CURR_NOT_SUPPORTED:
						break;
					case PDO_FIXED_FRS_CURR_DFLT_USB_POWER:
					case PDO_FIXED_FRS_CURR_1A5_AT_5V:
					case PDO_FIXED_FRS_CURR_3A0_AT_5V:
						typec_set_source_current_limit(
							port, TYPEC_RP_3A0);
						pe_set_frs_enable(port, 1);
						break;
					}
				}
				set_state_pe(port, PE_SNK_READY);
			} else if (type == PD_CTRL_REJECT ||
				   type == PD_CTRL_NOT_SUPPORTED) {
				set_state_pe(port, PE_SNK_READY);
			} else {
				set_state_pe(port, PE_SEND_SOFT_RESET);
			}
			return;
		}
	}

	/*
	 * Transition to PE_SNK_Ready state when:
	 *   1) SenderResponseTimer times out.
	 */
	if (get_time().val > pe[port].sender_response_timer)
		set_state_pe(port, PE_SNK_READY);
}

/*
 * PE_SENDER_RESPONSE
 *
 * Parent state to run first message in an AMS and start SenderResponseTimer
 * appropriately.
 */
static void pe_sender_response_entry(int port)
{
	pe[port].sender_response_timer = TIMER_DISABLED;
}

static void pe_sender_response_run(int port)
{
	if (PE_CHK_FLAG(port, PE_FLAGS_MSG_DISCARDED)) {
		/*
		 * Go back to ready on first AMS message discard
		 * (ready states will clear the discard flag)
		 */
		pe_set_ready_state(port);
		return;
	}

	if (pe[port].sender_response_timer == TIMER_DISABLED &&
			PE_CHK_FLAG(port, PE_FLAGS_TX_COMPLETE)) {
		PE_CLR_FLAG(port, PE_FLAGS_TX_COMPLETE);
		/* Initialize and run SenderResponseTimer */
		pe[port].sender_response_timer =
			get_time().val + PD_T_SENDER_RESPONSE;
	}

	/* Note: child must check timer, as response to a timeout varies */
}

const uint32_t * const pd_get_src_caps(int port)
{
	return pe[port].src_caps;
}

void pd_set_src_caps(int port, int cnt, uint32_t *src_caps)
{
	int i;

	pe[port].src_cap_cnt = cnt;

	for (i = 0; i < cnt; i++)
		pe[port].src_caps[i] = *src_caps++;
}

uint8_t pd_get_src_cap_cnt(int port)
{
	return pe[port].src_cap_cnt;
}

void pd_dfp_discovery_init(int port)
{
	memset(&pe[port].discovery, 0, sizeof(pe[port].discovery));
	memset(pe[port].partner_amodes, 0, sizeof(pe[port].partner_amodes));

	/* Reset the DPM and DP modules to enable alternate mode entry. */
	dpm_init(port);
	dp_init(port);

	if (IS_ENABLED(CONFIG_USB_PD_TBT_COMPAT_MODE))
		tbt_init(port);

	if (IS_ENABLED(CONFIG_USB_PD_USB4))
		enter_usb_init(port);
}

#ifdef CONFIG_USB_PD_ALT_MODE_DFP
struct pd_discovery *pd_get_am_discovery(int port, enum tcpm_transmit_type type)
{
	return &pe[port].discovery[type];
}

struct partner_active_modes *pd_get_partner_active_modes(int port,
		enum tcpm_transmit_type type)
{
	assert(type < AMODE_TYPE_COUNT);
	return &pe[port].partner_amodes[type];
}

void pd_set_dfp_enter_mode_flag(int port, bool set)
{
	if (set)
		PE_SET_FLAG(port, PE_FLAGS_MODAL_OPERATION);
	else
		PE_CLR_FLAG(port, PE_FLAGS_MODAL_OPERATION);
}
#endif /* CONFIG_USB_PD_ALT_MODE_DFP */

const char *pe_get_current_state(int port)
{
	if (pe_is_running(port) && IS_ENABLED(USB_PD_DEBUG_LABELS))
		return pe_state_names[get_state_pe(port)];
	else
		return "";
}

uint32_t pe_get_flags(int port)
{
	return pe[port].flags;
}


static const struct usb_state pe_states[] = {
	/* Super States */
#ifdef CONFIG_USB_PD_REV30
	[PE_PRS_FRS_SHARED] = {
		.entry = pe_prs_frs_shared_entry,
		.exit  = pe_prs_frs_shared_exit,
	},
#endif /* CONFIG_USB_PD_REV30 */

	/* Normal States */
	[PE_SRC_STARTUP] = {
		.entry = pe_src_startup_entry,
		.run   = pe_src_startup_run,
	},
	[PE_SRC_DISCOVERY] = {
		.entry = pe_src_discovery_entry,
		.run   = pe_src_discovery_run,
	},
	[PE_SRC_SEND_CAPABILITIES] = {
		.entry = pe_src_send_capabilities_entry,
		.run   = pe_src_send_capabilities_run,
	},
	[PE_SRC_NEGOTIATE_CAPABILITY] = {
		.entry = pe_src_negotiate_capability_entry,
	},
	[PE_SRC_TRANSITION_SUPPLY] = {
		.entry = pe_src_transition_supply_entry,
		.run   = pe_src_transition_supply_run,
	},
	[PE_SRC_READY] = {
		.entry = pe_src_ready_entry,
		.run   = pe_src_ready_run,
	},
	[PE_SRC_DISABLED] = {
		.entry = pe_src_disabled_entry,
	},
	[PE_SRC_CAPABILITY_RESPONSE] = {
		.entry = pe_src_capability_response_entry,
		.run   = pe_src_capability_response_run,
	},
	[PE_SRC_HARD_RESET] = {
		.entry = pe_src_hard_reset_entry,
		.run   = pe_src_hard_reset_run,
	},
	[PE_SRC_HARD_RESET_RECEIVED] = {
		.entry = pe_src_hard_reset_received_entry,
		.run = pe_src_hard_reset_received_run,
	},
	[PE_SRC_TRANSITION_TO_DEFAULT] = {
		.entry = pe_src_transition_to_default_entry,
		.run = pe_src_transition_to_default_run,
	},
	[PE_SNK_STARTUP] = {
		.entry = pe_snk_startup_entry,
		.run = pe_snk_startup_run,
	},
	[PE_SNK_DISCOVERY] = {
		.entry = pe_snk_discovery_entry,
		.run = pe_snk_discovery_run,
	},
	[PE_SNK_WAIT_FOR_CAPABILITIES] = {
		.entry = pe_snk_wait_for_capabilities_entry,
		.run = pe_snk_wait_for_capabilities_run,
	},
	[PE_SNK_EVALUATE_CAPABILITY] = {
		.entry = pe_snk_evaluate_capability_entry,
	},
	[PE_SNK_SELECT_CAPABILITY] = {
		.entry = pe_snk_select_capability_entry,
		.run = pe_snk_select_capability_run,
	},
	[PE_SNK_READY] = {
		.entry = pe_snk_ready_entry,
		.run   = pe_snk_ready_run,
	},
	[PE_SNK_HARD_RESET] = {
		.entry = pe_snk_hard_reset_entry,
		.run   = pe_snk_hard_reset_run,
	},
	[PE_SNK_TRANSITION_TO_DEFAULT] = {
		.entry = pe_snk_transition_to_default_entry,
		.run   = pe_snk_transition_to_default_run,
	},
	[PE_SNK_GIVE_SINK_CAP] = {
		.entry = pe_snk_give_sink_cap_entry,
		.run = pe_snk_give_sink_cap_run,
	},
	[PE_SNK_GET_SOURCE_CAP] = {
		.entry = pe_snk_get_source_cap_entry,
		.run   = pe_snk_get_source_cap_run,
	},
	[PE_SNK_TRANSITION_SINK] = {
		.entry = pe_snk_transition_sink_entry,
		.run   = pe_snk_transition_sink_run,
		.exit   = pe_snk_transition_sink_exit,
	},
	[PE_SEND_SOFT_RESET] = {
		.entry = pe_send_soft_reset_entry,
		.run = pe_send_soft_reset_run,
	},
	[PE_SOFT_RESET] = {
		.entry = pe_soft_reset_entry,
		.run = pe_soft_reset_run,
	},
	[PE_SEND_NOT_SUPPORTED] = {
		.entry = pe_send_not_supported_entry,
		.run = pe_send_not_supported_run,
	},
	[PE_SRC_PING] = {
		.entry = pe_src_ping_entry,
		.run   = pe_src_ping_run,
	},
#ifdef CONFIG_USB_PD_REV30
	[PE_GIVE_BATTERY_CAP] = {
		.entry = pe_give_battery_cap_entry,
		.run   = pe_give_battery_cap_run,
	},
	[PE_GIVE_BATTERY_STATUS] = {
		.entry = pe_give_battery_status_entry,
		.run   = pe_give_battery_status_run,
	},
	[PE_SEND_ALERT] = {
		.entry = pe_send_alert_entry,
		.run   = pe_send_alert_run,
	},
#endif /* CONFIG_USB_PD_REV30 */
	[PE_DRS_EVALUATE_SWAP] = {
		.entry = pe_drs_evaluate_swap_entry,
		.run   = pe_drs_evaluate_swap_run,
	},
	[PE_DRS_CHANGE] = {
		.entry = pe_drs_change_entry,
		.run   = pe_drs_change_run,
	},
	[PE_DRS_SEND_SWAP] = {
		.entry = pe_drs_send_swap_entry,
		.run   = pe_drs_send_swap_run,
		.parent = &pe_states[PE_SENDER_RESPONSE],
	},
	[PE_PRS_SRC_SNK_EVALUATE_SWAP] = {
		.entry = pe_prs_src_snk_evaluate_swap_entry,
		.run   = pe_prs_src_snk_evaluate_swap_run,
	},
	[PE_PRS_SRC_SNK_TRANSITION_TO_OFF] = {
		.entry = pe_prs_src_snk_transition_to_off_entry,
		.run   = pe_prs_src_snk_transition_to_off_run,
	},
	[PE_PRS_SRC_SNK_ASSERT_RD] = {
		.entry = pe_prs_src_snk_assert_rd_entry,
		.run   = pe_prs_src_snk_assert_rd_run,
	},
	[PE_PRS_SRC_SNK_WAIT_SOURCE_ON] = {
		.entry = pe_prs_src_snk_wait_source_on_entry,
		.run   = pe_prs_src_snk_wait_source_on_run,
		.exit  = pe_prs_src_snk_wait_source_on_exit,
	},
	[PE_PRS_SRC_SNK_SEND_SWAP] = {
		.entry = pe_prs_src_snk_send_swap_entry,
		.run   = pe_prs_src_snk_send_swap_run,
		.parent = &pe_states[PE_SENDER_RESPONSE],
	},
	[PE_PRS_SNK_SRC_EVALUATE_SWAP] = {
		.entry = pe_prs_snk_src_evaluate_swap_entry,
		.run   = pe_prs_snk_src_evaluate_swap_run,
	},
	/*
	 * Some of the Power Role Swap actions are shared with the very
	 * similar actions of Fast Role Swap.
	 */
	/* State actions are shared with PE_FRS_SNK_SRC_TRANSITION_TO_OFF */
	[PE_PRS_SNK_SRC_TRANSITION_TO_OFF] = {
		.entry = pe_prs_snk_src_transition_to_off_entry,
		.run   = pe_prs_snk_src_transition_to_off_run,
#ifdef CONFIG_USB_PD_REV30
		.parent = &pe_states[PE_PRS_FRS_SHARED],
#endif /* CONFIG_USB_PD_REV30 */
	},
	/* State actions are shared with PE_FRS_SNK_SRC_ASSERT_RP */
	[PE_PRS_SNK_SRC_ASSERT_RP] = {
		.entry = pe_prs_snk_src_assert_rp_entry,
		.run   = pe_prs_snk_src_assert_rp_run,
#ifdef CONFIG_USB_PD_REV30
		.parent = &pe_states[PE_PRS_FRS_SHARED],
#endif /* CONFIG_USB_PD_REV30 */
	},
	/* State actions are shared with PE_FRS_SNK_SRC_SOURCE_ON */
	[PE_PRS_SNK_SRC_SOURCE_ON] = {
		.entry = pe_prs_snk_src_source_on_entry,
		.run   = pe_prs_snk_src_source_on_run,
		.exit  = pe_prs_snk_src_source_on_exit,
#ifdef CONFIG_USB_PD_REV30
		.parent = &pe_states[PE_PRS_FRS_SHARED],
#endif /* CONFIG_USB_PD_REV30 */
	},
	/* State actions are shared with PE_FRS_SNK_SRC_SEND_SWAP */
	[PE_PRS_SNK_SRC_SEND_SWAP] = {
		.entry = pe_prs_snk_src_send_swap_entry,
		.run   = pe_prs_snk_src_send_swap_run,
#ifdef CONFIG_USB_PD_REV30
		.parent = &pe_states[PE_PRS_FRS_SHARED],
#endif /* CONFIG_USB_PD_REV30 */
	},
#ifdef CONFIG_USB_PD_REV30
	[PE_FRS_SNK_SRC_START_AMS] = {
		.entry = pe_frs_snk_src_start_ams_entry,
		.parent = &pe_states[PE_PRS_FRS_SHARED],
	},
#endif /* CONFIG_USB_PD_REV30 */
#ifdef CONFIG_USBC_VCONN
	[PE_VCS_EVALUATE_SWAP] = {
		.entry = pe_vcs_evaluate_swap_entry,
		.run   = pe_vcs_evaluate_swap_run,
	},
	[PE_VCS_SEND_SWAP] = {
		.entry = pe_vcs_send_swap_entry,
		.run   = pe_vcs_send_swap_run,
		.parent = &pe_states[PE_SENDER_RESPONSE],
	},
	[PE_VCS_WAIT_FOR_VCONN_SWAP] = {
		.entry = pe_vcs_wait_for_vconn_swap_entry,
		.run   = pe_vcs_wait_for_vconn_swap_run,
	},
	[PE_VCS_TURN_ON_VCONN_SWAP] = {
		.entry = pe_vcs_turn_on_vconn_swap_entry,
		.run   = pe_vcs_turn_on_vconn_swap_run,
	},
	[PE_VCS_TURN_OFF_VCONN_SWAP] = {
		.entry = pe_vcs_turn_off_vconn_swap_entry,
		.run   = pe_vcs_turn_off_vconn_swap_run,
	},
	[PE_VCS_SEND_PS_RDY_SWAP] = {
		.entry = pe_vcs_send_ps_rdy_swap_entry,
		.run   = pe_vcs_send_ps_rdy_swap_run,
	},
#endif /* CONFIG_USBC_VCONN */
	[PE_VDM_SEND_REQUEST] = {
		.entry = pe_vdm_send_request_entry,
		.run   = pe_vdm_send_request_run,
		.exit  = pe_vdm_send_request_exit,
	},
	[PE_VDM_IDENTITY_REQUEST_CBL] = {
		.entry  = pe_vdm_identity_request_cbl_entry,
		.run    = pe_vdm_identity_request_cbl_run,
		.exit   = pe_vdm_identity_request_cbl_exit,
		.parent = &pe_states[PE_VDM_SEND_REQUEST],
	},
	[PE_INIT_PORT_VDM_IDENTITY_REQUEST] = {
		.entry  = pe_init_port_vdm_identity_request_entry,
		.run    = pe_init_port_vdm_identity_request_run,
		.exit	= pe_init_port_vdm_identity_request_exit,
		.parent = &pe_states[PE_VDM_SEND_REQUEST],
	},
	[PE_INIT_VDM_SVIDS_REQUEST] = {
		.entry	= pe_init_vdm_svids_request_entry,
		.run	= pe_init_vdm_svids_request_run,
		.parent = &pe_states[PE_VDM_SEND_REQUEST],
	},
	[PE_INIT_VDM_MODES_REQUEST] = {
		.entry	= pe_init_vdm_modes_request_entry,
		.run	= pe_init_vdm_modes_request_run,
		.parent = &pe_states[PE_VDM_SEND_REQUEST],
	},
	[PE_VDM_REQUEST_DPM] = {
		.entry = pe_vdm_request_dpm_entry,
		.run   = pe_vdm_request_dpm_run,
		.exit  = pe_vdm_request_dpm_exit,
		.parent = &pe_states[PE_VDM_SEND_REQUEST],
	},
	[PE_VDM_RESPONSE] = {
		.entry = pe_vdm_response_entry,
		.run   = pe_vdm_response_run,
		.exit  = pe_vdm_response_exit,
	},
	[PE_HANDLE_CUSTOM_VDM_REQUEST] = {
		.entry = pe_handle_custom_vdm_request_entry,
		.run   = pe_handle_custom_vdm_request_run,
		.exit  = pe_handle_custom_vdm_request_exit,
	},
	[PE_DEU_SEND_ENTER_USB] = {
		.entry = pe_enter_usb_entry,
		.run = pe_enter_usb_run,
	},
	[PE_WAIT_FOR_ERROR_RECOVERY] = {
		.entry = pe_wait_for_error_recovery_entry,
		.run   = pe_wait_for_error_recovery_run,
	},
	[PE_BIST_TX] = {
		.entry = pe_bist_tx_entry,
		.run   = pe_bist_tx_run,
	},
	[PE_BIST_RX] = {
		.entry = pe_bist_rx_entry,
		.run   = pe_bist_rx_run,
	},
	[PE_DR_SNK_GET_SINK_CAP] = {
		.entry = pe_dr_snk_get_sink_cap_entry,
		.run   = pe_dr_snk_get_sink_cap_run,
		.parent = &pe_states[PE_SENDER_RESPONSE],
	},
	[PE_SENDER_RESPONSE] = {
		.entry = pe_sender_response_entry,
		.run   = pe_sender_response_run,
	},
};

#ifdef TEST_BUILD
const struct test_sm_data test_pe_sm_data[] = {
	{
		.base = pe_states,
		.size = ARRAY_SIZE(pe_states),
		.names = pe_state_names,
		.names_size = ARRAY_SIZE(pe_state_names),
	},
};
BUILD_ASSERT(ARRAY_SIZE(pe_states) == ARRAY_SIZE(pe_state_names));
const int test_pe_sm_data_size = ARRAY_SIZE(test_pe_sm_data);

void pe_set_flag(int port, int flag)
{
	PE_SET_FLAG(port, flag);
}
void pe_clr_flag(int port, int flag)
{
	PE_CLR_FLAG(port, flag);
}
int pe_chk_flag(int port, int flag)
{
	return PE_CHK_FLAG(port, flag);
}
int pe_get_all_flags(int port)
{
	return pe[port].flags;
}
void pe_set_all_flags(int port, int flags)
{
	pe[port].flags = flags;
}
#endif
