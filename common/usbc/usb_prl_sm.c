/* Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "battery.h"
#include "battery_smart.h"
#include "board.h"
#include "charge_manager.h"
#include "charge_state.h"
#include "chipset.h"
#include "common.h"
#include "console.h"
#include "ec_commands.h"
#include "gpio.h"
#include "hooks.h"
#include "host_command.h"
#include "registers.h"
#include "system.h"
#include "task.h"
#include "timer.h"
#include "tcpm.h"
#include "util.h"
#include "usb_charge.h"
#include "usb_mux.h"
#include "usb_pd.h"
#include "usb_pe_sm.h"
#include "usb_prl_sm.h"
#include "usb_tc_sm.h"
#include "usb_emsg.h"
#include "usb_sm.h"
#include "vpd_api.h"
#include "version.h"

#define RCH_SET_FLAG(port, flag) atomic_or(&rch[port].flags, (flag))
#define RCH_CLR_FLAG(port, flag) atomic_clear(&rch[port].flags, (flag))
#define RCH_CHK_FLAG(port, flag) (rch[port].flags & (flag))

#define TCH_SET_FLAG(port, flag) atomic_or(&tch[port].flags, (flag))
#define TCH_CLR_FLAG(port, flag) atomic_clear(&tch[port].flags, (flag))
#define TCH_CHK_FLAG(port, flag) (tch[port].flags & (flag))

#define PRL_TX_SET_FLAG(port, flag) atomic_or(&prl_tx[port].flags, (flag))
#define PRL_TX_CLR_FLAG(port, flag) atomic_clear(&prl_tx[port].flags, (flag))
#define PRL_TX_CHK_FLAG(port, flag) (prl_tx[port].flags & (flag))

#define PRL_HR_SET_FLAG(port, flag) atomic_or(&prl_hr[port].flags, (flag))
#define PRL_HR_CLR_FLAG(port, flag) atomic_clear(&prl_hr[port].flags, (flag))
#define PRL_HR_CHK_FLAG(port, flag) (prl_hr[port].flags & (flag))

#define PDMSG_SET_FLAG(port, flag) atomic_or(&pdmsg[port].flags, (flag))
#define PDMSG_CLR_FLAG(port, flag) atomic_clear(&pdmsg[port].flags, (flag))
#define PDMSG_CHK_FLAG(port, flag) (pdmsg[port].flags & (flag))

/* Protocol Layer Flags */
#define PRL_FLAGS_TX_COMPLETE             BIT(0)
#define PRL_FLAGS_START_AMS               BIT(1)
#define PRL_FLAGS_END_AMS                 BIT(2)
#define PRL_FLAGS_TX_ERROR                BIT(3)
#define PRL_FLAGS_PE_HARD_RESET           BIT(4)
#define PRL_FLAGS_HARD_RESET_COMPLETE     BIT(5)
#define PRL_FLAGS_PORT_PARTNER_HARD_RESET BIT(6)
#define PRL_FLAGS_MSG_XMIT                BIT(7)
#define PRL_FLAGS_MSG_RECEIVED            BIT(8)
#define PRL_FLAGS_ABORT                   BIT(9)
#define PRL_FLAGS_CHUNKING                BIT(10)

/* PD counter definitions */
#define PD_MESSAGE_ID_COUNT 7

static enum sm_local_state local_state[CONFIG_USB_PD_PORT_MAX_COUNT];

/* Protocol Transmit States (Section 6.11.2.2) */
enum usb_prl_tx_state {
	PRL_TX_PHY_LAYER_RESET,
	PRL_TX_WAIT_FOR_MESSAGE_REQUEST,
	PRL_TX_LAYER_RESET_FOR_TRANSMIT,
	PRL_TX_WAIT_FOR_PHY_RESPONSE,
	PRL_TX_SRC_SOURCE_TX,
	PRL_TX_SNK_START_AMS,
	PRL_TX_SRC_PENDING,
	PRL_TX_SNK_PENDING,
	PRL_TX_DISCARD_MESSAGE,
};

/* Protocol Hard Reset States (Section 6.11.2.4) */
enum usb_prl_hr_state {
	PRL_HR_WAIT_FOR_REQUEST,
	PRL_HR_RESET_LAYER,
	PRL_HR_WAIT_FOR_PHY_HARD_RESET_COMPLETE,
	PRL_HR_WAIT_FOR_PE_HARD_RESET_COMPLETE,
};

/* Chunked Rx states (Section 6.11.2.1.2) */
enum usb_rch_state {
	RCH_WAIT_FOR_MESSAGE_FROM_PROTOCOL_LAYER,
	RCH_PASS_UP_MESSAGE,
	RCH_PROCESSING_EXTENDED_MESSAGE,
	RCH_REQUESTING_CHUNK,
	RCH_WAITING_CHUNK,
	RCH_REPORT_ERROR,
};

/* Chunked Tx states (Section 6.11.2.1.3) */
enum usb_tch_state {
	TCH_WAIT_FOR_MESSAGE_REQUEST_FROM_PE,
	TCH_WAIT_FOR_TRANSMISSION_COMPLETE,
	TCH_CONSTRUCT_CHUNKED_MESSAGE,
	TCH_SENDING_CHUNKED_MESSAGE,
	TCH_WAIT_CHUNK_REQUEST,
	TCH_MESSAGE_RECEIVED,
	TCH_MESSAGE_SENT,
	TCH_REPORT_ERROR,
};

/* Forward declare full list of states. Index by above enums. */
static const struct usb_state prl_tx_states[];
static const struct usb_state prl_hr_states[];
static const struct usb_state rch_states[];
static const struct usb_state tch_states[];


/* Chunked Rx State Machine Object */
static struct rx_chunked {
	/* state machine context */
	struct sm_ctx ctx;
	/* PRL_FLAGS */
	uint32_t flags;
	/* protocol timer */
	uint64_t chunk_sender_response_timer;
} rch[CONFIG_USB_PD_PORT_MAX_COUNT];

/* Chunked Tx State Machine Object */
static struct tx_chunked {
	/* state machine context */
	struct sm_ctx ctx;
	/* state machine flags */
	uint32_t flags;
	/* protocol timer */
	uint64_t chunk_sender_request_timer;
	/* error to report when moving to tch_report_error state */
	enum pe_error error;
} tch[CONFIG_USB_PD_PORT_MAX_COUNT];

/* Message Reception State Machine Object */
static struct protocol_layer_rx {
	/* message ids for all valid port partners */
	int msg_id[NUM_SOP_STAR_TYPES];
} prl_rx[CONFIG_USB_PD_PORT_MAX_COUNT];

/* Message Transmission State Machine Object */
static struct protocol_layer_tx {
	/* state machine context */
	struct sm_ctx ctx;
	/* state machine flags */
	uint32_t flags;
	/* protocol timer */
	uint64_t sink_tx_timer;
	/* tcpc transmit timeout */
	uint64_t tcpc_tx_timeout;
	/* last message type we transmitted */
	enum tcpm_transmit_type last_xmit_type;
	/* message id counters for all 6 port partners */
	uint32_t msg_id_counter[NUM_SOP_STAR_TYPES];
	/* message retry counter */
	uint32_t retry_counter;
	/* transmit status */
	int xmit_status;
} prl_tx[CONFIG_USB_PD_PORT_MAX_COUNT];

/* Hard Reset State Machine Object */
static struct protocol_hard_reset {
	/* state machine context */
	struct sm_ctx ctx;
	/* state machine flags */
	uint32_t flags;
	/* protocol timer */
	uint64_t hard_reset_complete_timer;
} prl_hr[CONFIG_USB_PD_PORT_MAX_COUNT];

