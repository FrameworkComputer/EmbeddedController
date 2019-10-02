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
#include "task.h"
#include "tcpm.h"
#include "util.h"
#include "usb_common.h"
#include "usb_pd.h"
#include "usb_pd_tcpm.h"
#include "usb_pe_sm.h"
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
#endif

#define PE_SET_FLAG(port, flag) atomic_or(&pe[port].flags, (flag))
#define PE_CLR_FLAG(port, flag) atomic_clear(&pe[port].flags, (flag))
#define PE_CHK_FLAG(port, flag) (pe[port].flags & (flag))

/*
 * These macros SET, CLEAR, and CHECK, a DPM (Device Policy Manager)
 * Request. The Requests are listed in usb_pe_sm.h.
 */
#define PE_SET_DPM_REQUEST(port, req) (pe[port].dpm_request |=  (req))
#define PE_CLR_DPM_REQUEST(port, req) (pe[port].dpm_request &= ~(req))
#define PE_CHK_DPM_REQUEST(port, req) (pe[port].dpm_request &   (req))

/* Policy Engine Layer Flags */
#define PE_FLAGS_PD_CONNECTION                  BIT(0)
#define PE_FLAGS_ACCEPT                         BIT(1)
#define PE_FLAGS_PS_READY                       BIT(2)
#define PE_FLAGS_PROTOCOL_ERROR                 BIT(3)
#define PE_FLAGS_MODAL_OPERATION                BIT(4)
#define PE_FLAGS_TX_COMPLETE                    BIT(5)
#define PE_FLAGS_MSG_RECEIVED                   BIT(6)
#define PE_FLAGS_HARD_RESET_PENDING             BIT(7)
#define PE_FLAGS_WAIT                           BIT(8)
#define PE_FLAGS_EXPLICIT_CONTRACT              BIT(9)
#define PE_FLAGS_SNK_WAIT_CAP_TIMEOUT           BIT(10)
#define PE_FLAGS_PS_TRANSITION_TIMEOUT          BIT(11)
#define PE_FLAGS_INTERRUPTIBLE_AMS              BIT(12)
#define PE_FLAGS_PS_RESET_COMPLETE              BIT(13)
#define PE_FLAGS_SEND_SVDM                      BIT(14)
#define PE_FLAGS_VCONN_SWAP_COMPLETE            BIT(15)
#define PE_FLAGS_DISCOVER_PORT_IDENTITY_DONE    BIT(16)
#define PE_FLAGS_DISCOVER_VDM_IDENTITY_DONE     BIT(17)
#define PE_FLAGS_RUN_SOURCE_START_TIMER         BIT(19)
#define PE_FLAGS_VDM_REQUEST_BUSY               BIT(20)
#define PE_FLAGS_VDM_REQUEST_NAKED              BIT(21)
#define PE_FLAGS_FAST_ROLE_SWAP_PATH            BIT(22)/* FRS/PRS Exec Path */
#define PE_FLAGS_FAST_ROLE_SWAP_ENABLED         BIT(23)/* FRS Listening State */
#define PE_FLAGS_FAST_ROLE_SWAP_SIGNALED        BIT(24)/* FRS PPC/TCPC Signal */

/* 6.7.3 Hard Reset Counter */
#define N_HARD_RESET_COUNT 2

/* 6.7.4 Capabilities Counter */
#define N_CAPS_COUNT 25

/* 6.7.5 Discover Identity Counter */
#define N_DISCOVER_IDENTITY_COUNT 20

#define TIMER_DISABLED 0xffffffffffffffff /* Unreachable time in future */

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
	PE_SRC_VDM_IDENTITY_REQUEST,
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
	PE_GIVE_BATTERY_CAP,
	PE_GIVE_BATTERY_STATUS,
	PE_DRS_EVALUATE_SWAP,
	PE_DRS_CHANGE,
	PE_DRS_SEND_SWAP,
	PE_PRS_SRC_SNK_EVALUATE_SWAP,
	PE_PRS_SRC_SNK_TRANSITION_TO_OFF,
	PE_PRS_SRC_SNK_WAIT_SOURCE_ON,
	PE_PRS_SRC_SNK_SEND_SWAP,
	PE_PRS_SNK_SRC_EVALUATE_SWAP,
	PE_PRS_SNK_SRC_TRANSITION_TO_OFF,
	PE_PRS_SNK_SRC_ASSERT_RP,
	PE_PRS_SNK_SRC_SOURCE_ON,
	PE_PRS_SNK_SRC_SEND_SWAP,
	PE_FRS_SNK_SRC_START_AMS,
	PE_VCS_EVALUATE_SWAP,
	PE_VCS_SEND_SWAP,
	PE_VCS_WAIT_FOR_VCONN_SWAP,
	PE_VCS_TURN_ON_VCONN_SWAP,
	PE_VCS_TURN_OFF_VCONN_SWAP,
	PE_VCS_SEND_PS_RDY_SWAP,
	PE_DO_PORT_DISCOVERY,
	PE_VDM_REQUEST,
	PE_VDM_ACKED,
	PE_VDM_RESPONSE,
	PE_HANDLE_CUSTOM_VDM_REQUEST,
	PE_WAIT_FOR_ERROR_RECOVERY,
	PE_BIST,
	PE_DR_SNK_GET_SINK_CAP,

	/* Super States */
	PE_PRS_FRS_SHARED,
};

/* Forward declare the full list of states. This is indexed by usb_pe_state */
static const struct usb_state pe_states[];

#ifdef CONFIG_COMMON_RUNTIME
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
	[PE_SRC_VDM_IDENTITY_REQUEST] = "PE_SRC_Vdm_Identity_Request",
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
	[PE_GIVE_BATTERY_CAP] = "PE_Give_Battery_Cap",
	[PE_GIVE_BATTERY_STATUS] = "PE_Give_Battery_Status",
	[PE_DRS_EVALUATE_SWAP] = "PE_DRS_Evaluate_Swap",
	[PE_DRS_CHANGE] = "PE_DRS_Change",
	[PE_DRS_SEND_SWAP] = "PE_DRS_Send_Swap",
	[PE_PRS_SRC_SNK_EVALUATE_SWAP] = "PE_PRS_SRC_SNK_Evaluate_Swap",
	[PE_PRS_SRC_SNK_TRANSITION_TO_OFF] = "PE_PRS_SRC_SNK_Transition_To_Off",
	[PE_PRS_SRC_SNK_WAIT_SOURCE_ON] = "PE_PRS_SRC_SNK_Wait_Source_On",
	[PE_PRS_SRC_SNK_SEND_SWAP] = "PE_PRS_SRC_SNK_Send_Swap",
	[PE_PRS_SNK_SRC_EVALUATE_SWAP] = "PE_PRS_SNK_SRC_Evaluate_Swap",
	[PE_PRS_SNK_SRC_TRANSITION_TO_OFF] = "PE_PRS_SNK_SRC_Transition_To_Off",
	[PE_PRS_SNK_SRC_ASSERT_RP] = "PE_PRS_SNK_SRC_Assert_Rp",
	[PE_PRS_SNK_SRC_SOURCE_ON] = "PE_PRS_SNK_SRC_Source_On",
	[PE_PRS_SNK_SRC_SEND_SWAP] = "PE_PRS_SNK_SRC_Send_Swap",
	[PE_FRS_SNK_SRC_START_AMS] = "PE_FRS_SNK_SRC_Start_Ams",
	[PE_VCS_EVALUATE_SWAP] = "PE_VCS_Evaluate_Swap",
	[PE_VCS_SEND_SWAP] = "PE_VCS_Send_Swap",
	[PE_VCS_WAIT_FOR_VCONN_SWAP] = "PE_VCS_Wait_For_Vconn_Swap",
	[PE_VCS_TURN_ON_VCONN_SWAP] = "PE_VCS_Turn_On_Vconn_Swap",
	[PE_VCS_TURN_OFF_VCONN_SWAP] = "PE_VCS_Turn_Off_Vconn_Swap",
	[PE_VCS_SEND_PS_RDY_SWAP] = "PE_VCS_Send_Ps_Rdy_Swap",
	[PE_DO_PORT_DISCOVERY] = "PE_Do_Port_Discovery",
	[PE_VDM_REQUEST] = "PE_VDM_Request",
	[PE_VDM_ACKED] = "PE_VDM_Acked",
	[PE_VDM_RESPONSE] = "PE_VDM_Response",
	[PE_HANDLE_CUSTOM_VDM_REQUEST] = "PE_Handle_Custom_Vdm_Request",
	[PE_WAIT_FOR_ERROR_RECOVERY] = "PE_Wait_For_Error_Recovery",
	[PE_BIST] = "PE_Bist",
	[PE_DR_SNK_GET_SINK_CAP] = "PE_DR_SNK_Get_Sink_Cap",
};
#endif

/*
 * NOTE:
 *	DO_PORT_DISCOVERY_START is not actually a vdm command. It is used
 *	to start the port partner discovery proccess.
 */
enum vdm_cmd {
	DO_PORT_DISCOVERY_START,
	DISCOVER_IDENTITY,
	DISCOVER_SVIDS,
	DISCOVER_MODES,
	ENTER_MODE,
	EXIT_MODE,
	ATTENTION,
};

enum port_partner {
	PORT,
	CABLE,
};

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
	/* saved data and power roles while communicating with a cable plug */
	enum pd_data_role saved_data_role;
	enum pd_power_role saved_power_role;
	/* state machine flags */
	uint32_t flags;
	/* Device Policy Manager Request */
	uint32_t dpm_request;
	/* state timeout timer */
	uint64_t timeout;
	/* last requested voltage PDO index */
	int requested_idx;

	/* Current limit / voltage based on the last request message */
	uint32_t curr_limit;
	uint32_t supply_voltage;

	/* state specific state machine variable */
	enum sub_state sub;

	/* VDO */

	/* PD_VDO_INVALID is used when there is an invalid VDO */
	int32_t active_cable_vdo1;
	int32_t active_cable_vdo2;
	int32_t passive_cable_vdo;
	int32_t ama_vdo;
	int32_t vpd_vdo;
	/* alternate mode policy*/
	struct pd_policy am_policy;

	/* VDM */
	enum port_partner partner_type;
	uint32_t vdm_cmd;
	uint32_t vdm_cnt;
	uint32_t vdm_data[VDO_HDR_SIZE + VDO_MAX_SIZE];

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
	 * whether a Cable Plug is PD Capable using SOP’.
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
	 * This timer is used by a UUT to ensure that a Continuous BIST Mode
	 * (i.e. BIST Carrier Mode) is exited in a timely fashion.
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
	 * These counter maintain a count of Messages sent to a Port and
	 * Cable Plug, respectively.
	 */
	uint32_t port_discover_identity_count;
	uint32_t cable_discover_identity_count;

	/* Last received source cap */
	uint32_t src_caps[PDO_MAX_OBJECTS];
	int src_cap_cnt;

} pe[CONFIG_USB_PD_PORT_MAX_COUNT];

/*
 * As a sink, this is the max voltage (in millivolts) we can request
 * before getting source caps
 */
static unsigned int max_request_mv = PD_MAX_VOLTAGE_MV;

/*
 * Private VDM utility functions
 */
#ifdef CONFIG_USB_PD_ALT_MODE_DFP
static int validate_mode_request(struct svdm_amode_data *modep,
						uint16_t svid, int opos);
static void dfp_consume_attention(int port, uint32_t *payload);
static void dfp_consume_identity(int port, int cnt, uint32_t *payload);
static void dfp_consume_svids(int port, int cnt, uint32_t *payload);
static int dfp_discover_modes(int port, uint32_t *payload);
static void dfp_consume_modes(int port, int cnt, uint32_t *payload);
static int get_mode_idx(int port, uint16_t svid);
static struct svdm_amode_data *get_modep(int port, uint16_t svid);
#endif

test_export_static enum usb_pe_state get_state_pe(const int port);
test_export_static void set_state_pe(const int port,
				     const enum usb_pe_state new_state);

static void pe_init(int port)
{
	pe[port].flags = 0;
	pe[port].dpm_request = 0;
	pe[port].source_cap_timer = TIMER_DISABLED;
	pe[port].no_response_timer = TIMER_DISABLED;
	pe[port].data_role = tc_get_data_role(port);

	tc_pd_connection(port, 0);

	if (tc_get_power_role(port) == PD_ROLE_SOURCE)
		set_state_pe(port, PE_SRC_STARTUP);
	else
		set_state_pe(port, PE_SNK_STARTUP);
}