/* Chunking Message Object */
static struct pd_message {
	/* message status flags */
	uint32_t flags;
	/* SOP* */
	enum tcpm_transmit_type xmit_type;
	/* type of message */
	uint8_t msg_type;
	/* extended message */
	uint8_t ext;
	/* PD revision */
	enum pd_rev_type rev;
	/* Cable PD revision */
	enum pd_rev_type cable_rev;
	/* Number of 32-bit objects in chk_buf */
	uint16_t data_objs;
	/* temp chunk buffer */
	uint32_t chk_buf[7];
	uint32_t chunk_number_expected;
	uint32_t num_bytes_received;
	uint32_t chunk_number_to_send;
	uint32_t send_offset;
} pdmsg[CONFIG_USB_PD_PORT_MAX_COUNT];

struct extended_msg emsg[CONFIG_USB_PD_PORT_MAX_COUNT];

/* Common Protocol Layer Message Transmission */
static void prl_tx_construct_message(int port);
static void prl_rx_wait_for_phy_message(const int port, int evt);

/* Set the protocol transmit statemachine to a new state. */
static void set_state_prl_tx(const int port,
			     const enum usb_prl_tx_state new_state)
{
	set_state(port, &prl_tx[port].ctx, &prl_tx_states[new_state]);
}

/* Get the protocol transmit statemachine's current state. */
test_export_static enum usb_prl_tx_state prl_tx_get_state(const int port)
{
	return prl_tx[port].ctx.current - &prl_tx_states[0];
}

/* Set the hard reset statemachine to a new state. */
static void set_state_prl_hr(const int port,
			     const enum usb_prl_hr_state new_state)
{
	set_state(port, &prl_hr[port].ctx, &prl_hr_states[new_state]);
}

#ifdef TEST_BUILD
/* Get the hard reset statemachine's current state. */
enum usb_prl_hr_state prl_hr_get_state(const int port)
{
	return prl_hr[port].ctx.current - &prl_hr_states[0];
}
#endif

/* Set the chunked Rx statemachine to a new state. */
static void set_state_rch(const int port, const enum usb_rch_state new_state)
{
	set_state(port, &rch[port].ctx, &rch_states[new_state]);
}

/* Get the chunked Rx statemachine's current state. */
test_export_static enum usb_rch_state rch_get_state(const int port)
{
	return rch[port].ctx.current - &rch_states[0];
}

/* Set the chunked Tx statemachine to a new state. */
static void set_state_tch(const int port, const enum usb_tch_state new_state)
{
	set_state(port, &tch[port].ctx, &tch_states[new_state]);
}

/* Get the chunked Tx statemachine's current state. */
test_export_static enum usb_tch_state tch_get_state(const int port)
{
	return tch[port].ctx.current - &tch_states[0];
}

void pd_transmit_complete(int port, int status)
{
	prl_tx[port].xmit_status = status;
}

void pd_execute_hard_reset(int port)
{
	/* Only allow async. function calls when state machine is running */
	if (!prl_is_running(port))
		return;

	PRL_HR_SET_FLAG(port, PRL_FLAGS_PORT_PARTNER_HARD_RESET);
	set_state_prl_hr(port, PRL_HR_RESET_LAYER);
	task_set_event(PD_PORT_TO_TASK_ID(port), PD_EVENT_SM, 0);
}

void prl_execute_hard_reset(int port)
{
	/* Only allow async. function calls when state machine is running */
	if (!prl_is_running(port))
		return;

	PRL_HR_SET_FLAG(port, PRL_FLAGS_PE_HARD_RESET);
	set_state_prl_hr(port, PRL_HR_RESET_LAYER);
	task_set_event(PD_PORT_TO_TASK_ID(port), PD_EVENT_SM, 0);
}

int prl_is_running(int port)
{
	return local_state[port] == SM_RUN;
}

static void prl_init(int port)
{
	int i;
	const struct sm_ctx cleared = {};

	prl_tx[port].flags = 0;
	prl_tx[port].last_xmit_type = TCPC_TX_SOP;
	prl_tx[port].xmit_status = TCPC_TX_UNSET;

	tch[port].flags = 0;
	rch[port].flags = 0;

	/*
	 * Initialize to highest revision supported. If the port or cable
	 * partner doesn't support this revision, the Protocol Engine will
	 * lower this value to the revision supported by the partner.
	 */
	pdmsg[port].cable_rev = PD_REV30;
	pdmsg[port].rev = PD_REV30;
	pdmsg[port].flags = 0;

	prl_hr[port].flags = 0;

	for (i = 0; i < NUM_SOP_STAR_TYPES; i++) {
		prl_rx[port].msg_id[i] = -1;
		prl_tx[port].msg_id_counter[i] = 0;
	}

	/* Clear state machines and set initial states */
	prl_tx[port].ctx = cleared;
	set_state_prl_tx(port, PRL_TX_PHY_LAYER_RESET);

	rch[port].ctx = cleared;
	set_state_rch(port, RCH_WAIT_FOR_MESSAGE_FROM_PROTOCOL_LAYER);

	tch[port].ctx = cleared;
	set_state_tch(port, TCH_WAIT_FOR_MESSAGE_REQUEST_FROM_PE);

	prl_hr[port].ctx = cleared;
	set_state_prl_hr(port, PRL_HR_WAIT_FOR_REQUEST);
}

void prl_start_ams(int port)
{
	PRL_TX_SET_FLAG(port, PRL_FLAGS_START_AMS);
}

void prl_end_ams(int port)
{
	PRL_TX_SET_FLAG(port, PRL_FLAGS_END_AMS);
}

void prl_hard_reset_complete(int port)
{
	PRL_HR_SET_FLAG(port, PRL_FLAGS_HARD_RESET_COMPLETE);
	task_set_event(PD_PORT_TO_TASK_ID(port), PD_EVENT_SM, 0);
}

void prl_send_ctrl_msg(int port,
		      enum tcpm_transmit_type type,
		      enum pd_ctrl_msg_type msg)
{
	pdmsg[port].xmit_type = type;
	pdmsg[port].msg_type = msg;
	pdmsg[port].ext = 0;
	emsg[port].len = 0;

	TCH_SET_FLAG(port, PRL_FLAGS_MSG_XMIT);
	task_set_event(PD_PORT_TO_TASK_ID(port), PD_EVENT_SM, 0);
}

void prl_send_data_msg(int port,
		      enum tcpm_transmit_type type,
		      enum pd_data_msg_type msg)
{
	pdmsg[port].xmit_type = type;
	pdmsg[port].msg_type = msg;
	pdmsg[port].ext = 0;

	TCH_SET_FLAG(port, PRL_FLAGS_MSG_XMIT);
	task_set_event(PD_PORT_TO_TASK_ID(port), PD_EVENT_SM, 0);
}

void prl_send_ext_data_msg(int port,
			  enum tcpm_transmit_type type,
			  enum pd_ext_msg_type msg)
{
	pdmsg[port].xmit_type = type;
	pdmsg[port].msg_type = msg;
	pdmsg[port].ext = 1;

	TCH_SET_FLAG(port, PRL_FLAGS_MSG_XMIT);
	task_set_event(PD_PORT_TO_TASK_ID(port), PD_EVENT_SM, 0);
}

void prl_reset(int port)
{
	local_state[port] = SM_INIT;
}

void prl_run(int port, int evt, int en)
{
	switch (local_state[port]) {
	case SM_PAUSED:
		if (!en)
			break;
		/* fall through */
	case SM_INIT:
		prl_init(port);
		local_state[port] = SM_RUN;
		/* fall through */
	case SM_RUN:
		/* If disabling, wait until message is sent. */
		if (!en && tch_get_state(port) ==
				   TCH_WAIT_FOR_MESSAGE_REQUEST_FROM_PE) {

			/* Disable RX */
			if (IS_ENABLED(CONFIG_USB_TYPEC_CTVPD) ||
			    IS_ENABLED(CONFIG_USB_TYPEC_VPD))
				vpd_rx_enable(0);
			else
				tcpm_set_rx_enable(port, 0);

			local_state[port] = SM_PAUSED;
			break;
		}

		/* Run Protocol Layer Message Reception */
		prl_rx_wait_for_phy_message(port, evt);

		/* Run RX Chunked state machine */
		run_state(port, &rch[port].ctx);

		/* Run TX Chunked state machine */
		run_state(port, &tch[port].ctx);

		/* Run Protocol Layer Message Transmission state machine */
		run_state(port, &prl_tx[port].ctx);

		/* Run Protocol Layer Hard Reset state machine */
		run_state(port, &prl_hr[port].ctx);
		break;
	}
}

void prl_set_rev(int port, enum pd_rev_type rev)
{
	pdmsg[port].rev = rev;
}

enum pd_rev_type prl_get_rev(int port)
{
	return pdmsg[port].rev;
}

void prl_set_cable_rev(int port, enum pd_rev_type rev)
{
	pdmsg[port].cable_rev = rev;
}

enum pd_rev_type prl_get_cable_rev(int port)
{
	return pdmsg[port].cable_rev;
}

/* Common Protocol Layer Message Transmission */
static void prl_tx_phy_layer_reset_entry(const int port)
{
	if (IS_ENABLED(CONFIG_USB_TYPEC_CTVPD)
	 || IS_ENABLED(CONFIG_USB_TYPEC_VPD)) {
		vpd_rx_enable(1);
	} else {
		tcpm_init(port);
		tcpm_set_rx_enable(port, 1);
	}
}

static void prl_tx_phy_layer_reset_run(const int port)
{
	set_state_prl_tx(port, PRL_TX_WAIT_FOR_MESSAGE_REQUEST);
}

static void prl_tx_wait_for_message_request_entry(const int port)
{
	/* Reset RetryCounter */
	prl_tx[port].retry_counter = 0;
}

static void prl_tx_wait_for_message_request_run(const int port)
{
	if (PRL_TX_CHK_FLAG(port, PRL_FLAGS_MSG_XMIT)) {
		PRL_TX_CLR_FLAG(port, PRL_FLAGS_MSG_XMIT);
		/*
		 * Soft Reset Message Message pending
		 */
		if ((pdmsg[port].msg_type == PD_CTRL_SOFT_RESET) &&
							(emsg[port].len == 0)) {
			set_state_prl_tx(port, PRL_TX_LAYER_RESET_FOR_TRANSMIT);
		}
		/*
		 * Message pending (except Soft Reset)
		 */
		else {
			/* NOTE: PRL_TX_Construct_Message State embedded here */
			prl_tx_construct_message(port);
			set_state_prl_tx(port, PRL_TX_WAIT_FOR_PHY_RESPONSE);
		}

		return;
	} else if ((pdmsg[port].rev == PD_REV30) && PRL_TX_CHK_FLAG(port,
				(PRL_FLAGS_START_AMS | PRL_FLAGS_END_AMS))) {
		if (tc_get_power_role(port) == PD_ROLE_SOURCE) {
			/*
			 * Start of AMS notification received from
			 * Policy Engine
			 */
			if (PRL_TX_CHK_FLAG(port, PRL_FLAGS_START_AMS)) {
				PRL_TX_CLR_FLAG(port, PRL_FLAGS_START_AMS);
				set_state_prl_tx(port, PRL_TX_SRC_SOURCE_TX);
				return;
			}
			/*
			 * End of AMS notification received from
			 * Policy Engine
			 */
			else if (PRL_TX_CHK_FLAG(port, PRL_FLAGS_END_AMS)) {
				PRL_TX_CLR_FLAG(port, PRL_FLAGS_END_AMS);
				/* Set Rp = SinkTxOk */
				tcpm_select_rp_value(port, SINK_TX_OK);
				tcpm_set_cc(port, TYPEC_CC_RP);
				prl_tx[port].retry_counter = 0;
				prl_tx[port].flags = 0;
			}
		} else {
			if (PRL_TX_CHK_FLAG(port, PRL_FLAGS_START_AMS)) {
				PRL_TX_CLR_FLAG(port, PRL_FLAGS_START_AMS);
				/*
				 * First Message in AMS notification
				 * received from Policy Engine.
				 */
				set_state_prl_tx(port, PRL_TX_SNK_START_AMS);
				return;
			}
		}
	}
}

static void increment_msgid_counter(int port)
{
	/* If the last message wasn't an SOP* message, no need to increment */
	if (prl_tx[port].last_xmit_type >= NUM_SOP_STAR_TYPES)
		return;

	prl_tx[port].msg_id_counter[prl_tx[port].last_xmit_type] =
		(prl_tx[port].msg_id_counter[prl_tx[port].last_xmit_type] + 1) &
		PD_MESSAGE_ID_COUNT;
}

/*
 * PrlTxDiscard
 */
static void prl_tx_discard_message_entry(const int port)
{
	/* Increment msgidCounter */
	increment_msgid_counter(port);
	set_state_prl_tx(port, PRL_TX_PHY_LAYER_RESET);
}

/*
 * PrlTxSrcSourceTx
 */
static void prl_tx_src_source_tx_entry(const int port)
{
	/* Set Rp = SinkTxNG */
	tcpm_select_rp_value(port, SINK_TX_NG);
	tcpm_set_cc(port, TYPEC_CC_RP);
}

static void prl_tx_src_source_tx_run(const int port)
{
	if (PRL_TX_CHK_FLAG(port, PRL_FLAGS_MSG_XMIT)) {
		PRL_TX_CLR_FLAG(port, PRL_FLAGS_MSG_XMIT);

		set_state_prl_tx(port, PRL_TX_SRC_PENDING);
	}
}

/*
 * PrlTxSnkStartAms
 */
static void prl_tx_snk_start_ams_run(const int port)
{
	if (PRL_TX_CHK_FLAG(port, PRL_FLAGS_MSG_XMIT)) {
		PRL_TX_CLR_FLAG(port, PRL_FLAGS_MSG_XMIT);

		set_state_prl_tx(port, PRL_TX_SNK_PENDING);
	}
}

/*
 * PrlTxLayerResetForTransmit
 */
static void prl_tx_layer_reset_for_transmit_entry(const int port)
{
	int i;

	/* Reset MessageIdCounters */
	for (i = 0; i < NUM_SOP_STAR_TYPES; i++)
		prl_tx[port].msg_id_counter[i] = 0;
}

static void prl_tx_layer_reset_for_transmit_run(const int port)
{
	/* NOTE: PRL_Tx_Construct_Message State embedded here */
	prl_tx_construct_message(port);
	set_state_prl_tx(port, PRL_TX_WAIT_FOR_PHY_RESPONSE);
}

static uint32_t get_sop_star_header(const int port)
{
	const int is_sop_packet = pdmsg[port].xmit_type == TCPC_TX_SOP;

	/* SOP vs SOP'/SOP" headers are different. Replace fields as needed */
	return PD_HEADER(
		pdmsg[port].msg_type,
		is_sop_packet ?
			tc_get_power_role(port) : tc_get_cable_plug(port),
		is_sop_packet ?
			tc_get_data_role(port) : 0,
		prl_tx[port].msg_id_counter[pdmsg[port].xmit_type],
		pdmsg[port].data_objs,
		is_sop_packet ?
			pdmsg[port].rev : pdmsg[port].cable_rev,
		pdmsg[port].ext);
}

static void prl_tx_construct_message(const int port)
{
	/* The header is unused for hard reset, etc. */
	const uint32_t header = pdmsg[port].xmit_type < NUM_SOP_STAR_TYPES ?
		get_sop_star_header(port) : 0;

	/* Save SOP* so the correct msg_id_counter can be incremented */
	prl_tx[port].last_xmit_type = pdmsg[port].xmit_type;

	/*
	 * These flags could be set if this function is called before the
	 * Policy Engine is informed of the previous transmission. Clear the
	 * flags so that this message can be sent.
	 */
	prl_tx[port].xmit_status = TCPC_TX_UNSET;
	PDMSG_CLR_FLAG(port, PRL_FLAGS_TX_COMPLETE);

	/* Pass message to PHY Layer */
	tcpm_transmit(port, pdmsg[port].xmit_type, header,
						pdmsg[port].chk_buf);
}