int pe_is_running(int port)
{
	return local_state[port] == SM_RUN;
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
		if (PE_CHK_FLAG(port, PE_FLAGS_FAST_ROLE_SWAP_SIGNALED)) {
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
	pe[port].power_role = tc_get_power_role(port);

	if (pe[port].power_role == PD_ROLE_SOURCE)
		set_state_pe(port, PE_SRC_HARD_RESET_RECEIVED);
	else
		set_state_pe(port, PE_SNK_TRANSITION_TO_DEFAULT);
}

/*
 * pe_got_frs_signal
 *
 * Called by the handler that detects the FRS signal in order to
 * switch PE states to complete the FRS that the hardware has
 * started.
 */
void pe_got_frs_signal(int port)
{
	PE_SET_FLAG(port, PE_FLAGS_FAST_ROLE_SWAP_SIGNALED);
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

	if (IS_ENABLED(CONFIG_USB_TYPEC_PD_FAST_ROLE_SWAP)) {
		int current = PE_CHK_FLAG(port,
					  PE_FLAGS_FAST_ROLE_SWAP_ENABLED);

		/* Request an FRS change, only if the state has changed */
		if (!!current != !!enable) {
			tcpm_set_frs_enable(port, enable);

			if (enable)
				PE_SET_FLAG(port,
					    PE_FLAGS_FAST_ROLE_SWAP_ENABLED);
			else
				PE_CLR_FLAG(port,
					    PE_FLAGS_FAST_ROLE_SWAP_ENABLED);
		}
	}
}

static void pe_invalidate_explicit_contract(int port)
{
	pe_set_frs_enable(port, 0);
	PE_CLR_FLAG(port, PE_FLAGS_EXPLICIT_CONTRACT);
}

void pe_report_error(int port, enum pe_error e)
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

	if (get_state_pe(port) == PE_SRC_SEND_CAPABILITIES ||
			get_state_pe(port) == PE_SRC_TRANSITION_SUPPLY ||
			get_state_pe(port) == PE_PRS_SRC_SNK_WAIT_SOURCE_ON ||
			get_state_pe(port) == PE_SRC_DISABLED ||
			get_state_pe(port) == PE_SRC_DISCOVERY ||
			get_state_pe(port) == PE_VDM_REQUEST) {
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
		set_state_pe(port, PE_SEND_SOFT_RESET);
	}
	/*
	 * Transition to PE_Snk_Ready or PE_Src_Ready by a Protocol
	 * Error during an Interruptible AMS.
	 */
	else {
		PE_SET_FLAG(port, PE_FLAGS_PROTOCOL_ERROR);
		if (pe[port].power_role == PD_ROLE_SINK)
			set_state_pe(port, PE_SNK_READY);
		else
			set_state_pe(port, PE_SRC_READY);
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
	if (get_state_pe(port) == PE_SRC_READY ||
			get_state_pe(port) == PE_SNK_READY)
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

void pe_send_vdm(int port, uint32_t vid, int cmd, const uint32_t *data,
						int count)
{
	pe[port].partner_type = PORT;

	/* Copy VDM Header */
	pe[port].vdm_data[0] = VDO(vid, ((vid & USB_SID_PD) == USB_SID_PD) ?
				1 : (PD_VDO_CMD(cmd) <= CMD_ATTENTION),
				VDO_SVDM_VERS(1) | cmd);

	/* Copy Data after VDM Header */
	memcpy((pe[port].vdm_data + 1), data, count);

	pe[port].vdm_cnt = count + 1;

	PE_SET_FLAG(port, PE_FLAGS_SEND_SVDM);
	task_wake(PD_PORT_TO_TASK_ID(port));
}

void pe_exit_dp_mode(int port)
{
	/* This should only be called from the PD task */
	assert(port == TASK_ID_TO_PD_PORT(task_get_current()));

	if (IS_ENABLED(CONFIG_USB_PD_ALT_MODE_DFP)) {
		int opos = pd_alt_mode(port, USB_SID_DISPLAYPORT);

		if (opos <= 0)
			return;

		CPRINTS("C%d Exiting DP mode", port);
		if (!pd_dfp_exit_mode(port, USB_SID_DISPLAYPORT, opos))
			return;

		pe_send_vdm(port, USB_SID_DISPLAYPORT,
				CMD_EXIT_MODE | VDO_OPOS(opos), NULL, 0);
	}
}

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

/* Get the previous TypeC state. */
static enum usb_pe_state get_last_state_pe(const int port)
{
	return pe[port].ctx.previous - &pe_states[0];
}

static void print_current_state(const int port)
{
	const char *mode = "";

	if (PE_CHK_FLAG(port, PE_FLAGS_FAST_ROLE_SWAP_PATH))
		mode = " FRS-MODE";

	CPRINTS("C%d: %s%s", port, pe_state_names[get_state_pe(port)], mode);
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
		prl_send_ctrl_msg(port, TCPC_TX_SOP, PD_CTRL_REJECT);
	}

	emsg[port].len = src_pdo_cnt * 4;
	memcpy(emsg[port].buf, (uint8_t *)src_pdo, emsg[port].len);

	prl_send_data_msg(port, TCPC_TX_SOP, PD_DATA_SOURCE_CAP);
}

/*
 * Request desired charge voltage from source.
 */
static void pe_send_request_msg(int port)
{
	uint32_t rdo;
	uint32_t curr_limit;
	uint32_t supply_voltage;
	int charging;
	int max_request_allowed;

	if (IS_ENABLED(CONFIG_CHARGE_MANAGER))
		charging = (charge_manager_get_active_charge_port() == port);
	else
		charging = 1;

	if (IS_ENABLED(CONFIG_USB_PD_CHECK_MAX_REQUEST_ALLOWED))
		max_request_allowed = pd_is_max_request_allowed();
	else
		max_request_allowed = 1;

	/* Build and send request RDO */
	/*
	 * If this port is not actively charging or we are not allowed to
	 * request the max voltage, then select vSafe5V
	 */
	pd_build_request(pe[port].src_cap_cnt, pe[port].src_caps,
		pe[port].vpd_vdo, &rdo, &curr_limit,
		&supply_voltage, charging && max_request_allowed ?
		PD_REQUEST_MAX : PD_REQUEST_VSAFE5V, max_request_mv);

	CPRINTF("C%d Req [%d] %dmV %dmA", port, RDO_POS(rdo),
					supply_voltage, curr_limit);
	if (rdo & RDO_CAP_MISMATCH)
		CPRINTF(" Mismatch");
	CPRINTF("\n");

	pe[port].curr_limit = curr_limit;
	pe[port].supply_voltage = supply_voltage;

	emsg[port].len = 4;

	memcpy(emsg[port].buf, (uint8_t *)&rdo, emsg[port].len);
	prl_send_data_msg(port, TCPC_TX_SOP, PD_DATA_REQUEST);
}

static void pe_update_pdo_flags(int port, uint32_t pdo)
{
#ifdef CONFIG_CHARGE_MANAGER
#ifdef CONFIG_USB_PD_ALT_MODE_DFP
	int charge_whitelisted =
		(tc_get_power_role(port) == PD_ROLE_SINK &&
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

	if (pdo & PDO_FIXED_EXTERNAL)
		tc_partner_extpower(port, 1);
	else
		tc_partner_extpower(port, 0);

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
	 * from it) if it does not support power swap, or if it is externally
	 * powered, or if we are a sink and the device identity matches a
	 * charging white-list.
	 */
	if (!(pdo & PDO_FIXED_DUAL_ROLE) || (pdo & PDO_FIXED_EXTERNAL) ||
		charge_whitelisted)
		charge_manager_update_dualrole(port, CAP_DEDICATED);
	else
		charge_manager_update_dualrole(port, CAP_DUALROLE);
#endif
}

int pd_board_check_request(uint32_t rdo, int pdo_cnt)
{
	int idx = RDO_POS(rdo);

	/* Check for invalid index */
	return (!idx || idx > pdo_cnt) ?
		EC_ERROR_INVAL : EC_SUCCESS;
}

static void pe_prl_execute_hard_reset(int port)
{
	prl_execute_hard_reset(port);
}

/**
 * PE_SRC_Startup
 */
static void pe_src_startup_entry(int port)
{
	print_current_state(port);

	/* Initialize VDOs to default values */
	pe[port].active_cable_vdo1 = PD_VDO_INVALID;
	pe[port].active_cable_vdo2 = PD_VDO_INVALID;
	pe[port].passive_cable_vdo = PD_VDO_INVALID;
	pe[port].ama_vdo = PD_VDO_INVALID;
	pe[port].vpd_vdo = PD_VDO_INVALID;

	/* Reset CapsCounter */
	pe[port].caps_counter = 0;

	/* Reset the protocol layer */
	prl_reset(port);

	/* Set initial data role */
	pe[port].data_role = tc_get_data_role(port);

	/* Set initial power role */
	pe[port].power_role = PD_ROLE_SOURCE;

	/* Clear explicit contract. */
	pe_invalidate_explicit_contract(port);

	pe[port].cable_discover_identity_count = 0;
	pe[port].port_discover_identity_count = 0;

	if (PE_CHK_FLAG(port, PE_FLAGS_RUN_SOURCE_START_TIMER)) {
		PE_CLR_FLAG(port, PE_FLAGS_RUN_SOURCE_START_TIMER);

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
	}
}

static void pe_src_startup_run(int port)
{
	/* Wait until protocol layer is running */
	if (!prl_is_running(port))
		return;

	if (get_time().val > pe[port].swap_source_start_timer)
		set_state_pe(port, PE_SRC_VDM_IDENTITY_REQUEST);
}

/**
 * PE_SRC_VDM_Identity_Request
 */
static void pe_src_vdm_identity_request_entry(int port)
{
	print_current_state(port);
}

static void pe_src_vdm_identity_request_run(int port)
{
	/*
	 * Discover identity of the Cable Plug
	 */
	if (!PE_CHK_FLAG(port, PE_FLAGS_DISCOVER_VDM_IDENTITY_DONE) &&
			tc_is_vconn_src(port) &&
			pe[port].cable_discover_identity_count <
						N_DISCOVER_IDENTITY_COUNT) {
		pe[port].cable_discover_identity_count++;

		pe[port].partner_type = CABLE;
		pe[port].vdm_cmd = DISCOVER_IDENTITY;
		pe[port].vdm_data[0] = VDO(USB_SID_PD, 1, /* structured */
			VDO_SVDM_VERS(1) | DISCOVER_IDENTITY);
		pe[port].vdm_cnt = 1;

		set_state_pe(port, PE_VDM_REQUEST);
	} else {
		set_state_pe(port, PE_SRC_SEND_CAPABILITIES);
	}
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
	 * The SourceCapabilityTimer Shall continue to run during cable
	 * identity discover and Shall Not be initialized on re-entry
	 * to PE_SRC_Discovery.
	 */
	if (get_last_state_pe(port) != PE_VDM_REQUEST)
		pe[port].source_cap_timer =
				get_time().val + PD_T_SEND_SOURCE_CAP;
}

static void pe_src_discovery_run(int port)
{
	/*
	 * A VCONN or Charge-Through VCONN Powered Device was detected.
	 */
	if (pe[port].vpd_vdo >= 0 && VPD_VDO_CTS(pe[port].vpd_vdo)) {
		set_state_pe(port, PE_SRC_DISABLED);
		return;
	}

	/*
	 * Transition to the PE_SRC_Send_Capabilities state when:
	 *   1) The SourceCapabilityTimer times out and
	 *      CapsCounter ≤ nCapsCount.
	 *
	 * Transition to the PE_SRC_Disabled state when:
	 *   1) The Port Partners are not presently PD Connected
	 *   2) And the SourceCapabilityTimer times out
	 *   3) And CapsCounter > nCapsCount.
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

	/*
	 * Discover identity of the Cable Plug
	 */
	if (!PE_CHK_FLAG(port, PE_FLAGS_DISCOVER_VDM_IDENTITY_DONE) &&
	pe[port].cable_discover_identity_count < N_DISCOVER_IDENTITY_COUNT) {
		set_state_pe(port, PE_SRC_VDM_IDENTITY_REQUEST);
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

	/*
	 * Clear PE_FLAGS_INTERRUPTIBLE_AMS flag if it was set
	 * in the src_discovery state
	 */
	PE_CLR_FLAG(port, PE_FLAGS_INTERRUPTIBLE_AMS);
}

static void pe_src_send_capabilities_run(int port)
{
	/*
	 * If a GoodCRC Message is received then the Policy Engine Shall:
	 *  1) Stop the NoResponseTimer.
	 *  2) Reset the HardResetCounter and CapsCounter to zero.
	 *  3) Initialize and run the SenderResponseTimer.
	 */
	if (PE_CHK_FLAG(port, PE_FLAGS_TX_COMPLETE)) {
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
	if (PE_CHK_FLAG(port, PE_FLAGS_MSG_RECEIVED)) {
		PE_CLR_FLAG(port, PE_FLAGS_MSG_RECEIVED);

		/*
		 * Request Message Received?
		 */
		if (PD_HEADER_CNT(emsg[port].header) > 0 &&
			PD_HEADER_TYPE(emsg[port].header) == PD_DATA_REQUEST) {

			/*
			 * Set to highest revision supported by both
			 * ports.
			 */
			prl_set_rev(port,
				(PD_HEADER_REV(emsg[port].header) > PD_REV30) ?
				PD_REV30 : PD_HEADER_REV(emsg[port].header));

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
			if (pe[port].power_role == PD_ROLE_SINK)
				set_state_pe(port, PE_SNK_READY);
			else
				set_state_pe(port, PE_SRC_READY);
		else
			set_state_pe(port, PE_SEND_SOFT_RESET);
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
	payload = *(uint32_t *)(&emsg[port].buf);

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
		prl_send_ctrl_msg(port, TCPC_TX_SOP, PD_CTRL_ACCEPT);
	} else {
		prl_send_ctrl_msg(port, TCPC_TX_SOP, PD_CTRL_GOTO_MIN);
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
			PE_SET_FLAG(port, PE_FLAGS_EXPLICIT_CONTRACT);
			set_state_pe(port, PE_SRC_READY);
		} else {
			/* NOTE: First pass through this code block */
			/* Send PS_RDY message */
			prl_send_ctrl_msg(port, TCPC_TX_SOP, PD_CTRL_PS_RDY);
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

	/*
	 * If the transition into PE_SRC_Ready is the result of Protocol Error
	 * that has not caused a Soft Reset (see Section 8.3.3.4.1) then the
	 * notification to the Protocol Layer of the end of the AMS Shall Not
	 * be sent since there is a Message to be processed.
	 */
	if (PE_CHK_FLAG(port, PE_FLAGS_PROTOCOL_ERROR)) {
		PE_CLR_FLAG(port, PE_FLAGS_PROTOCOL_ERROR);
	} else {
		PE_CLR_FLAG(port, PE_FLAGS_INTERRUPTIBLE_AMS);
		prl_end_ams(port);
	}

	/*
	 * Do port partner discovery
	 */
	if (!PE_CHK_FLAG(port, PE_FLAGS_MODAL_OPERATION |
				PE_FLAGS_DISCOVER_PORT_IDENTITY_DONE) &&
				pe[port].port_discover_identity_count <=
						N_DISCOVER_IDENTITY_COUNT) {
		pe[port].discover_identity_timer =
				get_time().val + PD_T_DISCOVER_IDENTITY;
	} else {
		PE_SET_FLAG(port, PE_FLAGS_DISCOVER_PORT_IDENTITY_DONE);
		pe[port].discover_identity_timer = TIMER_DISABLED;
	}

	/* NOTE: PPS Implementation should be added here. */

}

static void pe_src_ready_run(int port)
{
	uint32_t payload;
	uint8_t type;
	uint8_t cnt;
	uint8_t ext;

	/*
	 * Start Port Discovery when:
	 *   1) The DiscoverIdentityTimer times out.
	 */
	if (get_time().val > pe[port].discover_identity_timer) {
		pe[port].port_discover_identity_count++;
		pe[port].vdm_cmd = DO_PORT_DISCOVERY_START;
		PE_CLR_FLAG(port, PE_FLAGS_VDM_REQUEST_NAKED |
						PE_FLAGS_VDM_REQUEST_BUSY);
		set_state_pe(port, PE_DO_PORT_DISCOVERY);
		return;
	}

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
		if (PE_CHK_DPM_REQUEST(port, DPM_REQUEST_DR_SWAP)) {
			PE_CLR_DPM_REQUEST(port, DPM_REQUEST_DR_SWAP);
			if (PE_CHK_FLAG(port, PE_FLAGS_MODAL_OPERATION))
				set_state_pe(port, PE_SRC_HARD_RESET);
			else
				set_state_pe(port, PE_DRS_SEND_SWAP);
		} else if (PE_CHK_DPM_REQUEST(port, DPM_REQUEST_PR_SWAP)) {
			PE_CLR_DPM_REQUEST(port, DPM_REQUEST_PR_SWAP);
			set_state_pe(port, PE_PRS_SRC_SNK_SEND_SWAP);
		} else if (PE_CHK_DPM_REQUEST(port, DPM_REQUEST_VCONN_SWAP)) {
			PE_CLR_DPM_REQUEST(port, DPM_REQUEST_VCONN_SWAP);
			set_state_pe(port, PE_VCS_SEND_SWAP);
		} else if (PE_CHK_DPM_REQUEST(port, DPM_REQUEST_GOTO_MIN)) {
			PE_CLR_DPM_REQUEST(port, DPM_REQUEST_GOTO_MIN);
			set_state_pe(port, PE_SRC_TRANSITION_SUPPLY);
		} else if (PE_CHK_DPM_REQUEST(port,
						DPM_REQUEST_SRC_CAP_CHANGE)) {
			PE_CLR_DPM_REQUEST(port, DPM_REQUEST_SRC_CAP_CHANGE);
			set_state_pe(port, PE_SRC_SEND_CAPABILITIES);
		} else if (PE_CHK_DPM_REQUEST(port, DPM_REQUEST_SEND_PING)) {
			PE_CLR_DPM_REQUEST(port, DPM_REQUEST_SEND_PING);
			set_state_pe(port, PE_SRC_PING);
		} else if (PE_CHK_DPM_REQUEST(port,
					DPM_REQUEST_DISCOVER_IDENTITY)) {
			PE_CLR_DPM_REQUEST(port, DPM_REQUEST_DISCOVER_IDENTITY);

			pe[port].partner_type = CABLE;
			pe[port].vdm_cmd = DISCOVER_IDENTITY;
			pe[port].vdm_data[0] = VDO(
					USB_SID_PD,
					1, /* structured */
					VDO_SVDM_VERS(1) | DISCOVER_IDENTITY);
			pe[port].vdm_cnt = 1;
			set_state_pe(port, PE_VDM_REQUEST);
		}
		return;
	}

	/*
	 * Handle Source Requests
	 */
	if (PE_CHK_FLAG(port, PE_FLAGS_MSG_RECEIVED)) {
		PE_CLR_FLAG(port, PE_FLAGS_MSG_RECEIVED);

		type = PD_HEADER_TYPE(emsg[port].header);
		cnt = PD_HEADER_CNT(emsg[port].header);
		ext = PD_HEADER_EXT(emsg[port].header);
		payload = *(uint32_t *)emsg[port].buf;

		/* Extended Message Requests */
		if (ext > 0) {
			switch (type) {
			case PD_EXT_GET_BATTERY_CAP:
				set_state_pe(port, PE_GIVE_BATTERY_CAP);
				break;
			case PD_EXT_GET_BATTERY_STATUS:
				set_state_pe(port, PE_GIVE_BATTERY_STATUS);
				break;
			default:
				set_state_pe(port, PE_SEND_NOT_SUPPORTED);
			}
		}
		/* Data Message Requests */
		else if (cnt > 0) {
			switch (type) {
			case PD_DATA_REQUEST:
				set_state_pe(port, PE_SRC_NEGOTIATE_CAPABILITY);
				break;
			case PD_DATA_SINK_CAP:
				break;
			case PD_DATA_VENDOR_DEF:
				if (PD_HEADER_TYPE(emsg[port].header) ==
							PD_DATA_VENDOR_DEF) {
					if (PD_VDO_SVDM(payload)) {
						set_state_pe(port,
							PE_VDM_RESPONSE);
					} else
						set_state_pe(port,
						PE_HANDLE_CUSTOM_VDM_REQUEST);
				}
				break;
			case PD_DATA_BIST:
				set_state_pe(port, PE_BIST);
				break;
			default:
				set_state_pe(port, PE_SEND_NOT_SUPPORTED);
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
				break;
			case PD_CTRL_GET_SINK_CAP:
				set_state_pe(port, PE_SNK_GIVE_SINK_CAP);
				break;
			case PD_CTRL_GOTO_MIN:
				break;
			case PD_CTRL_PR_SWAP:
				set_state_pe(port,
					PE_PRS_SRC_SNK_EVALUATE_SWAP);
				break;
			case PD_CTRL_DR_SWAP:
				if (PE_CHK_FLAG(port,
						PE_FLAGS_MODAL_OPERATION)) {
					set_state_pe(port, PE_SRC_HARD_RESET);
					return;
				}

				set_state_pe(port, PE_DRS_EVALUATE_SWAP);
				break;
			case PD_CTRL_VCONN_SWAP:
				set_state_pe(port, PE_VCS_EVALUATE_SWAP);
				break;
			default:
				set_state_pe(port, PE_SEND_NOT_SUPPORTED);
			}
		}
	}
}

static void pe_src_ready_exit(int port)
{
	PE_CLR_FLAG(port, PE_FLAGS_TX_COMPLETE);

	/*
	 * If the Source is initiating an AMS then the Policy Engine Shall
	 * notify the Protocol Layer that the first Message in an AMS will
	 * follow.
	 */
	if (!PE_CHK_FLAG(port, PE_FLAGS_INTERRUPTIBLE_AMS))
		prl_start_ams(port);
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

	prl_send_ctrl_msg(port, TCPC_TX_SOP, PD_CTRL_REJECT);
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
	tc_hard_reset(port);
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
	pe[port].data_role = tc_get_data_role(port);

	/* Set initial power role */
	pe[port].power_role = PD_ROLE_SINK;

	/* Clear explicit contract */
	pe_invalidate_explicit_contract(port);
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

		type = PD_HEADER_TYPE(emsg[port].header);
		cnt = PD_HEADER_CNT(emsg[port].header);
		ext = PD_HEADER_EXT(emsg[port].header);

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
	uint32_t *pdo = (uint32_t *)emsg[port].buf;
	uint32_t header = emsg[port].header;
	uint32_t num = emsg[port].len >> 2;
	int i;

	print_current_state(port);

	/* Reset Hard Reset counter to zero */
	pe[port].hard_reset_counter = 0;

	/* Set to highest revision supported by both ports. */
	prl_set_rev(port, (PD_HEADER_REV(header) > PD_REV30) ?
					PD_REV30 : PD_HEADER_REV(header));

	pe[port].src_cap_cnt = num;

	for (i = 0; i < num; i++)
		pe[port].src_caps[i] = *pdo++;

	/* src cap 0 should be fixed PDO */
	pe_update_pdo_flags(port, pdo[0]);

	/* Evaluate the options based on supplied capabilities */
	pd_process_source_cap(port, pe[port].src_cap_cnt, pe[port].src_caps);

	/* We are PD Connected */
	PE_SET_FLAG(port, PE_FLAGS_PD_CONNECTION);
	tc_pd_connection(port, 1);

	/* Device Policy Response Received */
	set_state_pe(port, PE_SNK_SELECT_CAPABILITY);
}

/**
 * PE_SNK_Select_Capability State
 */
static void pe_snk_select_capability_entry(int port)
{
	print_current_state(port);

	pe[port].sender_response_timer = TIMER_DISABLED;
	/* Send Request */
	pe_send_request_msg(port);
}

static void pe_snk_select_capability_run(int port)
{
	uint8_t type;
	uint8_t cnt;

	/* Wait until message is sent */
	if (PE_CHK_FLAG(port, PE_FLAGS_TX_COMPLETE)) {
		PE_CLR_FLAG(port, PE_FLAGS_TX_COMPLETE);

		/* Initialize and run SenderResponseTimer */
		pe[port].sender_response_timer =
					get_time().val + PD_T_SENDER_RESPONSE;
	}

	if (PE_CHK_FLAG(port, PE_FLAGS_MSG_RECEIVED)) {
		PE_CLR_FLAG(port, PE_FLAGS_MSG_RECEIVED);
		type = PD_HEADER_TYPE(emsg[port].header);
		cnt = PD_HEADER_CNT(emsg[port].header);

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
				PE_SET_FLAG(port, PE_FLAGS_EXPLICIT_CONTRACT);
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
		if ((PD_HEADER_CNT(emsg[port].header) == 0) &&
			   (PD_HEADER_TYPE(emsg[port].header) ==
			   PD_CTRL_PS_RDY)) {
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

	PE_CLR_FLAG(port, PE_FLAGS_INTERRUPTIBLE_AMS);
	prl_end_ams(port);

	/*
	 * On entry to the PE_SNK_Ready state as the result of a wait, then do
	 * the following:
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
	 * Do port partner discovery
	 */
	if (!PE_CHK_FLAG(port, PE_FLAGS_MODAL_OPERATION |
				PE_FLAGS_DISCOVER_PORT_IDENTITY_DONE) &&
				pe[port].port_discover_identity_count <=
						N_DISCOVER_IDENTITY_COUNT) {
		pe[port].discover_identity_timer =
			get_time().val + PD_T_DISCOVER_IDENTITY;
	} else {
		PE_SET_FLAG(port, PE_FLAGS_DISCOVER_PORT_IDENTITY_DONE);
		pe[port].discover_identity_timer = TIMER_DISABLED;
	}

	/*
	 * On entry to the PE_SNK_Ready state if the current Explicit Contract
	 * is for a PPS APDO, then do the following:
	 *  1) Initialize and run the SinkPPSPeriodicTimer.
	 *  NOTE: PPS Implementation should be added here.
	 */
}

static void pe_snk_ready_run(int port)
{
	uint32_t payload;
	uint8_t type;
	uint8_t cnt;
	uint8_t ext;

	if (get_time().val > pe[port].sink_request_timer) {
		set_state_pe(port, PE_SNK_SELECT_CAPABILITY);
		return;
	}

	/*
	 * Start Port Discovery when:
	 *   1) The DiscoverIdentityTimer times out.
	 */
	if (get_time().val > pe[port].discover_identity_timer) {
		pe[port].port_discover_identity_count++;
		pe[port].vdm_cmd = DO_PORT_DISCOVERY_START;
		PE_CLR_FLAG(port, PE_FLAGS_VDM_REQUEST_NAKED |
						PE_FLAGS_VDM_REQUEST_BUSY);
		set_state_pe(port, PE_DO_PORT_DISCOVERY);
		return;
	}

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
		if (PE_CHK_DPM_REQUEST(port, DPM_REQUEST_DR_SWAP)) {
			PE_CLR_DPM_REQUEST(port, DPM_REQUEST_DR_SWAP);
			if (PE_CHK_FLAG(port, PE_FLAGS_MODAL_OPERATION))
				set_state_pe(port, PE_SNK_HARD_RESET);
			else
				set_state_pe(port, PE_DRS_SEND_SWAP);
		} else if (PE_CHK_DPM_REQUEST(port, DPM_REQUEST_PR_SWAP)) {
			PE_CLR_DPM_REQUEST(port, DPM_REQUEST_PR_SWAP);
			set_state_pe(port, PE_PRS_SNK_SRC_SEND_SWAP);
		} else if (PE_CHK_DPM_REQUEST(port, DPM_REQUEST_VCONN_SWAP)) {
			PE_CLR_DPM_REQUEST(port, DPM_REQUEST_VCONN_SWAP);
			set_state_pe(port, PE_VCS_SEND_SWAP);
		} else if (PE_CHK_DPM_REQUEST(port, DPM_REQUEST_SOURCE_CAP)) {
			PE_CLR_DPM_REQUEST(port, DPM_REQUEST_SOURCE_CAP);
			set_state_pe(port, PE_SNK_GET_SOURCE_CAP);
		} else if (PE_CHK_DPM_REQUEST(port,
					DPM_REQUEST_NEW_POWER_LEVEL)) {
			PE_CLR_DPM_REQUEST(port, DPM_REQUEST_NEW_POWER_LEVEL);
			set_state_pe(port, PE_SNK_SELECT_CAPABILITY);
		} else if (PE_CHK_DPM_REQUEST(port,
					DPM_REQUEST_DISCOVER_IDENTITY)) {
			PE_CLR_DPM_REQUEST(port,
					   DPM_REQUEST_DISCOVER_IDENTITY);

			pe[port].partner_type = CABLE;
			pe[port].vdm_cmd = DISCOVER_IDENTITY;
			pe[port].vdm_data[0] = VDO(
				USB_SID_PD,
				1, /* structured */
				VDO_SVDM_VERS(1) | DISCOVER_IDENTITY);
			pe[port].vdm_cnt = 1;

			set_state_pe(port, PE_VDM_REQUEST);
		} else if (PE_CHK_DPM_REQUEST(port,
					      DPM_REQUEST_GET_SNK_CAPS)) {
			PE_CLR_DPM_REQUEST(port, DPM_REQUEST_GET_SNK_CAPS);
			set_state_pe(port, PE_DR_SNK_GET_SINK_CAP);
		}
		return;
	}

	/*
	 * Handle Source Requests
	 */
	if (PE_CHK_FLAG(port, PE_FLAGS_MSG_RECEIVED)) {
		PE_CLR_FLAG(port, PE_FLAGS_MSG_RECEIVED);

		type = PD_HEADER_TYPE(emsg[port].header);
		cnt = PD_HEADER_CNT(emsg[port].header);
		ext = PD_HEADER_EXT(emsg[port].header);
		payload = *(uint32_t *)emsg[port].buf;

		/* Extended Message Request */
		if (ext > 0) {
			switch (type) {
			case PD_EXT_GET_BATTERY_CAP:
				set_state_pe(port, PE_GIVE_BATTERY_CAP);
				break;
			case PD_EXT_GET_BATTERY_STATUS:
				set_state_pe(port, PE_GIVE_BATTERY_STATUS);
				break;
			default:
				set_state_pe(port, PE_SEND_NOT_SUPPORTED);
			}
		}
		/* Data Messages */
		else if (cnt > 0) {
			switch (type) {
			case PD_DATA_SOURCE_CAP:
				set_state_pe(port,
					PE_SNK_EVALUATE_CAPABILITY);
				break;
			case PD_DATA_VENDOR_DEF:
				if (PD_HEADER_TYPE(emsg[port].header) ==
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
				set_state_pe(port, PE_BIST);
				break;
			default:
				set_state_pe(port, PE_SEND_NOT_SUPPORTED);
			}
		}
		/* Control Messages */
		else {
			switch (type) {
			case PD_CTRL_GOOD_CRC:
				/* Do nothing */
				break;
			case PD_CTRL_PING:
				/* Do noghing */
				break;
			case PD_CTRL_GET_SOURCE_CAP:
				set_state_pe(port, PE_SNK_GET_SOURCE_CAP);
				break;
			case PD_CTRL_GET_SINK_CAP:
				set_state_pe(port, PE_SNK_GIVE_SINK_CAP);
				break;
			case PD_CTRL_GOTO_MIN:
				set_state_pe(port, PE_SNK_TRANSITION_SINK);
				break;
			case PD_CTRL_PR_SWAP:
				set_state_pe(port,
						PE_PRS_SNK_SRC_EVALUATE_SWAP);
				break;
			case PD_CTRL_DR_SWAP:
				if (PE_CHK_FLAG(port, PE_FLAGS_MODAL_OPERATION))
					set_state_pe(port, PE_SNK_HARD_RESET);
				else
					set_state_pe(port,
							PE_DRS_EVALUATE_SWAP);
				break;
			case PD_CTRL_VCONN_SWAP:
				set_state_pe(port, PE_VCS_EVALUATE_SWAP);
				break;
			case PD_CTRL_NOT_SUPPORTED:
				/* Do nothing */
				break;
			default:
				set_state_pe(port, PE_SEND_NOT_SUPPORTED);
			}
		}
	}
}

static void pe_snk_ready_exit(int port)
{
	if (!PE_CHK_FLAG(port, PE_FLAGS_INTERRUPTIBLE_AMS))
		prl_start_ams(port);
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

	PE_CLR_FLAG(port, PE_FLAGS_SNK_WAIT_CAP_TIMEOUT);

	/* Request the generation of Hard Reset Signaling by the PHY Layer */
	pe_prl_execute_hard_reset(port);

	/* Increment the HardResetCounter */
	pe[port].hard_reset_counter++;

	/*
	 * Transition the Sink’s power supply to the new power level if
	 * PSTransistionTimer timeout occurred.
	 */
	if (PE_CHK_FLAG(port, PE_FLAGS_PS_TRANSITION_TIMEOUT)) {
		PE_SET_FLAG(port, PE_FLAGS_PS_TRANSITION_TIMEOUT);

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

	tc_hard_reset(port);
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
	emsg[port].len = 0;
	prl_send_ctrl_msg(port, TCPC_TX_SOP, PD_CTRL_GET_SOURCE_CAP);
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

	/* Reset Protocol Layer */
	prl_reset(port);

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
		/* Send Soft Reset message */
		prl_send_ctrl_msg(port, TCPC_TX_SOP, PD_CTRL_SOFT_RESET);

		/* Initialize and run SenderResponseTimer */
		pe[port].sender_response_timer =
					get_time().val + PD_T_SENDER_RESPONSE;
	}

	/*
	 * Transition to PE_SNK_Hard_Reset or PE_SRC_Hard_Reset on Sender
	 * Response Timer Timeout or Protocol Layer or Protocol Error
	 */
	if (get_time().val > pe[port].sender_response_timer ||
			PE_CHK_FLAG(port, PE_FLAGS_PROTOCOL_ERROR)) {
		PE_CLR_FLAG(port, PE_FLAGS_PROTOCOL_ERROR);

		if (pe[port].power_role == PD_ROLE_SINK)
			set_state_pe(port, PE_SRC_HARD_RESET);
		else
			set_state_pe(port, PE_SRC_HARD_RESET);
		return;
	}

	/*
	 * Transition to the PE_SNK_Send_Capabilities or
	 * PE_SRC_Send_Capabilities state when:
	 *   1) An Accept Message has been received.
	 */
	if (PE_CHK_FLAG(port, PE_FLAGS_MSG_RECEIVED)) {
		PE_CLR_FLAG(port, PE_FLAGS_MSG_RECEIVED);

		type = PD_HEADER_TYPE(emsg[port].header);
		cnt = PD_HEADER_CNT(emsg[port].header);
		ext = PD_HEADER_EXT(emsg[port].header);

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
}

static void pe_send_soft_reset_exit(int port)
{
	/* Clear TX Complete Flag */
	PE_CLR_FLAG(port, PE_FLAGS_TX_COMPLETE);
}

/**
 * PE_SNK_Soft_Reset and PE_SNK_Soft_Reset
 */
static void pe_soft_reset_entry(int port)
{
	print_current_state(port);

	pe[port].sender_response_timer = TIMER_DISABLED;

	prl_send_ctrl_msg(port, TCPC_TX_SOP, PD_CTRL_ACCEPT);
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
 */
static void pe_send_not_supported_entry(int port)
{
	print_current_state(port);

	/* Request the Protocol Layer to send a Not_Supported Message. */
	if (prl_get_rev(port) > PD_REV20)
		prl_send_ctrl_msg(port, TCPC_TX_SOP, PD_CTRL_NOT_SUPPORTED);
	else
		prl_send_ctrl_msg(port, TCPC_TX_SOP, PD_CTRL_REJECT);
}

static void pe_send_not_supported_run(int port)
{
	if (PE_CHK_FLAG(port, PE_FLAGS_TX_COMPLETE)) {
		PE_CLR_FLAG(port, PE_FLAGS_TX_COMPLETE);

		if (pe[port].power_role == PD_ROLE_SOURCE)
			set_state_pe(port, PE_SRC_READY);
		else
			set_state_pe(port, PE_SNK_READY);
	}
}

/**
 * PE_SRC_Ping
 */
static void pe_src_ping_entry(int port)
{
	print_current_state(port);
	prl_send_ctrl_msg(port, TCPC_TX_SOP, PD_CTRL_PING);
}

static void pe_src_ping_run(int port)
{
	if (PE_CHK_FLAG(port, PE_FLAGS_TX_COMPLETE)) {
		PE_CLR_FLAG(port, PE_FLAGS_TX_COMPLETE);
		set_state_pe(port, PE_SRC_READY);
	}
}

/**
 * PE_Give_Battery_Cap
 */
static void pe_give_battery_cap_entry(int port)
{
	uint32_t payload = *(uint32_t *)(&emsg[port].buf);
	uint16_t *msg = (uint16_t *)emsg[port].buf;

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
	emsg[port].len = 9;

	prl_send_ext_data_msg(port, TCPC_TX_SOP, PD_EXT_BATTERY_CAP);
}

static void pe_give_battery_cap_run(int port)
{
	if (PE_CHK_FLAG(port, PE_FLAGS_TX_COMPLETE)) {
		PE_CLR_FLAG(port, PE_FLAGS_TX_COMPLETE);
		if (pe[port].power_role == PD_ROLE_SOURCE)
			set_state_pe(port, PE_SRC_READY);
		else
			set_state_pe(port, PE_SNK_READY);
	}
}

/**
 * PE_Give_Battery_Status
 */
static void pe_give_battery_status_entry(int port)
{
	uint32_t payload = *(uint32_t *)(&emsg[port].buf);
	uint32_t *msg = (uint32_t *)emsg[port].buf;

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
	emsg[port].len = 4;

	prl_send_data_msg(port, TCPC_TX_SOP, PD_DATA_BATTERY_STATUS);
}

static void pe_give_battery_status_run(int port)
{
	if (PE_CHK_FLAG(port, PE_FLAGS_TX_COMPLETE)) {
		PE_CLR_FLAG(port, PE_FLAGS_TX_COMPLETE);
		set_state_pe(port, PE_SRC_READY);
	}
}

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
		prl_send_ctrl_msg(port, TCPC_TX_SOP, PD_CTRL_ACCEPT);
	} else {
		/*
		 * PE_DRS_UFP_DFP_Reject_Swap and PE_DRS_DFP_UFP_Reject_Swap
		 * states embedded here.
		 */
		prl_send_ctrl_msg(port, TCPC_TX_SOP, PD_CTRL_REJECT);
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
			if (pe[port].power_role == PD_ROLE_SOURCE)
				set_state_pe(port, PE_SRC_READY);
			else
				set_state_pe(port, PE_SNK_READY);
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
	if (pe[port].data_role == tc_get_data_role(port))
		return;

	/* Update the data role */
	pe[port].data_role = tc_get_data_role(port);

	/*
	 * Port changed. Transition back to PE_SRC_Ready or
	 * PE_SNK_Ready.
	 */
	if (pe[port].power_role == PD_ROLE_SINK)
		set_state_pe(port, PE_SNK_READY);
	else
		set_state_pe(port, PE_SRC_READY);
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
	prl_send_ctrl_msg(port, TCPC_TX_SOP, PD_CTRL_DR_SWAP);

	pe[port].sender_response_timer = TIMER_DISABLED;
}

static void pe_drs_send_swap_run(int port)
{
	int type;
	int cnt;
	int ext;

	/* Wait until message is sent */
	if (PE_CHK_FLAG(port, PE_FLAGS_TX_COMPLETE)) {
		PE_CLR_FLAG(port, PE_FLAGS_TX_COMPLETE);
		/* start the SenderResponseTimer */
		pe[port].sender_response_timer =
				get_time().val + PD_T_SENDER_RESPONSE;
	}

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

		type = PD_HEADER_TYPE(emsg[port].header);
		cnt = PD_HEADER_CNT(emsg[port].header);
		ext = PD_HEADER_EXT(emsg[port].header);

		if ((ext == 0) && (cnt == 0)) {
			if (type == PD_CTRL_ACCEPT) {
				set_state_pe(port, PE_DRS_CHANGE);
				return;
			} else if ((type == PD_CTRL_REJECT) ||
						(type == PD_CTRL_WAIT)) {
				if (pe[port].power_role == PD_ROLE_SINK)
					set_state_pe(port, PE_SNK_READY);
				else
					set_state_pe(port, PE_SRC_READY);
				return;
			}
		}
	}

	/*
	 * Transition to PE_SRC_Ready or PE_SNK_Ready state when:
	 *   1) the SenderResponseTimer times out.
	 */
	if (get_time().val > pe[port].sender_response_timer) {
		if (pe[port].power_role == PD_ROLE_SINK)
			set_state_pe(port, PE_SNK_READY);
		else
			set_state_pe(port, PE_SRC_READY);
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
		prl_send_ctrl_msg(port, TCPC_TX_SOP, PD_CTRL_REJECT);
	} else {
		pd_request_power_swap(port);
		/* PE_PRS_SRC_SNK_Accept_Swap state embedded here */
		PE_SET_FLAG(port, PE_FLAGS_ACCEPT);
		prl_send_ctrl_msg(port, TCPC_TX_SOP, PD_CTRL_ACCEPT);
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

	/* Tell TypeC to swap from Attached.SRC to Attached.SNK */
	tc_prs_src_snk_assert_rd(port);
	pe[port].ps_source_timer =
			get_time().val + PD_POWER_SUPPLY_TURN_OFF_DELAY;
}

static void pe_prs_src_snk_transition_to_off_run(int port)
{
	/* Give time for supply to power off */
	if (get_time().val < pe[port].ps_source_timer)
		return;

	/* Wait until Rd is asserted */
	if (tc_is_attached_snk(port)) {
		/* Contract is invalid */
		pe_invalidate_explicit_contract(port);
		set_state_pe(port, PE_PRS_SRC_SNK_WAIT_SOURCE_ON);
	}
}

/**
 * PE_PRS_SRC_SNK_Wait_Source_On
 */
static void pe_prs_src_snk_wait_source_on_entry(int port)
{
	print_current_state(port);
	prl_send_ctrl_msg(port, TCPC_TX_SOP, PD_CTRL_PS_RDY);
	pe[port].ps_source_timer = TIMER_DISABLED;
}

static void pe_prs_src_snk_wait_source_on_run(int port)
{
	int type;
	int cnt;
	int ext;

	if (PE_CHK_FLAG(port, PE_FLAGS_TX_COMPLETE)) {
		PE_CLR_FLAG(port, PE_FLAGS_TX_COMPLETE);

		/* Update pe power role */
		pe[port].power_role = tc_get_power_role(port);
		pe[port].ps_source_timer = get_time().val + PD_T_PS_SOURCE_ON;
	}

	/*
	 * Transition to PE_SNK_Startup when:
	 *   1) An PS_RDY Message is received.
	 */
	if (PE_CHK_FLAG(port, PE_FLAGS_MSG_RECEIVED)) {
		PE_CLR_FLAG(port, PE_FLAGS_MSG_RECEIVED);

		type = PD_HEADER_TYPE(emsg[port].header);
		cnt = PD_HEADER_CNT(emsg[port].header);
		ext = PD_HEADER_EXT(emsg[port].header);

		if ((ext == 0) && (cnt == 0) && (type == PD_CTRL_PS_RDY)) {
			tc_pr_swap_complete(port);
			pe[port].ps_source_timer = TIMER_DISABLED;
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

/**
 * PE_PRS_SRC_SNK_Send_Swap
 */
static void pe_prs_src_snk_send_swap_entry(int port)
{
	print_current_state(port);

	/* Request the Protocol Layer to send a PR_Swap Message. */
	prl_send_ctrl_msg(port, TCPC_TX_SOP, PD_CTRL_PR_SWAP);

	/* Start the SenderResponseTimer */
	pe[port].sender_response_timer =
				get_time().val + PD_T_SENDER_RESPONSE;
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

		type = PD_HEADER_TYPE(emsg[port].header);
		cnt = PD_HEADER_CNT(emsg[port].header);
		ext = PD_HEADER_EXT(emsg[port].header);

		if ((ext == 0) && (cnt == 0)) {
			if (type == PD_CTRL_ACCEPT)
				set_state_pe(port,
					PE_PRS_SRC_SNK_TRANSITION_TO_OFF);
			else if ((type == PD_CTRL_REJECT) ||
						(type == PD_CTRL_WAIT))
				set_state_pe(port, PE_SRC_READY);
		}
	}
}

static void pe_prs_src_snk_send_swap_exit(int port)
{
	/* Clear TX Complete Flag if set */
	PE_CLR_FLAG(port, PE_FLAGS_TX_COMPLETE);
}

/**
 * PE_PRS_SNK_SRC_Evaluate_Swap
 */
static void pe_prs_snk_src_evaluate_swap_entry(int port)
{
	print_current_state(port);

	if (!pd_check_power_swap(port)) {
		/* PE_PRS_SNK_SRC_Reject_Swap state embedded here */
		prl_send_ctrl_msg(port, TCPC_TX_SOP, PD_CTRL_REJECT);
	} else {
		pd_request_power_swap(port);
		/* PE_PRS_SNK_SRC_Accept_Swap state embedded here */
		PE_SET_FLAG(port, PE_FLAGS_ACCEPT);
		prl_send_ctrl_msg(port, TCPC_TX_SOP, PD_CTRL_ACCEPT);
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

	if (!PE_CHK_FLAG(port, PE_FLAGS_FAST_ROLE_SWAP_PATH))
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

		type = PD_HEADER_TYPE(emsg[port].header);
		cnt = PD_HEADER_CNT(emsg[port].header);
		ext = PD_HEADER_EXT(emsg[port].header);

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
		if (!PE_CHK_FLAG(port, PE_FLAGS_FAST_ROLE_SWAP_PATH)) {
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
		if (get_time().val >= pe[port].ps_source_timer) {
			/* update pe power role */
			pe[port].power_role = tc_get_power_role(port);
			prl_send_ctrl_msg(port, TCPC_TX_SOP, PD_CTRL_PS_RDY);
			/* reset timer so PD_CTRL_PS_RDY isn't sent again */
			pe[port].ps_source_timer = TIMER_DISABLED;
		}
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
		PE_SET_FLAG(port, PE_FLAGS_RUN_SOURCE_START_TIMER);
		tc_pr_swap_complete(port);
		set_state_pe(port, PE_SRC_STARTUP);
	}
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
	prl_send_ctrl_msg(port,
		TCPC_TX_SOP,
		PE_CHK_FLAG(port, PE_FLAGS_FAST_ROLE_SWAP_PATH)
			? PD_CTRL_FR_SWAP
			: PD_CTRL_PR_SWAP);

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
		set_state_pe(port,
			     PE_CHK_FLAG(port, PE_FLAGS_FAST_ROLE_SWAP_PATH)
				? PE_WAIT_FOR_ERROR_RECOVERY
				: PE_SNK_READY);

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

		type = PD_HEADER_TYPE(emsg[port].header);
		cnt = PD_HEADER_CNT(emsg[port].header);
		ext = PD_HEADER_EXT(emsg[port].header);

		if ((ext == 0) && (cnt == 0)) {
			if (type == PD_CTRL_ACCEPT)
				set_state_pe(port,
					     PE_PRS_SNK_SRC_TRANSITION_TO_OFF);
			else if ((type == PD_CTRL_REJECT) ||
						(type == PD_CTRL_WAIT))
				set_state_pe(port,
					PE_CHK_FLAG(port,
						PE_FLAGS_FAST_ROLE_SWAP_PATH)
					   ? PE_WAIT_FOR_ERROR_RECOVERY
					   : PE_SNK_READY);
		}
	}
}

static void pe_prs_snk_src_send_swap_exit(int port)
{
	/* Clear TX Complete Flag if set */
	PE_CLR_FLAG(port, PE_FLAGS_TX_COMPLETE);
}

/**
 * PE_FRS_SNK_SRC_Start_AMS
 */
static void pe_frs_snk_src_start_ams_entry(int port)
{
	print_current_state(port);

	/* Contract is invalid now */
	pe_invalidate_explicit_contract(port);

	/* Inform Protocol Layer this is start of AMS */
	prl_start_ams(port);

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

/**
 * BIST
 */
static void pe_bist_entry(int port)
{
	uint32_t *payload = (uint32_t *)emsg[port].buf;
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
		prl_send_ctrl_msg(port, TCPC_TX_BIST_MODE_2, 0);
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

static void pe_bist_run(int port)
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
 * Give_Sink_Cap Message
 */
static void pe_snk_give_sink_cap_entry(int port)
{
	print_current_state(port);

	/* Send a Sink_Capabilities Message */
	emsg[port].len = pd_snk_pdo_cnt * 4;
	memcpy(emsg[port].buf, (uint8_t *)pd_snk_pdo, emsg[port].len);
	prl_send_data_msg(port, TCPC_TX_SOP, PD_DATA_SINK_CAP);
}

static void pe_snk_give_sink_cap_run(int port)
{
	if (PE_CHK_FLAG(port, PE_FLAGS_TX_COMPLETE)) {
		PE_CLR_FLAG(port, PE_FLAGS_TX_COMPLETE);
		if (pe[port].power_role == PD_ROLE_SOURCE)
			set_state_pe(port, PE_SRC_READY);
		else
			set_state_pe(port, PE_SNK_READY);
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
	uint32_t *payload = (uint32_t *)emsg[port].buf;
	int cnt = PD_HEADER_CNT(emsg[port].header);
	int sop = PD_HEADER_GET_SOP(emsg[port].header);
	int rlen = 0;
	uint32_t *rdata;

	print_current_state(port);

	/* This is an Interruptible AMS */
	PE_SET_FLAG(port, PE_FLAGS_INTERRUPTIBLE_AMS);

	rlen = pd_custom_vdm(port, cnt, payload, &rdata);
	if (rlen > 0) {
		emsg[port].len = rlen * 4;
		memcpy(emsg[port].buf, (uint8_t *)rdata, emsg[port].len);
		prl_send_data_msg(port, sop, PD_DATA_VENDOR_DEF);
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
		if (pe[port].power_role == PD_ROLE_SOURCE)
			set_state_pe(port, PE_SRC_READY);
		else
			set_state_pe(port, PE_SNK_READY);
	}
}

/**
 * PE_DO_PORT_Discovery
 *
 * NOTE: Port Discovery Policy
 *	To discover a port partner, Vendor Defined Messages (VDMs) are
 *	sent to the port partner. The sequence of commands are
 *	sent in the following order:
 *		1) CMD_DISCOVER_IDENT
 *		2) CMD_DISCOVER_SVID
 *		3) CMD_DISCOVER_MODES
 *		4) CMD_ENTER_MODE
 *		5) CMD_DP_STATUS
 *		6) CMD_DP_CONFIG
 *
 *	If a the port partner replies with BUSY, the sequence is resent
 *	N_DISCOVER_IDENTITY_COUNT times before giving up.
 */
static void pe_do_port_discovery_entry(int port)
{
	print_current_state(port);

	pe[port].partner_type = PORT;
	pe[port].vdm_cnt = 0;
}

static void pe_do_port_discovery_run(int port)
{
#ifdef CONFIG_USB_PD_ALT_MODE_DFP
	uint32_t *payload = (uint32_t *)emsg[port].buf;
	struct svdm_amode_data *modep = get_modep(port, PD_VDO_VID(payload[0]));
	int ret = 0;

	if (!PE_CHK_FLAG(port,
		PE_FLAGS_VDM_REQUEST_NAKED | PE_FLAGS_VDM_REQUEST_BUSY)) {
		switch (pe[port].vdm_cmd) {
		case DO_PORT_DISCOVERY_START:
			pe[port].vdm_cmd = CMD_DISCOVER_IDENT;
			pe[port].vdm_data[0] = 0;
			ret = 1;
			break;
		case CMD_DISCOVER_IDENT:
			pe[port].vdm_cmd = CMD_DISCOVER_SVID;
			pe[port].vdm_data[0] = 0;
			ret = 1;
			break;
		case CMD_DISCOVER_SVID:
			pe[port].vdm_cmd = CMD_DISCOVER_MODES;
			ret = dfp_discover_modes(port, pe[port].vdm_data);
			break;
		case CMD_DISCOVER_MODES:
			pe[port].vdm_cmd = CMD_ENTER_MODE;
			pe[port].vdm_data[0] = pd_dfp_enter_mode(port, 0, 0);
			if (pe[port].vdm_data[0])
				ret = 1;
			break;
		case CMD_ENTER_MODE:
			pe[port].vdm_cmd = CMD_DP_STATUS;
			if (modep->opos) {
				ret = modep->fx->status(port,
						pe[port].vdm_data);
				pe[port].vdm_data[0] |=
						PD_VDO_OPOS(modep->opos);
			}
			break;
		case CMD_DP_STATUS:
			pe[port].vdm_cmd = CMD_DP_CONFIG;

			/*
			 * DP status response & UFP's DP attention have same
			 * payload
			 */
			dfp_consume_attention(port, pe[port].vdm_data);
			if (modep && modep->opos)
				ret = modep->fx->config(port,
							pe[port].vdm_data);
			break;
		case CMD_DP_CONFIG:
			if (modep && modep->opos && modep->fx->post_config)
				modep->fx->post_config(port);
			PE_SET_FLAG(port, PE_FLAGS_DISCOVER_PORT_IDENTITY_DONE);
			break;
		case CMD_EXIT_MODE:
			/* Do nothing */
			break;
		case CMD_ATTENTION:
			/* Do nothing */
			break;
		}
	}

	if (ret == 0) {
		if (PE_CHK_FLAG(port, PE_FLAGS_VDM_REQUEST_NAKED))
			PE_SET_FLAG(port, PE_FLAGS_DISCOVER_PORT_IDENTITY_DONE);

		if (pe[port].power_role == PD_ROLE_SOURCE)
			set_state_pe(port, PE_SRC_READY);
		else
			set_state_pe(port, PE_SNK_READY);
	} else {
		PE_CLR_FLAG(port, PE_FLAGS_VDM_REQUEST_BUSY);

		/*
		 * Copy Vendor Defined Message (VDM) Header into
		 * message buffer
		 */
		if (pe[port].vdm_data[0] == 0)
			pe[port].vdm_data[0] = VDO(
					USB_SID_PD,
					1, /* structured */
					VDO_SVDM_VERS(1) | pe[port].vdm_cmd);

		pe[port].vdm_data[0] |= VDO_CMDT(CMDT_INIT);
		pe[port].vdm_data[0] |= VDO_SVDM_VERS(pd_get_vdo_ver(port));

		pe[port].vdm_cnt = ret;
		set_state_pe(port, PE_VDM_REQUEST);
	}
#endif
}

/**
 * PE_VDM_REQUEST
 */

static void pe_vdm_request_entry(int port)
{
	print_current_state(port);

	/* This is an Interruptible AMS */
	PE_SET_FLAG(port, PE_FLAGS_INTERRUPTIBLE_AMS);

	/* Copy Vendor Data Objects (VDOs) into message buffer */
	if (pe[port].vdm_cnt > 0) {
		/* Copy data after header */
		memcpy(&emsg[port].buf,
			(uint8_t *)pe[port].vdm_data,
			pe[port].vdm_cnt * 4);
		/* Update len with the number of VDO bytes */
		emsg[port].len = pe[port].vdm_cnt * 4;
	}

	if (pe[port].partner_type) {
		/* Save power and data roles */
		pe[port].saved_power_role = tc_get_power_role(port);
		pe[port].saved_data_role = tc_get_data_role(port);

		prl_send_data_msg(port, TCPC_TX_SOP_PRIME, PD_DATA_VENDOR_DEF);
	} else {
		prl_send_data_msg(port, TCPC_TX_SOP, PD_DATA_VENDOR_DEF);
	}

	pe[port].vdm_response_timer = TIMER_DISABLED;
}

static void pe_vdm_request_run(int port)
{
	if (PE_CHK_FLAG(port, PE_FLAGS_TX_COMPLETE)) {
		/* Message was sent */
		PE_CLR_FLAG(port, PE_FLAGS_TX_COMPLETE);

		if (pe[port].partner_type) {
			/* Restore power and data roles */
			tc_set_power_role(port, pe[port].saved_power_role);
			tc_set_data_role(port, pe[port].saved_data_role);
		}

		/* Start no response timer */
		pe[port].vdm_response_timer =
			get_time().val + PD_T_VDM_SNDR_RSP;
	}

	if (PE_CHK_FLAG(port, PE_FLAGS_MSG_RECEIVED)) {
		uint32_t *payload;
		int sop;
		uint8_t type;
		uint8_t cnt;
		uint8_t ext;

		/* Message received */
		PE_CLR_FLAG(port, PE_FLAGS_MSG_RECEIVED);

		/* Get the message */
		payload = (uint32_t *)emsg[port].buf;
		sop = PD_HEADER_GET_SOP(emsg[port].header);
		type = PD_HEADER_TYPE(emsg[port].header);
		cnt = PD_HEADER_CNT(emsg[port].header);
		ext = PD_HEADER_EXT(emsg[port].header);

		if ((sop == TCPC_TX_SOP || sop == TCPC_TX_SOP_PRIME) &&
			type == PD_DATA_VENDOR_DEF && cnt > 0 && ext == 0) {
			if (PD_VDO_CMDT(payload[0]) == CMDT_RSP_ACK)
				return  set_state_pe(port, PE_VDM_ACKED);
			else if (PD_VDO_CMDT(payload[0]) == CMDT_RSP_NAK ||
				PD_VDO_CMDT(payload[0]) == CMDT_RSP_BUSY) {
				if (PD_VDO_CMDT(payload[0]) == CMDT_RSP_NAK)
					PE_SET_FLAG(port,
						PE_FLAGS_VDM_REQUEST_NAKED);
				else
					PE_SET_FLAG(port,
						PE_FLAGS_VDM_REQUEST_BUSY);

				/* Return to previous state */
				if (get_last_state_pe(port) ==
							PE_DO_PORT_DISCOVERY)
					set_state_pe(port,
							PE_DO_PORT_DISCOVERY);
				else if (get_last_state_pe(port) ==
						PE_SRC_VDM_IDENTITY_REQUEST)
					set_state_pe(port,
						PE_SRC_VDM_IDENTITY_REQUEST);
				else if (pe[port].power_role == PD_ROLE_SOURCE)
					set_state_pe(port, PE_SRC_READY);
				else
					set_state_pe(port, PE_SNK_READY);
				return;
			}
		}
	}



	if (PE_CHK_FLAG(port, PE_FLAGS_PROTOCOL_ERROR)) {
		/* Message not sent and we received a protocol error */
		PE_CLR_FLAG(port, PE_FLAGS_PROTOCOL_ERROR);

		if (pe[port].partner_type) {
			/* Restore power and data roles */
			tc_set_power_role(port, pe[port].saved_power_role);
			tc_set_data_role(port, pe[port].saved_data_role);
		}

		/* Fake busy response so we try to send command again */
		PE_SET_FLAG(port, PE_FLAGS_VDM_REQUEST_BUSY);
	} else if (get_time().val > pe[port].vdm_response_timer) {
		CPRINTF("VDM %s Response Timeout\n",
				pe[port].partner_type ? "Cable" : "Port");

		PE_SET_FLAG(port, PE_FLAGS_VDM_REQUEST_NAKED);
	} else {
		/* No error yet, keep looping */
		return;
	}

	/*
	 * We errored out, we may need to return to the previous state or the
	 * default state for the power role.
	 */
	if (get_last_state_pe(port) == PE_DO_PORT_DISCOVERY)
		set_state_pe(port, PE_DO_PORT_DISCOVERY);
	else if (get_last_state_pe(port) == PE_SRC_VDM_IDENTITY_REQUEST)
		set_state_pe(port, PE_SRC_VDM_IDENTITY_REQUEST);
	else if (pe[port].power_role == PD_ROLE_SOURCE)
		set_state_pe(port, PE_SRC_READY);
	else
		set_state_pe(port, PE_SNK_READY);
}

static void pe_vdm_request_exit(int port)
{
	PE_CLR_FLAG(port, PE_FLAGS_INTERRUPTIBLE_AMS);
}

enum idh_ptype get_usb_pd_mux_cable_type(int port)
{
	if (pe[port].passive_cable_vdo != PD_VDO_INVALID)
		return IDH_PTYPE_PCABLE;
	else if (pe[port].active_cable_vdo1 != PD_VDO_INVALID)
		return IDH_PTYPE_ACABLE;
	else
		return IDH_PTYPE_UNDEF;
}

/**
 * PE_VDM_Acked
 */
static void pe_vdm_acked_entry(int port)
{
	uint32_t *payload;
	uint8_t vdo_cmd;
	int sop;

	print_current_state(port);

	/* Get the message */
	payload = (uint32_t *)emsg[port].buf;
	vdo_cmd = PD_VDO_CMD(payload[0]);
	sop = PD_HEADER_GET_SOP(emsg[port].header);

	if (sop == TCPC_TX_SOP_PRIME) {
		/*
		 * Handle Message From Cable Plug
		 */

		uint32_t vdm_header = payload[0];
		uint32_t id_header = payload[1];
		uint8_t ptype_ufp;

		if (PD_VDO_CMD(vdm_header) == CMD_DISCOVER_IDENT &&
				PD_VDO_SVDM(vdm_header) &&
				PD_HEADER_CNT(emsg[port].header) == 5) {
			ptype_ufp = PD_IDH_PTYPE(id_header);

			switch (ptype_ufp) {
			case IDH_PTYPE_UNDEF:
				break;
			case IDH_PTYPE_HUB:
				break;
			case IDH_PTYPE_PERIPH:
				break;
			case IDH_PTYPE_PCABLE:
				/* Passive Cable Detected */
				pe[port].passive_cable_vdo =
						payload[4];
				break;
			case IDH_PTYPE_ACABLE:
				/* Active Cable Detected */
				pe[port].active_cable_vdo1 =
						payload[4];
				pe[port].active_cable_vdo2 =
						payload[5];
				break;
			case IDH_PTYPE_AMA:
				/*
				 * Alternate Mode Adapter
				 * Detected
				 */
				pe[port].ama_vdo = payload[4];
				break;
			case IDH_PTYPE_VPD:
				/*
				 * VCONN Powered Device
				 * Detected
				 */
				pe[port].vpd_vdo = payload[4];

				/*
				 * If a CTVPD device was not discovered, inform
				 * the Device Policy Manager that the Discover
				 * Identity is done.
				 *
				 * If a CTVPD device is discovered, the Device
				 * Policy Manager will clear the DISC_IDENT flag
				 * set by tc_disc_ident_in_progress.
				 */
				if (pe[port].vpd_vdo < 0 ||
						!VPD_VDO_CTS(pe[port].vpd_vdo))
					tc_disc_ident_complete(port);
				break;
			}
		}
	} else {
		/*
		 * Handle Message From Port Partner
		 */

#ifdef CONFIG_USB_PD_ALT_MODE_DFP
		int cnt = PD_HEADER_CNT(emsg[port].header);
		struct svdm_amode_data *modep;

		modep = get_modep(port, PD_VDO_VID(payload[0]));
#endif

		switch (vdo_cmd) {
#ifdef CONFIG_USB_PD_ALT_MODE_DFP
		case CMD_DISCOVER_IDENT:
			dfp_consume_identity(port, cnt, payload);
#ifdef CONFIG_CHARGE_MANAGER
			if (pd_charge_from_device(pd_get_identity_vid(port),
						pd_get_identity_pid(port))) {
				charge_manager_update_dualrole(port,
								CAP_DEDICATED);
			}
#endif
			break;
		case CMD_DISCOVER_SVID:
			dfp_consume_svids(port, cnt, payload);
			break;
		case CMD_DISCOVER_MODES:
			dfp_consume_modes(port, cnt, payload);
			break;
		case CMD_ENTER_MODE:
			break;
		case CMD_DP_STATUS:
			/*
			 * DP status response & UFP's DP attention have same
			 * payload
			 */
			dfp_consume_attention(port, payload);
			break;
		case CMD_DP_CONFIG:
			if (modep && modep->opos && modep->fx->post_config)
				modep->fx->post_config(port);
			break;
		case CMD_EXIT_MODE:
			/* Do nothing */
			break;
#endif
		case CMD_ATTENTION:
			/* Do nothing */
			break;
		default:
			CPRINTF("ERR:CMD:%d\n", vdo_cmd);
		}
	}

	if (!PE_CHK_FLAG(port, PE_FLAGS_DISCOVER_VDM_IDENTITY_DONE)) {
		PE_SET_FLAG(port, PE_FLAGS_DISCOVER_VDM_IDENTITY_DONE);
		set_state_pe(port, PE_SRC_VDM_IDENTITY_REQUEST);
	} else if (!PE_CHK_FLAG(port, PE_FLAGS_DISCOVER_PORT_IDENTITY_DONE)) {
		set_state_pe(port, PE_DO_PORT_DISCOVERY);
	} else if (pe[port].power_role == PD_ROLE_SOURCE) {
		set_state_pe(port, PE_SRC_READY);
	} else {
		set_state_pe(port, PE_SNK_READY);
	}
}

/**
 * PE_VDM_Response
 */
static void pe_vdm_response_entry(int port)
{
	int ret = 0;
	uint32_t *payload;
	uint8_t vdo_cmd;
	int cmd_type;
	svdm_rsp_func func = NULL;

	print_current_state(port);

	/* Get the message */
	payload = (uint32_t *)emsg[port].buf;
	vdo_cmd = PD_VDO_CMD(payload[0]);
	cmd_type = PD_VDO_CMDT(payload[0]);
	payload[0] &= ~VDO_CMDT_MASK;

	if (cmd_type != CMDT_INIT) {
		CPRINTF("ERR:CMDT:%d\n", vdo_cmd);

		if (pe[port].power_role == PD_ROLE_SOURCE)
			set_state_pe(port, PE_SRC_READY);
		else
			set_state_pe(port, PE_SNK_READY);
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
		dfp_consume_attention(port, payload);
		if (pe[port].power_role == PD_ROLE_SOURCE)
			set_state_pe(port, PE_SRC_READY);
		else
			set_state_pe(port, PE_SNK_READY);
		return;
#endif
	default:
		CPRINTF("VDO ERR:CMD:%d\n", vdo_cmd);
	}

	if (func) {
		ret = func(port, payload);
		if (ret)
			/* ACK */
			payload[0] = VDO(
				USB_VID_GOOGLE,
				1, /* Structured VDM */
				VDO_SVDM_VERS(pd_get_vdo_ver(port)) |
				VDO_CMDT(CMDT_RSP_ACK) |
				vdo_cmd);
		else if (!ret)
			/* NAK */
			payload[0] = VDO(
				USB_VID_GOOGLE,
				1, /* Structured VDM */
				VDO_SVDM_VERS(pd_get_vdo_ver(port)) |
				VDO_CMDT(CMDT_RSP_NAK) |
				vdo_cmd);
		else
			/* BUSY */
			payload[0] = VDO(
				USB_VID_GOOGLE,
				1, /* Structured VDM */
				VDO_SVDM_VERS(pd_get_vdo_ver(port)) |
				VDO_CMDT(CMDT_RSP_BUSY) |
				vdo_cmd);

		if (ret <= 0)
			ret = 4;
	} else {
		/* not supported : NACK it */
		payload[0] = VDO(
			USB_VID_GOOGLE,
			1, /* Structured VDM */
			VDO_SVDM_VERS(pd_get_vdo_ver(port)) |
			VDO_CMDT(CMDT_RSP_NAK) |
			vdo_cmd);
		ret = 4;
	}

	/* Send ACK, NAK, or BUSY */
	emsg[port].len = ret;
	prl_send_data_msg(port, TCPC_TX_SOP, PD_DATA_VENDOR_DEF);
}

static void pe_vdm_response_run(int port)
{
	if (PE_CHK_FLAG(port, PE_FLAGS_TX_COMPLETE) ||
			PE_CHK_FLAG(port, PE_FLAGS_PROTOCOL_ERROR)) {
		PE_CLR_FLAG(port, PE_FLAGS_TX_COMPLETE |
						PE_FLAGS_PROTOCOL_ERROR);

		if (pe[port].power_role == PD_ROLE_SOURCE)
			set_state_pe(port, PE_SRC_READY);
		else
			set_state_pe(port, PE_SNK_READY);
	}
}

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
		prl_send_ctrl_msg(port, TCPC_TX_SOP, PD_CTRL_REJECT);
	}
	/* Port is not ready to perform a VCONN swap */
	else if (tc_is_vconn_src(port) < 0) {
		/* NOTE: PE_VCS_Reject_Swap State embedded here */
		prl_send_ctrl_msg(port, TCPC_TX_SOP, PD_CTRL_WAIT);
	}
	/* Port is ready to perform a VCONN swap */
	else {
		/* NOTE: PE_VCS_Accept_Swap State embedded here */
		PE_SET_FLAG(port, PE_FLAGS_ACCEPT);
		prl_send_ctrl_msg(port, TCPC_TX_SOP, PD_CTRL_ACCEPT);
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
			if (pe[port].power_role == PD_ROLE_SOURCE)
				set_state_pe(port, PE_SRC_READY);
			else
				set_state_pe(port, PE_SNK_READY);

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
	prl_send_ctrl_msg(port, TCPC_TX_SOP, PD_CTRL_VCONN_SWAP);

	pe[port].sender_response_timer = TIMER_DISABLED;
}

static void pe_vcs_send_swap_run(int port)
{
	uint8_t type;
	uint8_t cnt;

	/* Wait until message is sent */
	if (PE_CHK_FLAG(port, PE_FLAGS_TX_COMPLETE)) {
		PE_CLR_FLAG(port, PE_FLAGS_TX_COMPLETE);
		/* Start the SenderResponseTimer */
		pe[port].sender_response_timer = get_time().val +
						PD_T_SENDER_RESPONSE;
	}

	if (PE_CHK_FLAG(port, PE_FLAGS_MSG_RECEIVED)) {
		PE_CLR_FLAG(port, PE_FLAGS_MSG_RECEIVED);

		type = PD_HEADER_TYPE(emsg[port].header);
		cnt = PD_HEADER_CNT(emsg[port].header);

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
			 *   1) SenderResponseTimer Timeout or
			 *   2) Reject message is received or
			 *   3) Wait message Received.
			 */
			if (get_time().val > pe[port].sender_response_timer ||
						type == PD_CTRL_REJECT ||
							type == PD_CTRL_WAIT) {
				if (pe[port].power_role == PD_ROLE_SOURCE)
					set_state_pe(port, PE_SRC_READY);
				else
					set_state_pe(port, PE_SNK_READY);
			}
		}
	}
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
		if ((PD_HEADER_CNT(emsg[port].header) == 0) &&
				(PD_HEADER_TYPE(emsg[port].header) ==
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
		pe[port].cable_discover_identity_count = 0;
		pe[port].port_discover_identity_count = 0;

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
	prl_send_ctrl_msg(port, TCPC_TX_SOP, PD_CTRL_PS_RDY);

	pe[port].sub = PE_SUB0;
}

static void pe_vcs_send_ps_rdy_swap_run(int port)
{
	if (PE_CHK_FLAG(port, PE_FLAGS_TX_COMPLETE)) {
		PE_CLR_FLAG(port, PE_FLAGS_TX_COMPLETE);

		switch (pe[port].sub) {
		case PE_SUB0:
			/*
			 * After a VCONN Swap the VCONN Source needs to reset
			 * the Cable Plug’s Protocol Layer in order to ensure
			 * MessageID synchronization.
			 */
			prl_send_ctrl_msg(port, TCPC_TX_SOP_PRIME,
							PD_CTRL_SOFT_RESET);
			pe[port].sub = PE_SUB1;
			pe[port].timeout = get_time().val + 100*MSEC;
			break;
		case PE_SUB1:
			/* Got ACCEPT or REJECT from Cable Plug */
			if (PE_CHK_FLAG(port, PE_FLAGS_MSG_RECEIVED) ||
					get_time().val > pe[port].timeout) {
				PE_CLR_FLAG(port, PE_FLAGS_MSG_RECEIVED);
				/*
				 * A VCONN Swap Shall reset the
				 * DiscoverIdentityCounter to zero
				 */
				pe[port].cable_discover_identity_count = 0;
				pe[port].port_discover_identity_count = 0;

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
	}
}

/*
 * PE_DR_SNK_Get_Sink_Cap
 */
static void pe_dr_snk_get_sink_cap_entry(int port)
{
	print_current_state(port);

	/* Send a Get Sink Cap Message */
	prl_send_ctrl_msg(port, TCPC_TX_SOP, PD_CTRL_GET_SINK_CAP);

	/* Don't start the timer until message sent */
	pe[port].sender_response_timer = 0;
}

static void pe_dr_snk_get_sink_cap_run(int port)
{
	int type;
	int cnt;
	int ext;
	uint32_t payload;

	/* Wait until message is sent */
	if (pe[port].sender_response_timer == 0) {
		if (PE_CHK_FLAG(port, PE_FLAGS_TX_COMPLETE)) {
			PE_CLR_FLAG(port, PE_FLAGS_TX_COMPLETE);
			/* start the SenderResponseTimer */
			pe[port].sender_response_timer =
					get_time().val + PD_T_SENDER_RESPONSE;
		} else {
			return;
		}
	}

	/*
	 * Determine if FRS is possible based on the returned Sink Caps
	 * and transition to PE_SNK_Ready when:
	 *   1) An Accept Message is received.
	 *
	 * Transition to PE_SNK_Ready state when:
	 *   1) A Reject Message is received.
	 *   2) Or a Wait Message is received.
	 */
	if (PE_CHK_FLAG(port, PE_FLAGS_MSG_RECEIVED)) {
		PE_CLR_FLAG(port, PE_FLAGS_MSG_RECEIVED);

		type = PD_HEADER_TYPE(emsg[port].header);
		cnt = PD_HEADER_CNT(emsg[port].header);
		ext = PD_HEADER_EXT(emsg[port].header);
		payload = *(uint32_t *)emsg[port].buf;

		if ((ext == 0) && (cnt == 0)) {
			if (type == PD_CTRL_ACCEPT) {
				/*
				 * Check message to see if we can handle
				 * FRS for this connection.
				 *
				 * TODO(b/14191267): Make sure we can handle
				 * the required current before we enable FRS.
				 */
				if (payload & PDO_FIXED_DUAL_ROLE) {
					switch (payload &
						PDO_FIXED_FRS_CURR_MASK) {
					case PDO_FIXED_FRS_CURR_NOT_SUPPORTED:
						break;
					case PDO_FIXED_FRS_CURR_DFLT_USB_POWER:
					case PDO_FIXED_FRS_CURR_1A5_AT_5V:
					case PDO_FIXED_FRS_CURR_3A0_AT_5V:
						pe_set_frs_enable(port, 1);
						return;
					}
				}
				set_state_pe(port, PE_SNK_READY);
				return;
			} else if ((type == PD_CTRL_REJECT) ||
				   (type == PD_CTRL_WAIT)) {
				set_state_pe(port, PE_SNK_READY);
				return;
			}
		}
	}

	/*
	 * Transition to PE_SNK_Ready state when:
	 *   1) SenderResponseTimer times out.
	 */
	if (get_time().val > pe[port].sender_response_timer)
		set_state_pe(port, PE_SNK_READY);
}

/* Policy Engine utility functions */
int pd_check_requested_voltage(uint32_t rdo, const int port)
{
	int max_ma = rdo & 0x3FF;
	int op_ma = (rdo >> 10) & 0x3FF;
	int idx = RDO_POS(rdo);
	uint32_t pdo;
	uint32_t pdo_ma;
#if defined(CONFIG_USB_PD_DYNAMIC_SRC_CAP) || \
		defined(CONFIG_USB_PD_MAX_SINGLE_SOURCE_CURRENT)
	const uint32_t *src_pdo;
	const int pdo_cnt = charge_manager_get_source_pdo(&src_pdo, port);
#else
	const uint32_t *src_pdo = pd_src_pdo;
	const int pdo_cnt = pd_src_pdo_cnt;
#endif

	/* Board specific check for this request */
	if (pd_board_check_request(rdo, pdo_cnt))
		return EC_ERROR_INVAL;

	/* check current ... */
	pdo = src_pdo[idx - 1];
	pdo_ma = (pdo & 0x3ff);

	if (op_ma > pdo_ma)
		return EC_ERROR_INVAL; /* too much op current */

	if (max_ma > pdo_ma && !(rdo & RDO_CAP_MISMATCH))
		return EC_ERROR_INVAL; /* too much max current */

	CPRINTF("Requested %d mV %d mA (for %d/%d mA)\n",
		 ((pdo >> 10) & 0x3ff) * 50, (pdo & 0x3ff) * 10,
		 op_ma * 10, max_ma * 10);

	/* Accept the requested voltage */
	return EC_SUCCESS;
}

void pd_process_source_cap(int port, int cnt, uint32_t *src_caps)
{
#ifdef CONFIG_CHARGE_MANAGER
	uint32_t ma, mv, pdo;
#endif
	int i;

	pe[port].src_cap_cnt = cnt;
	for (i = 0; i < cnt; i++)
		pe[port].src_caps[i] = *src_caps++;

#ifdef CONFIG_CHARGE_MANAGER
	/* Get max power info that we could request */
	pd_find_pdo_index(pe[port].src_cap_cnt, pe[port].src_caps,
						PD_MAX_VOLTAGE_MV, &pdo);
	pd_extract_pdo_power(pdo, &ma, &mv);
	/* Set max. limit, but apply 500mA ceiling */
	charge_manager_set_ceil(port, CEIL_REQUESTOR_PD, PD_MIN_MA);
	pd_set_input_current_limit(port, ma, mv);
#endif
}

void pd_set_max_voltage(unsigned int mv)
{
	max_request_mv = mv;
}

unsigned int pd_get_max_voltage(void)
{
	return max_request_mv;
}

int pd_charge_from_device(uint16_t vid, uint16_t pid)
{
	/* TODO: rewrite into table if we get more of these */
	/*
	 * White-list Apple charge-through accessory since it doesn't set
	 * externally powered bit, but we still need to charge from it when
	 * we are a sink.
	 */
	return (vid == USB_VID_APPLE &&
			(pid == USB_PID1_APPLE || pid == USB_PID2_APPLE));
}

#ifdef CONFIG_USB_PD_DISCHARGE
void pd_set_vbus_discharge(int port, int enable)
{
	static struct mutex discharge_lock[CONFIG_USB_PD_PORT_MAX_COUNT];

	mutex_lock(&discharge_lock[port]);
	enable &= !board_vbus_source_enabled(port);

#ifdef CONFIG_USB_PD_DISCHARGE_GPIO
#if CONFIG_USB_PD_PORT_MAX_COUNT == 0
	gpio_set_level(GPIO_USB_C0_DISCHARGE, enable);
#elif CONFIG_USB_PD_PORT_MAX_COUNT == 1
	gpio_set_level(GPIO_USB_C1_DISCHARGE, enable);
#elif CONFIG_USB_PD_PORT_MAX_COUNT == 2
	gpio_set_level(GPIO_USB_C2_DISCHARGE, enable);
#elif CONFIG_USB_PD_PORT_MAX_COUNT == 3
	gpio_set_level(GPIO_USB_C3_DISCHARGE, enable);
#endif
#else
	if (IS_ENABLED(CONFIG_USB_PD_DISCHARGE_TCPC))
		tcpc_discharge_vbus(port, enable);
	else if (IS_ENABLED(CONFIG_USB_PD_DISCHARGE_PPC))
		ppc_discharge_vbus(port, enable);
#endif
	mutex_unlock(&discharge_lock[port]);
}
#endif /* CONFIG_USB_PD_DISCHARGE */

/* VDM utility functions */
#ifdef CONFIG_USB_PD_ALT_MODE_DFP
static void pd_usb_billboard_deferred(void)
{
#if defined(CONFIG_USB_PD_ALT_MODE) && !defined(CONFIG_USB_PD_ALT_MODE_DFP) \
	&& !defined(CONFIG_USB_PD_SIMPLE_DFP) && defined(CONFIG_USB_BOS)

	/*
	 * TODO(tbroch)
	 * 1. Will we have multiple type-C port UFPs
	 * 2. Will there be other modes applicable to DFPs besides DP
	 */
	if (!pd_alt_mode(0, USB_SID_DISPLAYPORT))
		usb_connect();

#endif
}
DECLARE_DEFERRED(pd_usb_billboard_deferred);

void pd_dfp_pe_init(int port)
{
	memset(&pe[port].am_policy, 0, sizeof(struct pd_policy));
}

#ifdef CONFIG_USB_PD_ALT_MODE_DFP
static void dfp_consume_identity(int port, int cnt, uint32_t *payload)
{
	int ptype = PD_IDH_PTYPE(payload[VDO_I(IDH)]);
	size_t identity_size = MIN(sizeof(pe[port].am_policy.identity),
				(cnt - 1) * sizeof(uint32_t));

	pd_dfp_pe_init(port);
	memcpy(&pe[port].am_policy.identity, payload + 1, identity_size);

	switch (ptype) {
	case IDH_PTYPE_AMA:
/* Leave vbus ON if the following macro is false */
#if defined(CONFIG_USB_PD_DUAL_ROLE) && defined(CONFIG_USBC_VCONN_SWAP)
		/* Adapter is requesting vconn, try to supply it */
		if (PD_VDO_AMA_VCONN_REQ(payload[VDO_I(AMA)]))
			tc_vconn_on(port);

		/* Only disable vbus if vconn was requested */
		if (PD_VDO_AMA_VCONN_REQ(payload[VDO_I(AMA)]) &&
				!PD_VDO_AMA_VBUS_REQ(payload[VDO_I(AMA)]))
			pd_power_supply_reset(port);
#endif
		break;
	default:
		break;
	}
}

static void dfp_consume_svids(int port, int cnt, uint32_t *payload)
{
	int i;
	uint32_t *ptr = payload + 1;
	int vdo = 1;
	uint16_t svid0, svid1;

	for (i = pe[port].am_policy.svid_cnt;
				i < pe[port].am_policy.svid_cnt + 12; i += 2) {
		if (i == SVID_DISCOVERY_MAX) {
			CPRINTF("ERR:SVIDCNT\n");
			break;
		}
		/*
		 * Verify we're still within the valid packet (count will be one
		 * for the VDM header + xVDOs)
		 */
		if (vdo >= cnt)
			break;

		svid0 = PD_VDO_SVID_SVID0(*ptr);
		if (!svid0)
			break;
		pe[port].am_policy.svids[i].svid = svid0;
		pe[port].am_policy.svid_cnt++;

		svid1 = PD_VDO_SVID_SVID1(*ptr);
		if (!svid1)
			break;
		pe[port].am_policy.svids[i + 1].svid = svid1;
		pe[port].am_policy.svid_cnt++;
		ptr++;
		vdo++;
	}

	/* TODO(tbroch) need to re-issue discover svids if > 12 */
	if (i && ((i % 12) == 0))
		CPRINTF("ERR:SVID+12\n");
}

static int dfp_discover_modes(int port, uint32_t *payload)
{
	uint16_t svid =
		pe[port].am_policy.svids[pe[port].am_policy.svid_idx].svid;

	if (pe[port].am_policy.svid_idx >= pe[port].am_policy.svid_cnt)
		return 0;

	payload[0] = VDO(svid, 1, CMD_DISCOVER_MODES);

	return 1;
}

static void dfp_consume_modes(int port, int cnt, uint32_t *payload)
{
	int idx = pe[port].am_policy.svid_idx;

	pe[port].am_policy.svids[idx].mode_cnt = cnt - 1;

	if (pe[port].am_policy.svids[idx].mode_cnt < 0) {
		CPRINTF("ERR:NOMODE\n");
	} else {
		memcpy(
		pe[port].am_policy.svids[pe[port].am_policy.svid_idx].mode_vdo,
		&payload[1],
		sizeof(uint32_t) * pe[port].am_policy.svids[idx].mode_cnt);
	}

	pe[port].am_policy.svid_idx++;
}

static int get_mode_idx(int port, uint16_t svid)
{
	int i;

	for (i = 0; i < PD_AMODE_COUNT; i++) {
		if (pe[port].am_policy.amodes[i].fx->svid == svid)
			return i;
	}

	return -1;
}

static struct svdm_amode_data *get_modep(int port, uint16_t svid)
{
	int idx = get_mode_idx(port, svid);

	return (idx == -1) ? NULL : &pe[port].am_policy.amodes[idx];
}

int pd_alt_mode(int port, uint16_t svid)
{
	struct svdm_amode_data *modep = get_modep(port, svid);

	return (modep) ? modep->opos : -1;
}

int allocate_mode(int port, uint16_t svid)
{
	int i, j;
	struct svdm_amode_data *modep;
	int mode_idx = get_mode_idx(port, svid);

	if (mode_idx != -1)
		return mode_idx;

	/* There's no space to enter another mode */
	if (pe[port].am_policy.amode_idx == PD_AMODE_COUNT) {
		CPRINTF("ERR:NO AMODE SPACE\n");
		return -1;
	}

	/* Allocate ...  if SVID == 0 enter default supported policy */
	for (i = 0; i < supported_modes_cnt; i++) {
		if (!&supported_modes[i])
			continue;

		for (j = 0; j < pe[port].am_policy.svid_cnt; j++) {
			struct svdm_svid_data *svidp =
						&pe[port].am_policy.svids[j];

			if ((svidp->svid != supported_modes[i].svid) ||
					(svid && (svidp->svid != svid)))
				continue;

			modep =
		&pe[port].am_policy.amodes[pe[port].am_policy.amode_idx];
			modep->fx = &supported_modes[i];
			modep->data = &pe[port].am_policy.svids[j];
			pe[port].am_policy.amode_idx++;
			return pe[port].am_policy.amode_idx - 1;
		}
	}
	return -1;
}

uint32_t pd_dfp_enter_mode(int port, uint16_t svid, int opos)
{
	int mode_idx = allocate_mode(port, svid);
	struct svdm_amode_data *modep;
	uint32_t mode_caps;

	if (mode_idx == -1)
		return 0;

	modep = &pe[port].am_policy.amodes[mode_idx];

	if (!opos) {
		/* choose the lowest as default */
		modep->opos = 1;
	} else if (opos <= modep->data->mode_cnt) {
		modep->opos = opos;
	} else {
		CPRINTF("opos error\n");
		return 0;
	}

	mode_caps = modep->data->mode_vdo[modep->opos - 1];
	if (modep->fx->enter(port, mode_caps) == -1)
		return 0;

	PE_SET_FLAG(port, PE_FLAGS_MODAL_OPERATION);

	/* SVDM to send to UFP for mode entry */
	return VDO(modep->fx->svid, 1, CMD_ENTER_MODE | VDO_OPOS(modep->opos));
}

static int validate_mode_request(struct svdm_amode_data *modep,
					uint16_t svid, int opos)
{
	if (!modep->fx)
		return 0;

	if (svid != modep->fx->svid) {
		CPRINTF("ERR:svid r:0x%04x != c:0x%04x\n",
			svid, modep->fx->svid);
		return 0;
	}

	if (opos != modep->opos) {
		CPRINTF("ERR:opos r:%d != c:%d\n",
			opos, modep->opos);
		return 0;
	}

	return 1;
}

static void dfp_consume_attention(int port, uint32_t *payload)
{
	uint16_t svid = PD_VDO_VID(payload[0]);
	int opos = PD_VDO_OPOS(payload[0]);
	struct svdm_amode_data *modep = get_modep(port, svid);

	if (!modep || !validate_mode_request(modep, svid, opos))
		return;

	if (modep->fx->attention)
		modep->fx->attention(port, payload);
}
#endif
/*
 * This algorithm defaults to choosing higher pin config over lower ones in
 * order to prefer multi-function if desired.
 *
 *  NAME | SIGNALING | OUTPUT TYPE | MULTI-FUNCTION | PIN CONFIG
 * -------------------------------------------------------------
 *  A    |  USB G2   |  ?          | no             | 00_0001
 *  B    |  USB G2   |  ?          | yes            | 00_0010
 *  C    |  DP       |  CONVERTED  | no             | 00_0100
 *  D    |  PD       |  CONVERTED  | yes            | 00_1000
 *  E    |  DP       |  DP         | no             | 01_0000
 *  F    |  PD       |  DP         | yes            | 10_0000
 *
 * if UFP has NOT asserted multi-function preferred code masks away B/D/F
 * leaving only A/C/E.  For single-output dongles that should leave only one
 * possible pin config depending on whether its a converter DP->(VGA|HDMI) or DP
 * output.  If UFP is a USB-C receptacle it may assert C/D/E/F.  The DFP USB-C
 * receptacle must always choose C/D in those cases.
 */
int pd_dfp_dp_get_pin_mode(int port, uint32_t status)
{
	struct svdm_amode_data *modep = get_modep(port, USB_SID_DISPLAYPORT);
	uint32_t mode_caps;
	uint32_t pin_caps;

	if (!modep)
		return 0;

	mode_caps = modep->data->mode_vdo[modep->opos - 1];

	/* TODO(crosbug.com/p/39656) revisit with DFP that can be a sink */
	pin_caps = PD_DP_PIN_CAPS(mode_caps);

	/* if don't want multi-function then ignore those pin configs */
	if (!PD_VDO_DPSTS_MF_PREF(status))
		pin_caps &= ~MODE_DP_PIN_MF_MASK;

	/* TODO(crosbug.com/p/39656) revisit if DFP drives USB Gen 2 signals */
	pin_caps &= ~MODE_DP_PIN_BR2_MASK;

	/* if C/D present they have precedence over E/F for USB-C->USB-C */
	if (pin_caps & (MODE_DP_PIN_C | MODE_DP_PIN_D))
		pin_caps &= ~(MODE_DP_PIN_E | MODE_DP_PIN_F);

	/* get_next_bit returns undefined for zero */
	if (!pin_caps)
		return 0;

	return 1 << get_next_bit(&pin_caps);
}

int pd_dfp_exit_mode(int port, uint16_t svid, int opos)
{
	struct svdm_amode_data *modep;
	int idx;


	/*
	 * Empty svid signals we should reset DFP VDM state by exiting all
	 * entered modes then clearing state.  This occurs when we've
	 * disconnected or for hard reset.
	 */
	if (!svid) {
		for (idx = 0; idx < PD_AMODE_COUNT; idx++)
			if (pe[port].am_policy.amodes[idx].fx)
				pe[port].am_policy.amodes[idx].fx->exit(port);

		pd_dfp_pe_init(port);
		return 0;
	}

	/*
	 * TODO(crosbug.com/p/33946) : below needs revisited to allow multiple
	 * mode exit.  Additionally it should honor OPOS == 7 as DFP's request
	 * to exit all modes.  We currently don't have any UFPs that support
	 * multiple modes on one SVID.
	 */
	modep = get_modep(port, svid);
	if (!modep || !validate_mode_request(modep, svid, opos))
		return 0;

	/* call DFPs exit function */
	modep->fx->exit(port);

	PE_CLR_FLAG(port, PE_FLAGS_MODAL_OPERATION);

	/* exit the mode */
	modep->opos = 0;

	return 1;
}

uint16_t pd_get_identity_vid(int port)
{
	return PD_IDH_VID(pe[port].am_policy.identity[0]);
}

uint16_t pd_get_identity_pid(int port)
{
	return PD_PRODUCT_PID(pe[port].am_policy.identity[2]);
}


#ifdef CONFIG_CMD_USB_PD_PE
static void dump_pe(int port)
{
	const char * const idh_ptype_names[]  = {
		"UNDEF", "Hub", "Periph", "PCable", "ACable", "AMA",
		"RSV6", "RSV7"};

	int i, j, idh_ptype;
	struct svdm_amode_data *modep;
	uint32_t mode_caps;

	if (pe[port].am_policy.identity[0] == 0) {
		ccprintf("No identity discovered yet.\n");
		return;
	}
	idh_ptype = PD_IDH_PTYPE(pe[port].am_policy.identity[0]);
	ccprintf("IDENT:\n");
	ccprintf("\t[ID Header] %08x :: %s, VID:%04x\n",
				pe[port].am_policy.identity[0],
				idh_ptype_names[idh_ptype],
				pd_get_identity_vid(port));
	ccprintf("\t[Cert Stat] %08x\n", pe[port].am_policy.identity[1]);
	for (i = 2; i < ARRAY_SIZE(pe[port].am_policy.identity); i++) {
		ccprintf("\t");
		if (pe[port].am_policy.identity[i])
			ccprintf("[%d] %08x ", i,
					pe[port].am_policy.identity[i]);
	}
	ccprintf("\n");

	if (pe[port].am_policy.svid_cnt < 1) {
		ccprintf("No SVIDS discovered yet.\n");
		return;
	}

	for (i = 0; i < pe[port].am_policy.svid_cnt; i++) {
		ccprintf("SVID[%d]: %04x MODES:", i,
					pe[port].am_policy.svids[i].svid);
		for (j = 0; j < pe[port].am_policy.svids[j].mode_cnt; j++)
			ccprintf(" [%d] %08x", j + 1,
			 pe[port].am_policy.svids[i].mode_vdo[j]);
		ccprintf("\n");
		modep = get_modep(port, pe[port].am_policy.svids[i].svid);
		if (modep) {
			mode_caps = modep->data->mode_vdo[modep->opos - 1];
			ccprintf("MODE[%d]: svid:%04x caps:%08x\n", modep->opos,
				 modep->fx->svid, mode_caps);
		}
	}
}

static int command_pe(int argc, char **argv)
{
	int port;
	char *e;

	if (argc < 3)
		return EC_ERROR_PARAM_COUNT;

	/* command: pe <port> <subcmd> <args> */
	port = strtoi(argv[1], &e, 10);
	if (*e || port >= CONFIG_USB_PD_PORT_MAX_COUNT)
		return EC_ERROR_PARAM2;
	if (!strncasecmp(argv[2], "dump", 4))
		dump_pe(port);

	return EC_SUCCESS;
}

DECLARE_CONSOLE_COMMAND(pe, command_pe,
			"<port> dump",
			"USB PE");
#endif /* CONFIG_CMD_USB_PD_PE */

static enum ec_status hc_remote_pd_discovery(struct host_cmd_handler_args *args)
{
	const uint8_t *port = args->params;
	struct ec_params_usb_pd_discovery_entry *r = args->response;

	if (*port >= CONFIG_USB_PD_PORT_MAX_COUNT)
		return EC_RES_INVALID_PARAM;

	r->vid = pd_get_identity_vid(*port);
	r->ptype = PD_IDH_PTYPE(pe[*port].am_policy.identity[0]);

	/* pid only included if vid is assigned */
	if (r->vid)
		r->pid = PD_PRODUCT_PID(pe[*port].am_policy.identity[2]);

	args->response_size = sizeof(*r);
	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_USB_PD_DISCOVERY,
		hc_remote_pd_discovery,
		EC_VER_MASK(0));

static enum ec_status hc_remote_pd_get_amode(struct host_cmd_handler_args *args)
{
	struct svdm_amode_data *modep;
	const struct ec_params_usb_pd_get_mode_request *p = args->params;
	struct ec_params_usb_pd_get_mode_response *r = args->response;

	if (p->port >= CONFIG_USB_PD_PORT_MAX_COUNT)
		return EC_RES_INVALID_PARAM;

	/* no more to send */
	if (p->svid_idx >= pe[p->port].am_policy.svid_cnt) {
		r->svid = 0;
		args->response_size = sizeof(r->svid);
		return EC_RES_SUCCESS;
	}

	r->svid = pe[p->port].am_policy.svids[p->svid_idx].svid;
	r->opos = 0;
	memcpy(r->vdo, pe[p->port].am_policy.svids[p->svid_idx].mode_vdo, 24);
	modep = get_modep(p->port, r->svid);

	if (modep)
		r->opos = pd_alt_mode(p->port, r->svid);

	args->response_size = sizeof(*r);
	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_USB_PD_GET_AMODE,
	hc_remote_pd_get_amode,
	EC_VER_MASK(0));

#endif /* CONFIG_USB_PD_ALT_MODE_DFP */

static const struct usb_state pe_states[] = {
	/* Super States */
	[PE_PRS_FRS_SHARED] = {
		.entry = pe_prs_frs_shared_entry,
		.exit  = pe_prs_frs_shared_exit,
	},

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
		.exit  = pe_src_ready_exit,
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
	[PE_SRC_VDM_IDENTITY_REQUEST] = {
		.entry = pe_src_vdm_identity_request_entry,
		.run = pe_src_vdm_identity_request_run,
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
		.exit  = pe_snk_ready_exit,
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
		.exit = pe_send_soft_reset_exit,
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
	[PE_GIVE_BATTERY_CAP] = {
		.entry = pe_give_battery_cap_entry,
		.run   = pe_give_battery_cap_run,
	},
	[PE_GIVE_BATTERY_STATUS] = {
		.entry = pe_give_battery_status_entry,
		.run   = pe_give_battery_status_run,
	},
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
	},
	[PE_PRS_SRC_SNK_EVALUATE_SWAP] = {
		.entry = pe_prs_src_snk_evaluate_swap_entry,
		.run   = pe_prs_src_snk_evaluate_swap_run,
	},
	[PE_PRS_SRC_SNK_TRANSITION_TO_OFF] = {
		.entry = pe_prs_src_snk_transition_to_off_entry,
		.run   = pe_prs_src_snk_transition_to_off_run,
	},
	[PE_PRS_SRC_SNK_WAIT_SOURCE_ON] = {
		.entry = pe_prs_src_snk_wait_source_on_entry,
		.run   = pe_prs_src_snk_wait_source_on_run,
	},
	[PE_PRS_SRC_SNK_SEND_SWAP] = {
		.entry = pe_prs_src_snk_send_swap_entry,
		.run   = pe_prs_src_snk_send_swap_run,
		.exit  = pe_prs_src_snk_send_swap_exit,
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
		.parent = &pe_states[PE_PRS_FRS_SHARED],
	},
	/* State actions are shared with PE_FRS_SNK_SRC_ASSERT_RP */
	[PE_PRS_SNK_SRC_ASSERT_RP] = {
		.entry = pe_prs_snk_src_assert_rp_entry,
		.run   = pe_prs_snk_src_assert_rp_run,
		.parent = &pe_states[PE_PRS_FRS_SHARED],
	},
	/* State actions are shared with PE_FRS_SNK_SRC_SOURCE_ON */
	[PE_PRS_SNK_SRC_SOURCE_ON] = {
		.entry = pe_prs_snk_src_source_on_entry,
		.run   = pe_prs_snk_src_source_on_run,
		.parent = &pe_states[PE_PRS_FRS_SHARED],
	},
	/* State actions are shared with PE_FRS_SNK_SRC_SEND_SWAP */
	[PE_PRS_SNK_SRC_SEND_SWAP] = {
		.entry = pe_prs_snk_src_send_swap_entry,
		.run   = pe_prs_snk_src_send_swap_run,
		.exit  = pe_prs_snk_src_send_swap_exit,
		.parent = &pe_states[PE_PRS_FRS_SHARED],
	},
	[PE_FRS_SNK_SRC_START_AMS] = {
		.entry = pe_frs_snk_src_start_ams_entry,
		.parent = &pe_states[PE_PRS_FRS_SHARED],
	},
	[PE_VCS_EVALUATE_SWAP] = {
		.entry = pe_vcs_evaluate_swap_entry,
		.run   = pe_vcs_evaluate_swap_run,
	},
	[PE_VCS_SEND_SWAP] = {
		.entry = pe_vcs_send_swap_entry,
		.run   = pe_vcs_send_swap_run,
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
	[PE_DO_PORT_DISCOVERY] = {
		.entry = pe_do_port_discovery_entry,
		.run   = pe_do_port_discovery_run,
	},
	[PE_VDM_REQUEST] = {
		.entry = pe_vdm_request_entry,
		.run   = pe_vdm_request_run,
		.exit  = pe_vdm_request_exit,
	},
	[PE_VDM_ACKED] = {
		.entry = pe_vdm_acked_entry,
	},
	[PE_VDM_RESPONSE] = {
		.entry = pe_vdm_response_entry,
		.run   = pe_vdm_response_run,
	},
	[PE_HANDLE_CUSTOM_VDM_REQUEST] = {
		.entry = pe_handle_custom_vdm_request_entry,
		.run   = pe_handle_custom_vdm_request_run
	},
	[PE_WAIT_FOR_ERROR_RECOVERY] = {
		.entry = pe_wait_for_error_recovery_entry,
		.run   = pe_wait_for_error_recovery_run,
	},
	[PE_BIST] = {
		.entry = pe_bist_entry,
		.run   = pe_bist_run,
	},
	[PE_DR_SNK_GET_SINK_CAP] = {
		.entry = pe_dr_snk_get_sink_cap_entry,
		.run   = pe_dr_snk_get_sink_cap_run,
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