/*
 * PrlTxWaitForPhyResponse
 */
static void prl_tx_wait_for_phy_response_entry(const int port)
{
	prl_tx[port].tcpc_tx_timeout = get_time().val + PD_T_TCPC_TX_TIMEOUT;
}

static void prl_tx_wait_for_phy_response_run(const int port)
{
	/* Wait until TX is complete */

	/*
	 * NOTE: The TCPC will set xmit_status to TCPC_TX_COMPLETE_DISCARDED
	 *       when a GoodCRC containing an incorrect MessageID is received.
	 *       This condition satifies the PRL_Tx_Match_MessageID state
	 *       requirement.
	 */

	if (get_time().val > prl_tx[port].tcpc_tx_timeout ||
		prl_tx[port].xmit_status == TCPC_TX_COMPLETE_FAILED ||
		prl_tx[port].xmit_status == TCPC_TX_COMPLETE_DISCARDED) {

		/* NOTE: PRL_Tx_Check_RetryCounter State embedded here. */

		/* Increment check RetryCounter */
		prl_tx[port].retry_counter++;

		/*
		 * (RetryCounter > nRetryCount) | Large Extended Message
		 */
		if (prl_tx[port].retry_counter > N_RETRY_COUNT ||
					(pdmsg[port].ext &&
					PD_EXT_HEADER_DATA_SIZE(GET_EXT_HEADER(
					pdmsg[port].chk_buf[0]) > 26))) {

			/*
			 * NOTE: PRL_Tx_Transmission_Error State embedded
			 * here.
			 */

			/*
			 * State tch_wait_for_transmission_complete will
			 * inform policy engine of error
			 */
			PDMSG_SET_FLAG(port, PRL_FLAGS_TX_ERROR);

			/* Increment message id counter */
			increment_msgid_counter(port);
			set_state_prl_tx(port, PRL_TX_WAIT_FOR_MESSAGE_REQUEST);
		} else {
			/*
			 * NOTE: PRL_TX_Construct_Message State embedded
			 * here.
			 */
			/* Try to resend the message. */
			prl_tx_construct_message(port);
		}
	} else if (prl_tx[port].xmit_status == TCPC_TX_COMPLETE_SUCCESS) {
		/* NOTE: PRL_TX_Message_Sent State embedded here. */

		/* Increment messageId counter */
		increment_msgid_counter(port);
		/* Inform Policy Engine Message was sent */
		PDMSG_SET_FLAG(port, PRL_FLAGS_TX_COMPLETE);
		set_state_prl_tx(port, PRL_TX_WAIT_FOR_MESSAGE_REQUEST);
	}
}

static void prl_tx_wait_for_phy_response_exit(const int port)
{
	prl_tx[port].xmit_status = TCPC_TX_UNSET;
}

/* Source Protocol Layer Message Transmission */
/*
 * PrlTxSrcPending
 */
static void prl_tx_src_pending_entry(const int port)
{
	/* Start SinkTxTimer */
	prl_tx[port].sink_tx_timer = get_time().val + PD_T_SINK_TX;
}

static void prl_tx_src_pending_run(const int port)
{

	if (get_time().val > prl_tx[port].sink_tx_timer) {
		/*
		 * Soft Reset Message pending &
		 * SinkTxTimer timeout
		 */
		if ((emsg[port].len == 0) &&
			(pdmsg[port].msg_type == PD_CTRL_SOFT_RESET)) {
			set_state_prl_tx(port, PRL_TX_LAYER_RESET_FOR_TRANSMIT);
		}
		/* Message pending (except Soft Reset) &
		 * SinkTxTimer timeout
		 */
		else {
			prl_tx_construct_message(port);
			set_state_prl_tx(port, PRL_TX_WAIT_FOR_PHY_RESPONSE);
		}

		return;
	}
}

/*
 * PrlTxSnkPending
 */
static void prl_tx_snk_pending_run(const int port)
{
	enum tcpc_cc_voltage_status cc1, cc2;

	tcpm_get_cc(port, &cc1, &cc2);
	if (cc1 == TYPEC_CC_VOLT_RP_3_0 || cc2 == TYPEC_CC_VOLT_RP_3_0) {
		/*
		 * Soft Reset Message Message pending &
		 * Rp = SinkTxOk
		 */
		if ((pdmsg[port].msg_type == PD_CTRL_SOFT_RESET) &&
					(emsg[port].len == 0)) {
			set_state_prl_tx(port, PRL_TX_LAYER_RESET_FOR_TRANSMIT);
		}
		/*
		 * Message pending (except Soft Reset) &
		 * Rp = SinkTxOk
		 */
		else {
			prl_tx_construct_message(port);
			set_state_prl_tx(port, PRL_TX_WAIT_FOR_PHY_RESPONSE);
		}
		return;
	}
}

/* Hard Reset Operation */

static void prl_hr_wait_for_request_entry(const int port)
{
	prl_hr[port].flags = 0;
}

static void prl_hr_wait_for_request_run(const int port)
{
	if (PRL_HR_CHK_FLAG(port, PRL_FLAGS_PE_HARD_RESET |
				PRL_FLAGS_PORT_PARTNER_HARD_RESET))
		set_state_prl_hr(port, PRL_HR_RESET_LAYER);
}

/*
 * PrlHrResetLayer
 */
static void prl_hr_reset_layer_entry(const int port)
{
	int i;

	/* reset messageIDCounters */
	for (i = 0; i < NUM_SOP_STAR_TYPES; i++)
		prl_tx[port].msg_id_counter[i] = 0;
	/*
	 * Protocol Layer message transmission transitions to
	 * PRL_Tx_Wait_For_Message_Request state.
	 */
	set_state_prl_tx(port, PRL_TX_WAIT_FOR_MESSAGE_REQUEST);

	tch[port].flags = 0;
	rch[port].flags = 0;
	pdmsg[port].flags = 0;

	/* Reset message ids */
	for (i = 0; i < NUM_SOP_STAR_TYPES; i++) {
		prl_rx[port].msg_id[i] = -1;
		prl_tx[port].msg_id_counter[i] = 0;
	}

	/* Disable RX */
	if (IS_ENABLED(CONFIG_USB_TYPEC_CTVPD) ||
	    IS_ENABLED(CONFIG_USB_TYPEC_VPD))
		vpd_rx_enable(0);
	else
		tcpm_set_rx_enable(port, 0);

	return;
}

static void prl_hr_reset_layer_run(const int port)
{
	/*
	 * Protocol Layer reset Complete &
	 * Hard Reset was initiated by Policy Engine
	 */
	if (PRL_HR_CHK_FLAG(port, PRL_FLAGS_PE_HARD_RESET)) {
		/* Request PHY to perform a Hard Reset */
		prl_send_ctrl_msg(port, TCPC_TX_HARD_RESET, 0);
		set_state_prl_hr(port, PRL_HR_WAIT_FOR_PHY_HARD_RESET_COMPLETE);
	}
	/*
	 * Protocol Layer reset complete &
	 * Hard Reset was initiated by Port Partner
	 */
	else {
		/* Inform Policy Engine of the Hard Reset */
		pe_got_hard_reset(port);
		set_state_prl_hr(port, PRL_HR_WAIT_FOR_PE_HARD_RESET_COMPLETE);
	}
}

/*
 * PrlHrWaitForPhyHardResetComplete
 */
static void prl_hr_wait_for_phy_hard_reset_complete_entry(const int port)
{
	/* Start HardResetCompleteTimer */
	prl_hr[port].hard_reset_complete_timer =
			get_time().val + PD_T_PS_HARD_RESET;
}

static void prl_hr_wait_for_phy_hard_reset_complete_run(const int port)
{
	/*
	 * Wait for hard reset from PHY
	 * or timeout
	 */
	if (PDMSG_CHK_FLAG(port, PRL_FLAGS_TX_COMPLETE) ||
	    (get_time().val > prl_hr[port].hard_reset_complete_timer)) {
		/* PRL_HR_PHY_Hard_Reset_Requested */

		/* Inform Policy Engine Hard Reset was sent */
		pe_hard_reset_sent(port);
		set_state_prl_hr(port, PRL_HR_WAIT_FOR_PE_HARD_RESET_COMPLETE);

		return;
	}
}

/*
 * PrlHrWaitForPeHardResetComplete
 */
static void prl_hr_wait_for_pe_hard_reset_complete_run(const int port)
{
	/*
	 * Wait for Hard Reset complete indication from Policy Engine
	 */
	if (PRL_HR_CHK_FLAG(port, PRL_FLAGS_HARD_RESET_COMPLETE))
		set_state_prl_hr(port, PRL_HR_WAIT_FOR_REQUEST);
}

static void prl_hr_wait_for_pe_hard_reset_complete_exit(const int port)
{
	/* Exit from Hard Reset */

	set_state_prl_tx(port, PRL_TX_PHY_LAYER_RESET);
	set_state_rch(port, RCH_WAIT_FOR_MESSAGE_FROM_PROTOCOL_LAYER);
	set_state_tch(port, TCH_WAIT_FOR_MESSAGE_REQUEST_FROM_PE);
}

static void copy_chunk_to_ext(int port)
{
	/* Calculate number of bytes */
	pdmsg[port].num_bytes_received = (PD_HEADER_CNT(emsg[port].header) * 4);

	/* Copy chunk into extended message */
	memcpy((uint8_t *)emsg[port].buf, (uint8_t *)pdmsg[port].chk_buf,
		pdmsg[port].num_bytes_received);

	/* Set extended message length */
	emsg[port].len = pdmsg[port].num_bytes_received;
}

/*
 * Chunked Rx State Machine
 */
/*
 * RchWaitForMessageFromProtocolLayer
 */
static void rch_wait_for_message_from_protocol_layer_entry(const int port)
{
	/* Clear Abort flag */
	PDMSG_CLR_FLAG(port, PRL_FLAGS_ABORT);

	/* All Messages are chunked */
	rch[port].flags = PRL_FLAGS_CHUNKING;
}

static void rch_wait_for_message_from_protocol_layer_run(const int port)
{
	if (RCH_CHK_FLAG(port, PRL_FLAGS_MSG_RECEIVED)) {
		RCH_CLR_FLAG(port, PRL_FLAGS_MSG_RECEIVED);
		/*
		 * Are we communicating with a PD3.0 device and is
		 * this an extended message?
		 */
		if (pdmsg[port].rev == PD_REV30 &&
					PD_HEADER_EXT(emsg[port].header)) {
			uint16_t exhdr = GET_EXT_HEADER(*pdmsg[port].chk_buf);
			uint8_t chunked = PD_EXT_HEADER_CHUNKED(exhdr);

			/*
			 * Received Extended Message &
			 * (Chunking = 1 & Chunked = 1)
			 */
			if ((RCH_CHK_FLAG(port, PRL_FLAGS_CHUNKING)) &&
								chunked) {
				/*
				 * RCH_Processing_Extended_Message first chunk
				 * entry processing embedded here
				 *
				 * This is the first chunk:
				 * Set Chunk_number_expected = 0 and
				 * Num_Bytes_Received = 0
				 */
				pdmsg[port].chunk_number_expected = 0;
				pdmsg[port].num_bytes_received = 0;
				pdmsg[port].msg_type =
					PD_HEADER_TYPE(emsg[port].header);

				set_state_rch(port,
					      RCH_PROCESSING_EXTENDED_MESSAGE);
			}
			/*
			 * (Received Extended Message &
			 * (Chunking = 0 & Chunked = 0))
			 */
			else if (!RCH_CHK_FLAG(port, PRL_FLAGS_CHUNKING) &&
								!chunked) {
				/* Copy chunk to extended buffer */
				copy_chunk_to_ext(port);
				set_state_rch(port, RCH_PASS_UP_MESSAGE);
			}
			/*
			 * Chunked != Chunking
			 */
			else {
				set_state_rch(port, RCH_REPORT_ERROR);
			}
		}
		/*
		 * Received Non-Extended Message
		 */
		else if (!PD_HEADER_EXT(emsg[port].header)) {
			/* Copy chunk to extended buffer */
			copy_chunk_to_ext(port);
			set_state_rch(port, RCH_PASS_UP_MESSAGE);
		}
		/*
		 * Received an Extended Message while communicating at a
		 * revision lower than PD3.0
		 */
		else {
			set_state_rch(port, RCH_REPORT_ERROR);
		}
	}
}

/*
 * RchPassUpMessage
 */
static void rch_pass_up_message_entry(const int port)
{
	/* Pass Message to Policy Engine */
	pe_message_received(port);
	set_state_rch(port, RCH_WAIT_FOR_MESSAGE_FROM_PROTOCOL_LAYER);
}

/*
 * RchProcessingExtendedMessage
 */
static void rch_processing_extended_message_run(const int port)
{
	uint16_t exhdr = GET_EXT_HEADER(pdmsg[port].chk_buf[0]);
	uint8_t chunk_num = PD_EXT_HEADER_CHUNK_NUM(exhdr);
	uint32_t data_size = PD_EXT_HEADER_DATA_SIZE(exhdr);
	uint32_t byte_num;

	/*
	 * Abort Flag Set
	 */
	if (PDMSG_CHK_FLAG(port, PRL_FLAGS_ABORT))
		set_state_rch(port, RCH_WAIT_FOR_MESSAGE_FROM_PROTOCOL_LAYER);

	/*
	 * If expected Chunk Number:
	 *   Append data to Extended_Message_Buffer
	 *   Increment Chunk_number_Expected
	 *   Adjust Num Bytes Received
	 */
	else if (chunk_num == pdmsg[port].chunk_number_expected) {
		byte_num = data_size - pdmsg[port].num_bytes_received;

		if (byte_num > 25)
			byte_num = 26;

		/* Make sure extended message buffer does not overflow */
		if (pdmsg[port].num_bytes_received +
					byte_num > EXTENDED_BUFFER_SIZE) {
			set_state_rch(port, RCH_REPORT_ERROR);
			return;
		}

		/* Append data */
		/* Add 2 to chk_buf to skip over extended message header */
		memcpy(((uint8_t *)emsg[port].buf +
				pdmsg[port].num_bytes_received),
				(uint8_t *)pdmsg[port].chk_buf + 2, byte_num);
		/* increment chunk number expected */
		pdmsg[port].chunk_number_expected++;
		/* adjust num bytes received */
		pdmsg[port].num_bytes_received += byte_num;

		/* Was that the last chunk? */
		if (pdmsg[port].num_bytes_received >= data_size) {
			emsg[port].len = pdmsg[port].num_bytes_received;
			 /* Pass Message to Policy Engine */
			set_state_rch(port, RCH_PASS_UP_MESSAGE);
		}
		/*
		 * Message not Complete
		 */
		else
			set_state_rch(port, RCH_REQUESTING_CHUNK);
	}
	/*
	 * Unexpected Chunk Number
	 */
	else
		set_state_rch(port, RCH_REPORT_ERROR);
}

/*
 * RchRequestingChunk
 */
static void rch_requesting_chunk_entry(const int port)
{
	/*
	 * Send Chunk Request to Protocol Layer
	 * with chunk number = Chunk_Number_Expected
	 */
	pdmsg[port].chk_buf[0] = PD_EXT_HEADER(
				pdmsg[port].chunk_number_expected,
				1, /* Request Chunk */
				0 /* Data Size */
				);

	pdmsg[port].data_objs = 1;
	pdmsg[port].ext = 1;
	PRL_TX_SET_FLAG(port, PRL_FLAGS_MSG_XMIT);
	task_set_event(PD_PORT_TO_TASK_ID(port), PD_EVENT_TX, 0);
}

static void rch_requesting_chunk_run(const int port)
{
	/*
	 * Message Transmitted received from Protocol Layer
	 */
	if (PDMSG_CHK_FLAG(port, PRL_FLAGS_TX_COMPLETE)) {
		PDMSG_CLR_FLAG(port, PRL_FLAGS_TX_COMPLETE);
		set_state_rch(port, RCH_WAITING_CHUNK);
	}
	/*
	 * Transmission Error from Protocol Layer or
	 * Message Received From Protocol Layer
	 */
	else if (RCH_CHK_FLAG(port, PRL_FLAGS_MSG_RECEIVED) ||
			PDMSG_CHK_FLAG(port, PRL_FLAGS_TX_ERROR)) {
		/*
		 * Leave PRL_FLAGS_MSG_RECEIVED flag set. It'll be
		 * cleared in rch_report_error state
		 */
		set_state_rch(port, RCH_REPORT_ERROR);
	}
}

/*
 * RchWaitingChunk
 */
static void rch_waiting_chunk_entry(const int port)
{
	/*
	 * Start ChunkSenderResponseTimer
	 */
	rch[port].chunk_sender_response_timer =
		get_time().val + PD_T_CHUNK_SENDER_RESPONSE;
}

static void rch_waiting_chunk_run(const int port)
{
	if (RCH_CHK_FLAG(port, PRL_FLAGS_MSG_RECEIVED)) {
		/*
		 * Leave PRL_FLAGS_MSG_RECEIVED flag set just in case an error
		 * is detected. If an error is detected, PRL_FLAGS_MSG_RECEIVED
		 * will be cleared in rch_report_error state.
		 */

		if (PD_HEADER_EXT(emsg[port].header)) {
			uint16_t exhdr = GET_EXT_HEADER(pdmsg[port].chk_buf[0]);
			/*
			 * Other Message Received from Protocol Layer
			 */
			if (PD_EXT_HEADER_REQ_CHUNK(exhdr) ||
			    !PD_EXT_HEADER_CHUNKED(exhdr)) {
				set_state_rch(port, RCH_REPORT_ERROR);
			}
			/*
			 * Chunk response Received from Protocol Layer
			 */
			else {
				/*
				 * No error wad detected, so clear
				 * PRL_FLAGS_MSG_RECEIVED flag.
				 */
				RCH_CLR_FLAG(port, PRL_FLAGS_MSG_RECEIVED);
				set_state_rch(port,
					      RCH_PROCESSING_EXTENDED_MESSAGE);
			}
		}
	}
	/*
	 * ChunkSenderResponseTimer Timeout
	 */
	else if (get_time().val > rch[port].chunk_sender_response_timer) {
		set_state_rch(port, RCH_REPORT_ERROR);
	}
}

/*
 * RchReportError
 */
static void rch_report_error_entry(const int port)
{
	/*
	 * If the state was entered because a message was received,
	 * this message is passed to the Policy Engine.
	 */
	if (RCH_CHK_FLAG(port, PRL_FLAGS_MSG_RECEIVED)) {
		RCH_CLR_FLAG(port, PRL_FLAGS_MSG_RECEIVED);

		/* Copy chunk to extended buffer */
		copy_chunk_to_ext(port);
		/* Pass Message to Policy Engine */
		pe_message_received(port);
		/* Report error */
		pe_report_error(port, ERR_RCH_MSG_REC);
	} else {
		/* Report error */
		pe_report_error(port, ERR_RCH_CHUNKED);
	}
}

static void rch_report_error_run(const int port)
{
	set_state_rch(port, RCH_WAIT_FOR_MESSAGE_FROM_PROTOCOL_LAYER);
}

/*
 * Chunked Tx State Machine
 */

/*
 * TchWaitForMessageRequestFromPe
 */
static void tch_wait_for_message_request_from_pe_entry(const int port)
{
	/* Clear Abort flag */
	PDMSG_CLR_FLAG(port, PRL_FLAGS_ABORT);

	/* All Messages are chunked */
	tch[port].flags = PRL_FLAGS_CHUNKING;
}

static void tch_wait_for_message_request_from_pe_run(const int port)
{
	/*
	 * Any message received and not in state TCH_Wait_Chunk_Request
	 */
	if (TCH_CHK_FLAG(port, PRL_FLAGS_MSG_RECEIVED)) {
		TCH_CLR_FLAG(port, PRL_FLAGS_MSG_RECEIVED);
		set_state_tch(port, TCH_MESSAGE_RECEIVED);
	} else if (TCH_CHK_FLAG(port, PRL_FLAGS_MSG_XMIT)) {
		TCH_CLR_FLAG(port, PRL_FLAGS_MSG_XMIT);
		/*
		 * Rx Chunking State != RCH_Wait_For_Message_From_Protocol_Layer
		 * & Abort Supported
		 *
		 * Discard the Message
		 */
		if (rch_get_state(port) !=
				RCH_WAIT_FOR_MESSAGE_FROM_PROTOCOL_LAYER) {
			tch[port].error = ERR_TCH_XMIT;
			set_state_tch(port, TCH_REPORT_ERROR);
		} else {
			/*
			 * Extended Message Request & Chunking
			 */
			if ((pdmsg[port].rev == PD_REV30) && pdmsg[port].ext &&
			     TCH_CHK_FLAG(port, PRL_FLAGS_CHUNKING)) {
				/*
				 * NOTE: TCH_Prepare_To_Send_Chunked_Message
				 * embedded here.
				 */
				pdmsg[port].send_offset = 0;
				pdmsg[port].chunk_number_to_send = 0;
				set_state_tch(port,
					      TCH_CONSTRUCT_CHUNKED_MESSAGE);
			} else
			/*
			 * Non-Extended Message Request
			 */
			{
				/* Make sure buffer doesn't overflow */
				if (emsg[port].len > BUFFER_SIZE) {
					tch[port].error = ERR_TCH_XMIT;
					set_state_tch(port, TCH_REPORT_ERROR);
					return;
				}

				/* NOTE: TCH_Pass_Down_Message embedded here */
				/* Copy message to chunked buffer */
				memset((uint8_t *)pdmsg[port].chk_buf,
					0, BUFFER_SIZE);
				memcpy((uint8_t *)pdmsg[port].chk_buf,
					(uint8_t *)emsg[port].buf,
					emsg[port].len);
				/*
				 * Pad length to 4-byte boundery and
				 * convert to number of 32-bit objects.
				 * Since the value is shifted right by 2,
				 * no need to explicitly clear the lower
				 * 2-bits.
				 */
				pdmsg[port].data_objs =
						(emsg[port].len + 3) >> 2;
				/* Pass Message to Protocol Layer */
				PRL_TX_SET_FLAG(port, PRL_FLAGS_MSG_XMIT);
				set_state_tch(port,
					TCH_WAIT_FOR_TRANSMISSION_COMPLETE);
			}
		}
	}
}

/*
 * TchWaitForTransmissionComplete
 */
static void tch_wait_for_transmission_complete_run(const int port)
{
	/*
	 * Inform Policy Engine that Message was sent.
	 */
	if (PDMSG_CHK_FLAG(port, PRL_FLAGS_TX_COMPLETE)) {
		PDMSG_CLR_FLAG(port, PRL_FLAGS_TX_COMPLETE);
		set_state_tch(port, TCH_MESSAGE_SENT);
	}
	/*
	 * Inform Policy Engine of Tx Error
	 */
	else if (PDMSG_CHK_FLAG(port, PRL_FLAGS_TX_ERROR)) {
		PDMSG_CLR_FLAG(port, PRL_FLAGS_TX_ERROR);
		tch[port].error = ERR_TCH_XMIT;
		set_state_tch(port, TCH_REPORT_ERROR);
	}
}

/*
 * TchConstructChunkedMessage
 */
static void tch_construct_chunked_message_entry(const int port)
{
	uint16_t *ext_hdr;
	uint8_t *data;
	uint16_t num;

	/*
	 * Any message received and not in state TCH_Wait_Chunk_Request
	 */
	if (TCH_CHK_FLAG(port, PRL_FLAGS_MSG_RECEIVED)) {
		TCH_CLR_FLAG(port, PRL_FLAGS_MSG_RECEIVED);
		set_state_tch(port, TCH_MESSAGE_RECEIVED);
		return;
	}

	/* Prepare to copy chunk into chk_buf */

	ext_hdr = (uint16_t *)pdmsg[port].chk_buf;
	data = ((uint8_t *)pdmsg[port].chk_buf + 2);
	num = emsg[port].len - pdmsg[port].send_offset;

	if (num > 26)
		num = 26;

	/* Set the chunks extended header */
	*ext_hdr = PD_EXT_HEADER(pdmsg[port].chunk_number_to_send,
				 0, /* Chunk Request */
				 emsg[port].len);

	/* Copy the message chunk into chk_buf */
	memset(data, 0, 28);
	memcpy(data, emsg[port].buf + pdmsg[port].send_offset, num);
	pdmsg[port].send_offset += num;

	/*
	 * Add in 2 bytes for extended header
	 * pad out to 4-byte boundary
	 * convert to number of 4-byte words
	 * Since the value is shifted right by 2,
	 * no need to explicitly clear the lower
	 * 2-bits.
	 */
	pdmsg[port].data_objs = (num + 2 + 3) >> 2;

	/* Pass message chunk to Protocol Layer */
	PRL_TX_SET_FLAG(port, PRL_FLAGS_MSG_XMIT);
}

static void tch_construct_chunked_message_run(const int port)
{
	if (PDMSG_CHK_FLAG(port, PRL_FLAGS_ABORT))
		set_state_tch(port, TCH_WAIT_FOR_MESSAGE_REQUEST_FROM_PE);
	else
		set_state_tch(port, TCH_SENDING_CHUNKED_MESSAGE);
}

/*
 * TchSendingChunkedMessage
 */
static void tch_sending_chunked_message_run(const int port)
{
	/*
	 * Transmission Error
	 */
	if (PDMSG_CHK_FLAG(port, PRL_FLAGS_TX_ERROR)) {
		tch[port].error = ERR_TCH_XMIT;
		set_state_tch(port, TCH_REPORT_ERROR);
	}
	/*
	 * Message Transmitted from Protocol Layer &
	 * Last Chunk
	 */
	else if (emsg[port].len == pdmsg[port].send_offset) {
		set_state_tch(port, TCH_MESSAGE_SENT);
	}
	/*
	 * Any message received and not in state TCH_Wait_Chunk_Request
	 */
	else if (TCH_CHK_FLAG(port, PRL_FLAGS_MSG_RECEIVED)) {
		TCH_CLR_FLAG(port, PRL_FLAGS_MSG_RECEIVED);
		set_state_tch(port, TCH_MESSAGE_RECEIVED);
	}
	/*
	 * Message Transmitted from Protocol Layer &
	 * Not Last Chunk
	 */
	else
		set_state_tch(port, TCH_WAIT_CHUNK_REQUEST);
}

/*
 * TchWaitChunkRequest
 */
static void tch_wait_chunk_request_entry(const int port)
{
	/* Increment Chunk Number to Send */
	pdmsg[port].chunk_number_to_send++;
	/* Start Chunk Sender Request Timer */
	tch[port].chunk_sender_request_timer =
		get_time().val + PD_T_CHUNK_SENDER_REQUEST;
}

static void tch_wait_chunk_request_run(const int port)
{
	if (TCH_CHK_FLAG(port, PRL_FLAGS_MSG_RECEIVED)) {
		TCH_CLR_FLAG(port, PRL_FLAGS_MSG_RECEIVED);

		if (PD_HEADER_EXT(emsg[port].header)) {
			uint16_t exthdr;

			exthdr = GET_EXT_HEADER(pdmsg[port].chk_buf[0]);
			if (PD_EXT_HEADER_REQ_CHUNK(exthdr)) {
				/*
				 * Chunk Request Received &
				 * Chunk Number = Chunk Number to Send
				 */
				if (PD_EXT_HEADER_CHUNK_NUM(exthdr) ==
				    pdmsg[port].chunk_number_to_send) {
					set_state_tch(port,
						TCH_CONSTRUCT_CHUNKED_MESSAGE);
				}
				/*
				 * Chunk Request Received &
				 * Chunk Number != Chunk Number to Send
				 */
				else {
					tch[port].error = ERR_TCH_CHUNKED;
					set_state_tch(port, TCH_REPORT_ERROR);
				}
				return;
			}
		}

		/*
		 * Other message received
		 */
		set_state_tch(port, TCH_MESSAGE_RECEIVED);
	}
	/*
	 * ChunkSenderRequestTimer timeout
	 */
	else if (get_time().val >=
			tch[port].chunk_sender_request_timer) {
		set_state_tch(port, TCH_MESSAGE_SENT);
	}
}

/*
 * TchMessageReceived
 */
static void tch_message_received_entry(const int port)
{
	/* Pass message to chunked Rx */
	RCH_SET_FLAG(port, PRL_FLAGS_MSG_RECEIVED);
}

static void tch_message_received_run(const int port)
{
	set_state_tch(port, TCH_WAIT_FOR_MESSAGE_REQUEST_FROM_PE);
}

/*
 * TchMessageSent
 */
static void tch_message_sent_entry(const int port)
{
	/* Tell PE message was sent */
	pe_message_sent(port);
	set_state_tch(port, TCH_WAIT_FOR_MESSAGE_REQUEST_FROM_PE);
}

/*
 * TchReportError
 */
static void tch_report_error_entry(const int port)
{
	/* Report Error To Policy Engine */
	pe_report_error(port, tch[port].error);
	set_state_tch(port, TCH_WAIT_FOR_MESSAGE_REQUEST_FROM_PE);
}

/*
 * Protocol Layer Message Reception State Machine
 */
static void prl_rx_wait_for_phy_message(const int port, int evt)
{
	uint32_t header;
	uint8_t type;
	uint8_t cnt;
	uint8_t sop;
	int8_t msid;

	/* If we don't have any message, just stop processing now. */
	if (!tcpm_has_pending_message(port) ||
	    tcpm_dequeue_message(port, pdmsg[port].chk_buf, &header))
		return;

	emsg[port].header = header;
	type = PD_HEADER_TYPE(header);
	cnt = PD_HEADER_CNT(header);
	msid = PD_HEADER_ID(header);
	sop = PD_HEADER_GET_SOP(header);

	/*
	 * Ignore messages sent to the cable from our
	 * port partner if we aren't Vconn powered device.
	 */
	if (!IS_ENABLED(CONFIG_USB_TYPEC_CTVPD) &&
	    !IS_ENABLED(CONFIG_USB_TYPEC_VPD) &&
	    PD_HEADER_GET_SOP(header) != PD_MSG_SOP &&
	    PD_HEADER_PROLE(header) == PD_PLUG_FROM_DFP_UFP)
		return;

	if (cnt == 0 && type == PD_CTRL_SOFT_RESET) {
		int i;

		for (i = 0; i < NUM_SOP_STAR_TYPES; i++) {
			/* Clear MessageIdCounter */
			prl_tx[port].msg_id_counter[i] = 0;
			/* Clear stored MessageID value */
			prl_rx[port].msg_id[i] = -1;
		}

		/* Inform Policy Engine of Soft Reset */
		pe_got_soft_reset(port);

		/* Soft Reset occurred */
		set_state_prl_tx(port, PRL_TX_PHY_LAYER_RESET);
		set_state_rch(port, RCH_WAIT_FOR_MESSAGE_FROM_PROTOCOL_LAYER);
		set_state_tch(port, TCH_WAIT_FOR_MESSAGE_REQUEST_FROM_PE);
	}

	/*
	 * Ignore if this is a duplicate message. Stop processing.
	 */
	if (prl_rx[port].msg_id[sop] == msid)
		return;

	/*
	 * Discard any pending tx message if this is
	 * not a ping message
	 */
	if ((pdmsg[port].rev == PD_REV30) &&
	   (cnt == 0) && type != PD_CTRL_PING) {
		if (prl_tx_get_state(port) == PRL_TX_SRC_PENDING ||
		    prl_tx_get_state(port) == PRL_TX_SNK_PENDING)
			set_state_prl_tx(port, PRL_TX_DISCARD_MESSAGE);
	}

	/* Store Message Id */
	prl_rx[port].msg_id[sop] = msid;

	/* RTR Chunked Message Router States. */
	/*
	 * Received Ping from Protocol Layer
	 */
	if (cnt == 0 && type == PD_CTRL_PING) {
		/* NOTE: RTR_PING State embedded here. */
		emsg[port].len = 0;
		pe_message_received(port);
		return;
	}
	/*
	 * Message (not Ping) Received from
	 * Protocol Layer & Doing Tx Chunks
	 */
	else if (tch_get_state(port) != TCH_WAIT_FOR_MESSAGE_REQUEST_FROM_PE &&
		tch_get_state(port) != TCH_WAIT_FOR_TRANSMISSION_COMPLETE) {
		/* NOTE: RTR_TX_CHUNKS State embedded here. */
		/*
		 * Send Message to Tx Chunk
		 * Chunk State Machine
		 */
		TCH_SET_FLAG(port, PRL_FLAGS_MSG_RECEIVED);
	}
	/*
	 * Message (not Ping) Received from
	 * Protocol Layer & Not Doing Tx Chunks
	 */
	else {
		/* NOTE: RTR_RX_CHUNKS State embedded here. */
		/*
		 * Send Message to Rx
		 * Chunk State Machine
		 */
		RCH_SET_FLAG(port, PRL_FLAGS_MSG_RECEIVED);
	}

	task_set_event(PD_PORT_TO_TASK_ID(port), PD_EVENT_SM, 0);
}

/* All necessary Protocol Transmit States (Section 6.11.2.2) */
static const struct usb_state prl_tx_states[] = {
	[PRL_TX_PHY_LAYER_RESET] = {
		.entry  = prl_tx_phy_layer_reset_entry,
		.run    = prl_tx_phy_layer_reset_run,
	},
	[PRL_TX_WAIT_FOR_MESSAGE_REQUEST] = {
		.entry  = prl_tx_wait_for_message_request_entry,
		.run    = prl_tx_wait_for_message_request_run,
	},
	[PRL_TX_LAYER_RESET_FOR_TRANSMIT] = {
		.entry  = prl_tx_layer_reset_for_transmit_entry,
		.run    = prl_tx_layer_reset_for_transmit_run,
	},
	[PRL_TX_WAIT_FOR_PHY_RESPONSE] = {
		.entry  = prl_tx_wait_for_phy_response_entry,
		.run    = prl_tx_wait_for_phy_response_run,
		.exit   = prl_tx_wait_for_phy_response_exit,
	},
	[PRL_TX_SRC_SOURCE_TX] = {
		.entry  = prl_tx_src_source_tx_entry,
		.run    = prl_tx_src_source_tx_run,
	},
	[PRL_TX_SNK_START_AMS] = {
		.run    = prl_tx_snk_start_ams_run,
	},
	[PRL_TX_SRC_PENDING] = {
		.entry  = prl_tx_src_pending_entry,
		.run    = prl_tx_src_pending_run,
	},
	[PRL_TX_SNK_PENDING] = {
		.run    = prl_tx_snk_pending_run,
	},
	[PRL_TX_DISCARD_MESSAGE] = {
		.entry  = prl_tx_discard_message_entry,
	},
};

/* All necessary Protocol Hard Reset States (Section 6.11.2.4) */
static const struct usb_state prl_hr_states[] = {
	[PRL_HR_WAIT_FOR_REQUEST] = {
		.entry  = prl_hr_wait_for_request_entry,
		.run    = prl_hr_wait_for_request_run,
	},
	[PRL_HR_RESET_LAYER] = {
		.entry  = prl_hr_reset_layer_entry,
		.run    = prl_hr_reset_layer_run,
	},
	[PRL_HR_WAIT_FOR_PHY_HARD_RESET_COMPLETE] = {
		.entry  = prl_hr_wait_for_phy_hard_reset_complete_entry,
		.run    = prl_hr_wait_for_phy_hard_reset_complete_run,
	},
	[PRL_HR_WAIT_FOR_PE_HARD_RESET_COMPLETE] = {
		.run    = prl_hr_wait_for_pe_hard_reset_complete_run,
		.exit   = prl_hr_wait_for_pe_hard_reset_complete_exit,
	},
};

/* All necessary Chunked Rx states (Section 6.11.2.1.2) */
static const struct usb_state rch_states[] = {
	[RCH_WAIT_FOR_MESSAGE_FROM_PROTOCOL_LAYER] = {
		.entry  = rch_wait_for_message_from_protocol_layer_entry,
		.run    = rch_wait_for_message_from_protocol_layer_run,
	},
	[RCH_PASS_UP_MESSAGE] = {
		.entry  = rch_pass_up_message_entry,
	},
	[RCH_PROCESSING_EXTENDED_MESSAGE] = {
		.run    = rch_processing_extended_message_run,
	},
	[RCH_REQUESTING_CHUNK] = {
		.entry  = rch_requesting_chunk_entry,
		.run    = rch_requesting_chunk_run,
	},
	[RCH_WAITING_CHUNK] = {
		.entry  = rch_waiting_chunk_entry,
		.run    = rch_waiting_chunk_run,
	},
	[RCH_REPORT_ERROR] = {
		.entry  = rch_report_error_entry,
		.run    = rch_report_error_run,
	},
};

/* All necessary Chunked Tx states (Section 6.11.2.1.3) */
static const struct usb_state tch_states[] = {
	[TCH_WAIT_FOR_MESSAGE_REQUEST_FROM_PE] = {
		.entry  = tch_wait_for_message_request_from_pe_entry,
		.run    = tch_wait_for_message_request_from_pe_run,
	},
	[TCH_WAIT_FOR_TRANSMISSION_COMPLETE] = {
		.run    = tch_wait_for_transmission_complete_run,
	},
	[TCH_CONSTRUCT_CHUNKED_MESSAGE] = {
		.entry  = tch_construct_chunked_message_entry,
		.run    = tch_construct_chunked_message_run,
	},
	[TCH_SENDING_CHUNKED_MESSAGE] = {
		.run    = tch_sending_chunked_message_run,
	},
	[TCH_WAIT_CHUNK_REQUEST] = {
		.entry  = tch_wait_chunk_request_entry,
		.run    = tch_wait_chunk_request_run,
	},
	[TCH_MESSAGE_RECEIVED] = {
		.entry  = tch_message_received_entry,
		.run    = tch_message_received_run,
	},
	[TCH_MESSAGE_SENT] = {
		.entry  = tch_message_sent_entry,
	},
	[TCH_REPORT_ERROR] = {
		.entry  = tch_report_error_entry,
	},
};

#ifdef TEST_BUILD
const struct test_sm_data test_prl_sm_data[] = {
	{
		.base = prl_tx_states,
		.size = ARRAY_SIZE(prl_tx_states),
	},
	{
		.base = prl_hr_states,
		.size = ARRAY_SIZE(prl_hr_states),
	},
	{
		.base = rch_states,
		.size = ARRAY_SIZE(rch_states),
	},
	{
		.base = tch_states,
		.size = ARRAY_SIZE(tch_states),
	},
};
const int test_prl_sm_data_size = ARRAY_SIZE(test_prl_sm_data);
#endif

