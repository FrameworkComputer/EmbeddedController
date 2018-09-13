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

#define RCH_OBJ(port)	(SM_OBJ(rch[port]))
#define TCH_OBJ(port)	(SM_OBJ(tch[port]))
#define PRL_TX_OBJ(port)   (SM_OBJ(prl_tx[port]))
#define PRL_HR_OBJ(port)   (SM_OBJ(prl_hr[port]))

#define RCH_TEST_OBJ(port) (SM_OBJ(rch[(port)].obj))
#define TCH_TEST_OBJ(port) (SM_OBJ(tch[(port)].obj))
#define PRL_TX_TEST_OBJ(port) (SM_OBJ(prl_tx[(port)].obj))
#define PRL_HR_TEST_OBJ(port) (SM_OBJ(prl_hr[(port)].obj))

static enum sm_local_state local_state[CONFIG_USB_PD_PORT_COUNT] = {SM_INIT};

/* Chunked Rx State Machine Object */
static struct rx_chunked {
	/* struct sm_obj must be first. */
	struct sm_obj obj;
	/* state id */
	enum rch_state_id state_id;
	/* PRL_FLAGS */
	uint32_t flags;
	/* protocol timer */
	uint64_t chunk_sender_response_timer;
} rch[CONFIG_USB_PD_PORT_COUNT];

/* Chunked Tx State Machine Object */
static struct tx_chunked {
	/* struct sm_obj must be first. */
	struct sm_obj obj;
	/* state id */
	enum tch_state_id state_id;
	/* state machine flags */
	uint32_t flags;
	/* protocol timer */
	uint64_t chunk_sender_request_timer;
} tch[CONFIG_USB_PD_PORT_COUNT];

/* Message Reception State Machine Object */
static struct protocol_layer_rx {
	/* message ids for all valid port partners */
	int msg_id[NUM_XMIT_TYPES];
} prl_rx[CONFIG_USB_PD_PORT_COUNT];

/* Message Transmission State Machine Object */
static struct protocol_layer_tx {
	/* struct sm_obj must be first. */
	struct sm_obj obj;
	/* state id */
	enum prl_tx_state_id state_id;
	/* state machine flags */
	uint32_t flags;
	/* protocol timer */
	uint64_t sink_tx_timer;
	/* tcpc transmit timeout */
	uint64_t tcpc_tx_timeout;
	/* Last SOP* we transmitted to */
	uint8_t sop;
	/* message id counters for all 6 port partners */
	uint32_t msg_id_counter[NUM_XMIT_TYPES];
	/* message retry counter */
	uint32_t retry_counter;
	/* transmit status */
	int xmit_status;
} prl_tx[CONFIG_USB_PD_PORT_COUNT];

/* Hard Reset State Machine Object */
static struct protocol_hard_reset {
	/* struct sm_obj must be first. */
	struct sm_obj obj;
	/* state id */
	enum prl_hr_state_id state_id;
	/* state machine flags */
	uint32_t flags;
	/* protocol timer */
	uint64_t hard_reset_complete_timer;
} prl_hr[CONFIG_USB_PD_PORT_COUNT];

/* Chunking Message Object */
static struct pd_message {
	/* message status flags */
	uint32_t status_flags;

	/* SOP* */
	enum tcpm_transmit_type xmit_type;
	/* type of message */
	uint8_t msg_type;
	/* extended message */
	uint8_t ext;
	/* PD revision */
	enum pd_rev_type rev;
	/* Number of 32-bit objects in chk_buf */
	uint16_t data_objs;
	/* temp chunk buffer */
	uint32_t chk_buf[7];
	uint32_t chunk_number_expected;
	uint32_t num_bytes_received;
	uint32_t chunk_number_to_send;
	uint32_t send_offset;
} pdmsg[CONFIG_USB_PD_PORT_COUNT];

struct extended_msg emsg[CONFIG_USB_PD_PORT_COUNT];

/* Protocol Layer States */
/* Common Protocol Layer Message Transmission */
static void  prl_tx_construct_message(int port);

static unsigned int prl_tx_phy_layer_reset(int port, enum signal sig);
static unsigned int prl_tx_phy_layer_reset_entry(int port);
static unsigned int prl_tx_phy_layer_reset_run(int port);

static unsigned int prl_tx_wait_for_message_request(int port, enum signal sig);
static unsigned int prl_tx_wait_for_message_request_entry(int port);
static unsigned int prl_tx_wait_for_message_request_run(int port);

static unsigned int prl_tx_layer_reset_for_transmit(int port, enum signal sig);
static unsigned int prl_tx_layer_reset_for_transmit_entry(int port);
static unsigned int prl_tx_layer_reset_for_transmit_run(int port);

static unsigned int prl_tx_wait_for_phy_response(int port, enum signal sig);
static unsigned int prl_tx_wait_for_phy_response_entry(int port);
static unsigned int prl_tx_wait_for_phy_response_run(int port);
static unsigned int prl_tx_wait_for_phy_response_exit(int port);

static unsigned int prl_tx_src_source_tx(int port, enum signal sig);
static unsigned int prl_tx_src_source_tx_entry(int port);
static unsigned int prl_tx_src_source_tx_run(int port);

static unsigned int prl_tx_snk_start_ams(int port, enum signal sig);
static unsigned int prl_tx_snk_start_ams_entry(int port);
static unsigned int prl_tx_snk_start_ams_run(int port);

/* Source Protocol Layser Message Transmission */
static unsigned int prl_tx_src_pending(int port, enum signal sig);
static unsigned int prl_tx_src_pending_entry(int port);
static unsigned int prl_tx_src_pending_run(int port);

/* Sink Protocol Layer Message Transmission */
static unsigned int prl_tx_snk_pending(int port, enum signal sig);
static unsigned int prl_tx_snk_pending_entry(int port);
static unsigned int prl_tx_snk_pending_run(int port);

static unsigned int prl_tx_discard_message(int port, enum signal sig);
static unsigned int prl_tx_discard_message_entry(int port);
static unsigned int prl_tx_discard_message_run(int port);

/* Protocol Layer Message Reception */
static unsigned int prl_rx_wait_for_phy_message(int port, int evt);

/* Hard Reset Operation */
static unsigned int prl_hr_wait_for_request(int port, enum signal sig);
static unsigned int prl_hr_wait_for_request_entry(int port);
static unsigned int prl_hr_wait_for_request_run(int port);

static unsigned int prl_hr_reset_layer(int port, enum signal sig);
static unsigned int prl_hr_reset_layer_entry(int port);
static unsigned int prl_hr_reset_layer_run(int port);

static unsigned int
	prl_hr_wait_for_phy_hard_reset_complete(int port, enum signal sig);
static unsigned int prl_hr_wait_for_phy_hard_reset_complete_entry(int port);
static unsigned int prl_hr_wait_for_phy_hard_reset_complete_run(int port);

static unsigned int
	prl_hr_wait_for_pe_hard_reset_complete(int port, enum signal sig);
static unsigned int prl_hr_wait_for_pe_hard_reset_complete_entry(int port);
static unsigned int prl_hr_wait_for_pe_hard_reset_complete_run(int port);
static unsigned int prl_hr_wait_for_pe_hard_reset_complete_exit(int port);

/* Chunked Rx */
static unsigned int
	rch_wait_for_message_from_protocol_layer(int port, enum signal sig);
static unsigned int rch_wait_for_message_from_protocol_layer_entry(int port);
static unsigned int rch_wait_for_message_from_protocol_layer_run(int port);

static unsigned int rch_processing_extended_message(int port, enum signal sig);
static unsigned int rch_processing_extended_message_entry(int port);
static unsigned int rch_processing_extended_message_run(int port);

static unsigned int rch_requesting_chunk(int port, enum signal sig);
static unsigned int rch_requesting_chunk_entry(int port);
static unsigned int rch_requesting_chunk_run(int port);

static unsigned int rch_waiting_chunk(int port, enum signal sig);
static unsigned int rch_waiting_chunk_entry(int port);
static unsigned int rch_waiting_chunk_run(int port);

static unsigned int rch_report_error(int port, enum signal sig);
static unsigned int rch_report_error_entry(int port);
static unsigned int rch_report_error_run(int port);

/* Chunked Tx */
static unsigned int
	tch_wait_for_message_request_from_pe(int port, enum signal sig);
static unsigned int tch_wait_for_message_request_from_pe_entry(int port);
static unsigned int tch_wait_for_message_request_from_pe_run(int port);

static unsigned int
	tch_wait_for_transmission_complete(int port, enum signal sig);
static unsigned int tch_wait_for_transmission_complete_entry(int port);
static unsigned int tch_wait_for_transmission_complete_run(int port);

static unsigned int tch_construct_chunked_message(int port, enum signal sig);
static unsigned int tch_construct_chunked_message_entry(int port);
static unsigned int tch_construct_chunked_message_run(int port);

static unsigned int tch_sending_chunked_message(int port, enum signal sig);
static unsigned int tch_sending_chunked_message_entry(int port);
static unsigned int tch_sending_chunked_message_run(int port);

static unsigned int tch_wait_chunk_request(int port, enum signal sig);
static unsigned int tch_wait_chunk_request_entry(int port);
static unsigned int tch_wait_chunk_request_run(int port);

static unsigned int tch_message_received(int port, enum signal sig);
static unsigned int tch_message_received_entry(int port);
static unsigned int tch_message_received_run(int port);

static unsigned int do_nothing_exit(int port);
static unsigned int get_super_state(int port);

static const state_sig prl_tx_phy_layer_reset_sig[] = {
	prl_tx_phy_layer_reset_entry,
	prl_tx_phy_layer_reset_run,
	do_nothing_exit,
	get_super_state
};

static const state_sig prl_tx_wait_for_message_request_sig[] = {
	prl_tx_wait_for_message_request_entry,
	prl_tx_wait_for_message_request_run,
	do_nothing_exit,
	get_super_state
};

static const state_sig prl_tx_layer_reset_for_transmit_sig[] = {
	prl_tx_layer_reset_for_transmit_entry,
	prl_tx_layer_reset_for_transmit_run,
	do_nothing_exit,
	get_super_state
};

static const state_sig prl_tx_wait_for_phy_response_sig[] = {
	prl_tx_wait_for_phy_response_entry,
	prl_tx_wait_for_phy_response_run,
	prl_tx_wait_for_phy_response_exit,
	get_super_state
};

static const state_sig prl_tx_src_source_tx_sig[] = {
	prl_tx_src_source_tx_entry,
	prl_tx_src_source_tx_run,
	do_nothing_exit,
	get_super_state
};

static const state_sig prl_tx_snk_start_ams_sig[] = {
	prl_tx_snk_start_ams_entry,
	prl_tx_snk_start_ams_run,
	do_nothing_exit,
	get_super_state
};

/* Source Protocol Layser Message Transmission */
static const state_sig prl_tx_src_pending_sig[] = {
	prl_tx_src_pending_entry,
	prl_tx_src_pending_run,
	do_nothing_exit,
	get_super_state
};

/* Sink Protocol Layer Message Transmission */
static const state_sig prl_tx_snk_pending_sig[] = {
	prl_tx_snk_pending_entry,
	prl_tx_snk_pending_run,
	do_nothing_exit,
	get_super_state
};

static const state_sig prl_tx_discard_message_sig[] = {
	prl_tx_discard_message_entry,
	prl_tx_discard_message_run,
	do_nothing_exit,
	get_super_state
};

/* Hard Reset Operation */
static const state_sig prl_hr_wait_for_request_sig[] = {
	prl_hr_wait_for_request_entry,
	prl_hr_wait_for_request_run,
	do_nothing_exit,
	get_super_state
};

static const state_sig prl_hr_reset_layer_sig[] = {
	prl_hr_reset_layer_entry,
	prl_hr_reset_layer_run,
	do_nothing_exit,
	get_super_state
};

static const state_sig prl_hr_wait_for_phy_hard_reset_complete_sig[] = {
	prl_hr_wait_for_phy_hard_reset_complete_entry,
	prl_hr_wait_for_phy_hard_reset_complete_run,
	do_nothing_exit,
	get_super_state
};

static const state_sig prl_hr_wait_for_pe_hard_reset_complete_sig[] = {
	prl_hr_wait_for_pe_hard_reset_complete_entry,
	prl_hr_wait_for_pe_hard_reset_complete_run,
	prl_hr_wait_for_pe_hard_reset_complete_exit,
	get_super_state
};

/* Chunked Rx */
static const state_sig rch_wait_for_message_from_protocol_layer_sig[] = {
	rch_wait_for_message_from_protocol_layer_entry,
	rch_wait_for_message_from_protocol_layer_run,
	do_nothing_exit,
	get_super_state
};

static const state_sig rch_processing_extended_message_sig[] = {
	rch_processing_extended_message_entry,
	rch_processing_extended_message_run,
	do_nothing_exit,
	get_super_state
};

static const state_sig rch_requesting_chunk_sig[] = {
	rch_requesting_chunk_entry,
	rch_requesting_chunk_run,
	do_nothing_exit,
	get_super_state
};

static const state_sig rch_waiting_chunk_sig[] = {
	rch_waiting_chunk_entry,
	rch_waiting_chunk_run,
	do_nothing_exit,
	get_super_state
};

static const state_sig rch_report_error_sig[] = {
	rch_report_error_entry,
	rch_report_error_run,
	do_nothing_exit,
	get_super_state
};

/* Chunked Tx */
static const state_sig tch_wait_for_message_request_from_pe_sig[] = {
	tch_wait_for_message_request_from_pe_entry,
	tch_wait_for_message_request_from_pe_run,
	do_nothing_exit,
	get_super_state
};

static const state_sig tch_wait_for_transmission_complete_sig[] = {
	tch_wait_for_transmission_complete_entry,
	tch_wait_for_transmission_complete_run,
	do_nothing_exit,
	get_super_state
};

static const state_sig tch_construct_chunked_message_sig[] = {
	tch_construct_chunked_message_entry,
	tch_construct_chunked_message_run,
	do_nothing_exit,
	get_super_state
};

static const state_sig tch_sending_chunked_message_sig[] = {
	tch_sending_chunked_message_entry,
	tch_sending_chunked_message_run,
	do_nothing_exit,
	get_super_state
};

static const state_sig tch_wait_chunk_request_sig[] = {
	tch_wait_chunk_request_entry,
	tch_wait_chunk_request_run,
	do_nothing_exit,
	get_super_state
};

static const state_sig tch_message_received_sig[] = {
	tch_message_received_entry,
	tch_message_received_run,
	do_nothing_exit,
	get_super_state
};

void pd_transmit_complete(int port, int status)
{
	prl_tx[port].xmit_status = status;
}

void pd_execute_hard_reset(int port)
{
	/* Only allow async. function calls when state machine is running */
	if (local_state[port] != SM_RUN)
		return;

	prl_hr[port].flags |= PRL_FLAGS_PORT_PARTNER_HARD_RESET;
	set_state(port, PRL_HR_OBJ(port), prl_hr_reset_layer);
	task_set_event(PD_PORT_TO_TASK_ID(port), PD_EVENT_SM, 0);
}

void prl_execute_hard_reset(int port)
{
	/* Only allow async. function calls when state machine is running */
	if (local_state[port] !=  SM_RUN)
		return;

	prl_hr[port].flags |= PRL_FLAGS_PE_HARD_RESET;
	set_state(port, PRL_HR_OBJ(port), prl_hr_reset_layer);
	task_set_event(PD_PORT_TO_TASK_ID(port), PD_EVENT_SM, 0);
}

void prl_init(int port)
{
	int i;

	prl_tx[port].flags = 0;
	prl_tx[port].xmit_status = TCPC_TX_UNSET;

	tch[port].flags = 0;
	rch[port].flags = 0;

	/*
	 * Initialize to highest revision supported. If the port partner
	 * doesn't support this revision, the Protocol Engine will lower
	 * this value to the revision supported by the port partner.
	 */
	pdmsg[port].rev = PD_REV30;
	pdmsg[port].status_flags = 0;

	prl_hr[port].flags = 0;

	for (i = 0; i < NUM_XMIT_TYPES; i++) {
		prl_rx[port].msg_id[i] = -1;
		prl_tx[port].msg_id_counter[i] = 0;
	}

	init_state(port, PRL_TX_OBJ(port), prl_tx_phy_layer_reset);
	init_state(port, RCH_OBJ(port),
				rch_wait_for_message_from_protocol_layer);
	init_state(port, TCH_OBJ(port), tch_wait_for_message_request_from_pe);
	init_state(port, PRL_HR_OBJ(port), prl_hr_wait_for_request);
}

enum rch_state_id get_rch_state_id(int port)
{
	return rch[port].state_id;
}

enum tch_state_id get_tch_state_id(int port)
{
	return tch[port].state_id;
}

enum prl_tx_state_id get_prl_tx_state_id(int port)
{
	return prl_tx[port].state_id;
}

enum prl_hr_state_id get_prl_hr_state_id(int port)
{
	return prl_hr[port].state_id;
}

void prl_start_ams(int port)
{
	prl_tx[port].flags |= PRL_FLAGS_START_AMS;
}

void prl_end_ams(int port)
{
	prl_tx[port].flags |= PRL_FLAGS_END_AMS;
}

void prl_hard_reset_complete(int port)
{
	prl_hr[port].flags |= PRL_FLAGS_HARD_RESET_COMPLETE;
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

	tch[port].flags |= PRL_FLAGS_MSG_XMIT;
	task_set_event(PD_PORT_TO_TASK_ID(port), PD_EVENT_SM, 0);
}

void prl_send_data_msg(int port,
		      enum tcpm_transmit_type type,
		      enum pd_data_msg_type msg)
{
	pdmsg[port].xmit_type = type;
	pdmsg[port].msg_type = msg;
	pdmsg[port].ext = 0;

	tch[port].flags |= PRL_FLAGS_MSG_XMIT;
	task_set_event(PD_PORT_TO_TASK_ID(port), PD_EVENT_SM, 0);
}

void prl_send_ext_data_msg(int port,
			  enum tcpm_transmit_type type,
			  enum pd_ext_msg_type msg)
{
	pdmsg[port].xmit_type = type;
	pdmsg[port].msg_type = msg;
	pdmsg[port].ext = 1;

	tch[port].flags |= PRL_FLAGS_MSG_XMIT;
	task_set_event(PD_PORT_TO_TASK_ID(port), PD_EVENT_SM, 0);
}

void prl_reset(int port)
{
	local_state[port] = SM_INIT;
}

void protocol_layer(int port, int evt, int en)
{
	switch (local_state[port]) {
	case SM_INIT:
		prl_init(port);
		local_state[port] = SM_RUN;
		/* fall through */
	case SM_RUN:
		/* If disabling, wait until message is sent. */
		if (!en && tch[port].state_id ==
					TCH_WAIT_FOR_MESSAGE_REQUEST_FROM_PE) {
			/* Disable RX */
#if defined(CONFIG_USB_TYPEC_CTVPD) || defined(CONFIG_USB_TYPEC_VPD)
			vpd_rx_enable(0);
#else
			tcpm_set_rx_enable(port, 0);
#endif
			local_state[port] = SM_PAUSED;
			break;
		}

		/* Run Protocol Layer Message Reception */
		prl_rx_wait_for_phy_message(port, evt);

		/* Run RX Chunked state machine */
		exe_state(port, RCH_OBJ(port), RUN_SIG);

		/* Run TX Chunked state machine */
		exe_state(port, TCH_OBJ(port), RUN_SIG);

		/* Run Protocol Layer Message Transmission state machine */
		exe_state(port, PRL_TX_OBJ(port), RUN_SIG);

		/* Run Protocol Layer Hard Reset state machine */
		exe_state(port, PRL_HR_OBJ(port), RUN_SIG);
		break;
	case SM_PAUSED:
		if (en)
			local_state[port] = SM_INIT;
		break;
	}
}

enum sm_local_state prl_get_local_state(int port)
{
	return local_state[port];
}

void prl_set_rev(int port, enum pd_rev_type rev)
{
	pdmsg[port].rev = rev;
}

enum pd_rev_type prl_get_rev(int port)
{
	return pdmsg[port].rev;
}

/* Common Protocol Layer Message Transmission */
static unsigned int prl_tx_phy_layer_reset(int port, enum signal sig)
{
	int ret;

	ret = (*prl_tx_phy_layer_reset_sig[sig])(port);
	return SUPER(ret, sig, 0);
}

static unsigned int prl_tx_phy_layer_reset_entry(int port)
{
	prl_tx[port].state_id = PRL_TX_PHY_LAYER_RESET;

#if defined(CONFIG_USB_TYPEC_CTVPD) || defined(CONFIG_USB_TYPEC_VPD)
	vpd_rx_enable(1);
#else
	tcpm_init(port);
	tcpm_set_rx_enable(port, 1);
#endif

	return 0;
}

static unsigned int prl_tx_phy_layer_reset_run(int port)
{
	set_state(port, PRL_TX_OBJ(port),
				prl_tx_wait_for_message_request);
	return 0;
}

static unsigned int prl_tx_wait_for_message_request(int port, enum signal sig)
{
	int ret;

	ret = (*prl_tx_wait_for_message_request_sig[sig])(port);
	return SUPER(ret, sig, 0);
}

static unsigned int prl_tx_wait_for_message_request_entry(int port)
{
	prl_tx[port].state_id = PRL_TX_WAIT_FOR_MESSAGE_REQUEST;

	/* Reset RetryCounter */
	prl_tx[port].retry_counter = 0;

	return 0;
}

static unsigned int prl_tx_wait_for_message_request_run(int port)
{
	if (prl_tx[port].flags & PRL_FLAGS_MSG_XMIT) {
		prl_tx[port].flags &= ~PRL_FLAGS_MSG_XMIT;
		/*
		 * Soft Reset Message Message pending
		 */
		if ((pdmsg[port].msg_type == PD_CTRL_SOFT_RESET) &&
							(emsg[port].len == 0)) {
			set_state(port, PRL_TX_OBJ(port),
					prl_tx_layer_reset_for_transmit);
		}
		/*
		 * Message pending (except Soft Reset)
		 */
		else {
			/* NOTE: PRL_TX_Construct_Message State embedded here */
			prl_tx_construct_message(port);
			set_state(port, PRL_TX_OBJ(port),
						prl_tx_wait_for_phy_response);
		}

		return 0;
	} else if ((pdmsg[port].rev == PD_REV30) &&
		(prl_tx[port].flags &
				(PRL_FLAGS_START_AMS | PRL_FLAGS_END_AMS))) {
		if (tc_get_power_role(port) == PD_ROLE_SOURCE) {
			/*
			 * Start of AMS notification received from
			 * Policy Engine
			 */
			if (prl_tx[port].flags & PRL_FLAGS_START_AMS) {
				prl_tx[port].flags &= ~PRL_FLAGS_START_AMS;
				set_state(port, PRL_TX_OBJ(port),
						prl_tx_src_source_tx);
				return 0;
			}
			/*
			 * End of AMS notification received from
			 * Policy Engine
			 */
			else if (prl_tx[port].flags & PRL_FLAGS_END_AMS) {
				prl_tx[port].flags &= ~PRL_FLAGS_END_AMS;
				/* Set Rp = SinkTxOk */
				tcpm_select_rp_value(port, SINK_TX_OK);
				tcpm_set_cc(port, TYPEC_CC_RP);
				prl_tx[port].retry_counter = 0;
				prl_tx[port].flags = 0;
			}
		} else {
			if (prl_tx[port].flags & PRL_FLAGS_START_AMS) {
				prl_tx[port].flags &= ~PRL_FLAGS_START_AMS;
				/*
				 * First Message in AMS notification
				 * received from Policy Engine.
				 */
				set_state(port, PRL_TX_OBJ(port),
							prl_tx_snk_start_ams);
				return 0;
			}
		}
	}

	return RUN_SUPER;
}

static void increment_msgid_counter(int port)
{
	prl_tx[port].msg_id_counter[prl_tx[port].sop] =
		(prl_tx[port].msg_id_counter[prl_tx[port].sop] + 1) &
		PD_MESSAGE_ID_COUNT;
}

/*
 * PrlTxDiscard
 */
static unsigned int prl_tx_discard_message(int port, enum signal sig)
{
	int ret;

	ret = (*prl_tx_discard_message_sig[sig])(port);
	return SUPER(ret, sig, 0);
}

static unsigned int prl_tx_discard_message_entry(int port)
{
	prl_tx[port].state_id = PRL_TX_DISCARD_MESSAGE;

	/* Increment msgidCounter */
	increment_msgid_counter(port);
	set_state(port, PRL_TX_OBJ(port), prl_tx_phy_layer_reset);

	return 0;
}

static unsigned int prl_tx_discard_message_run(int port)
{
	return RUN_SUPER;
}

/*
 * PrlTxSrcSourceTx
 */
static unsigned int prl_tx_src_source_tx(int port, enum signal sig)
{
	int ret;

	ret = (*prl_tx_src_source_tx_sig[sig])(port);
	return SUPER(ret, sig, 0);
}

static unsigned int prl_tx_src_source_tx_entry(int port)
{
	prl_tx[port].state_id = PRL_TX_SRC_SOURCE_TX;

	/* Set Rp = SinkTxNG */
	tcpm_select_rp_value(port, SINK_TX_NG);
	tcpm_set_cc(port, TYPEC_CC_RP);

	return 0;
}

static unsigned int prl_tx_src_source_tx_run(int port)
{
	if (prl_tx[port].flags & PRL_FLAGS_MSG_XMIT) {
		prl_tx[port].flags &= ~PRL_FLAGS_MSG_XMIT;

		set_state(port, PRL_TX_OBJ(port), prl_tx_src_pending);
	}

	return RUN_SUPER;
}

/*
 * PrlTxSnkStartAms
 */
static unsigned int prl_tx_snk_start_ams(int port, enum signal sig)
{
	int ret;

	ret = (*prl_tx_snk_start_ams_sig[sig])(port);
	return SUPER(ret, sig, 0);
}

static unsigned int prl_tx_snk_start_ams_entry(int port)
{
	prl_tx[port].state_id = PRL_TX_SNK_START_OF_AMS;
	return 0;
}

static unsigned int prl_tx_snk_start_ams_run(int port)
{
	if (prl_tx[port].flags & PRL_FLAGS_MSG_XMIT) {
		prl_tx[port].flags &= ~PRL_FLAGS_MSG_XMIT;

		set_state(port, PRL_TX_OBJ(port), prl_tx_snk_pending);
		return 0;
	}

	return RUN_SUPER;
}

/*
 * PrlTxLayerResetForTransmit
 */
static unsigned int prl_tx_layer_reset_for_transmit(int port, enum signal sig)
{
	int ret;

	ret = (*prl_tx_layer_reset_for_transmit_sig[sig])(port);
	return SUPER(ret, sig, 0);
}

static unsigned int prl_tx_layer_reset_for_transmit_entry(int port)
{
	int i;

	prl_tx[port].state_id = PRL_TX_LAYER_RESET_FOR_TRANSMIT;

	/* Reset MessageIdCounters */
	for (i = 0; i < NUM_XMIT_TYPES; i++)
		prl_tx[port].msg_id_counter[i] = 0;

	return 0;
}

static unsigned int prl_tx_layer_reset_for_transmit_run(int port)
{
	/* NOTE: PRL_Tx_Construct_Message State embedded here */
	prl_tx_construct_message(port);
	set_state(port, PRL_TX_OBJ(port), prl_tx_wait_for_phy_response);

	return 0;
}

static void prl_tx_construct_message(int port)
{
	uint32_t header = PD_HEADER(
			pdmsg[port].msg_type,
			tc_get_power_role(port),
			tc_get_data_role(port),
			prl_tx[port].msg_id_counter[pdmsg[port].xmit_type],
			pdmsg[port].data_objs,
			pdmsg[port].rev,
			pdmsg[port].ext);

	/* Save SOP* so the correct msg_id_counter can be incremented */
	prl_tx[port].sop = pdmsg[port].xmit_type;

	/* Pass message to PHY Layer */
	tcpm_transmit(port, pdmsg[port].xmit_type, header,
						pdmsg[port].chk_buf);
}

/*
 * PrlTxWaitForPhyResponse
 */
static unsigned int prl_tx_wait_for_phy_response(int port, enum signal sig)
{
	int ret;

	ret = (*prl_tx_wait_for_phy_response_sig[sig])(port);
	return SUPER(ret, sig, 0);
}

static unsigned int prl_tx_wait_for_phy_response_entry(int port)
{
	prl_tx[port].state_id = PRL_TX_WAIT_FOR_PHY_RESPONSE;

	prl_tx[port].tcpc_tx_timeout = get_time().val + PD_T_TCPC_TX_TIMEOUT;
	return 0;
}

static unsigned int prl_tx_wait_for_phy_response_run(int port)
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
			pdmsg[port].status_flags |= PRL_FLAGS_TX_ERROR;

			/* Increment message id counter */
			increment_msgid_counter(port);
			set_state(port, PRL_TX_OBJ(port),
					prl_tx_wait_for_message_request);
			return 0;
		}

		/* Try to resend the message. */
		/* NOTE: PRL_TX_Construct_Message State embedded here. */
		prl_tx_construct_message(port);
		return 0;
	}

	if (prl_tx[port].xmit_status == TCPC_TX_COMPLETE_SUCCESS) {

		/* NOTE: PRL_TX_Message_Sent State embedded here. */

		/* Increment messageId counter */
		increment_msgid_counter(port);
		/* Inform Policy Engine Message was sent */
		pdmsg[port].status_flags |= PRL_FLAGS_TX_COMPLETE;
		set_state(port, PRL_TX_OBJ(port),
			prl_tx_wait_for_message_request);
		return 0;
	}

	return RUN_SUPER;
}

static unsigned int prl_tx_wait_for_phy_response_exit(int port)
{
	prl_tx[port].xmit_status = TCPC_TX_UNSET;
	return 0;
}

/* Source Protocol Layer Message Transmission */
/*
 * PrlTxSrcPending
 */
static unsigned int prl_tx_src_pending(int port, enum signal sig)
{
	int ret;

	ret = (*prl_tx_src_pending_sig[sig])(port);
	return SUPER(ret, sig, 0);
}

static unsigned int prl_tx_src_pending_entry(int port)
{
	prl_tx[port].state_id = PRL_TX_SRC_PENDING;

	/* Start SinkTxTimer */
	prl_tx[port].sink_tx_timer = get_time().val + PD_T_SINK_TX;

	return 0;
}

static unsigned int prl_tx_src_pending_run(int port)
{

	if (get_time().val > prl_tx[port].sink_tx_timer) {
		/*
		 * Soft Reset Message pending &
		 * SinkTxTimer timeout
		 */
		if ((emsg[port].len == 0) &&
			(pdmsg[port].msg_type == PD_CTRL_SOFT_RESET)) {
			set_state(port, PRL_TX_OBJ(port),
					prl_tx_layer_reset_for_transmit);
		}
		/* Message pending (except Soft Reset) &
		 * SinkTxTimer timeout
		 */
		else {
			prl_tx_construct_message(port);
			set_state(port, PRL_TX_OBJ(port),
					prl_tx_wait_for_phy_response);
		}

		return 0;
	}

	return RUN_SUPER;
}

/*
 * PrlTxSnkPending
 */
static unsigned int prl_tx_snk_pending(int port, enum signal sig)
{
	int ret;

	ret = (*prl_tx_snk_pending_sig[sig])(port);
	return SUPER(ret, sig, 0);
}

static unsigned int prl_tx_snk_pending_entry(int port)
{
	prl_tx[port].state_id = PRL_TX_SNK_PENDING;
	return 0;
}

static unsigned int prl_tx_snk_pending_run(int port)
{
	int cc1;
	int cc2;

	tcpm_get_cc(port, &cc1, &cc2);
	if (cc1 == TYPEC_CC_VOLT_RP_3_0 || cc2 == TYPEC_CC_VOLT_RP_3_0) {
		/*
		 * Soft Reset Message Message pending &
		 * Rp = SinkTxOk
		 */
		if ((pdmsg[port].msg_type == PD_CTRL_SOFT_RESET) &&
					(emsg[port].len == 0)) {
			set_state(port, PRL_TX_OBJ(port),
				prl_tx_layer_reset_for_transmit);
		}
		/*
		 * Message pending (except Soft Reset) &
		 * Rp = SinkTxOk
		 */
		else {
			prl_tx_construct_message(port);
			set_state(port, PRL_TX_OBJ(port),
					prl_tx_wait_for_phy_response);
		}
		return 0;
	}

	return RUN_SUPER;
}

/* Hard Reset Operation */

static unsigned int prl_hr_wait_for_request(int port, enum signal sig)
{
	int ret;

	ret = (*prl_hr_wait_for_request_sig[sig])(port);
	return SUPER(ret, sig, 0);
}

static unsigned int prl_hr_wait_for_request_entry(int port)
{
	prl_hr[port].state_id = PRL_HR_WAIT_FOR_REQUEST;

	prl_hr[port].flags = 0;
	return 0;
}

static unsigned int prl_hr_wait_for_request_run(int port)
{
	if (prl_hr[port].flags & PRL_FLAGS_PE_HARD_RESET ||
		prl_hr[port].flags & PRL_FLAGS_PORT_PARTNER_HARD_RESET) {
		set_state(port, PRL_HR_OBJ(port), prl_hr_reset_layer);
	}

	return 0;
}

/*
 * PrlHrResetLayer
 */
static unsigned int prl_hr_reset_layer(int port, enum signal sig)
{
	int ret;

	ret = (*prl_hr_reset_layer_sig[sig])(port);
	return SUPER(ret, sig, 0);
}

static unsigned int prl_hr_reset_layer_entry(int port)
{
	int i;

	prl_hr[port].state_id = PRL_HR_RESET_LAYER;

	/* reset messageIDCounters */
	for (i = 0; i < NUM_XMIT_TYPES; i++)
		prl_tx[port].msg_id_counter[i] = 0;
	/*
	 * Protocol Layer message transmission transitions to
	 * PRL_Tx_Wait_For_Message_Request state.
	 */
	set_state(port, PRL_TX_OBJ(port),
		prl_tx_wait_for_message_request);

	return 0;
}

static unsigned int prl_hr_reset_layer_run(int port)
{
	/*
	 * Protocol Layer reset Complete &
	 * Hard Reset was initiated by Policy Engine
	 */
	if (prl_hr[port].flags & PRL_FLAGS_PE_HARD_RESET) {
		/* Request PHY to perform a Hard Reset */
		prl_send_ctrl_msg(port, TCPC_TX_HARD_RESET, 0);
		set_state(port, PRL_HR_OBJ(port),
			prl_hr_wait_for_phy_hard_reset_complete);
	}
	/*
	 * Protocol Layer reset complete &
	 * Hard Reset was initiated by Port Partner
	 */
	else {
		/* Inform Policy Engine of the Hard Reset */
		pe_got_hard_reset(port);
		set_state(port, PRL_HR_OBJ(port),
			prl_hr_wait_for_pe_hard_reset_complete);
	}

	return 0;
}

/*
 * PrlHrWaitForPhyHardResetComplete
 */
static unsigned int
	prl_hr_wait_for_phy_hard_reset_complete(int port, enum signal sig)
{
	int ret;

	ret = (*prl_hr_wait_for_phy_hard_reset_complete_sig[sig])(port);
	return SUPER(ret, sig, 0);
}

static unsigned int prl_hr_wait_for_phy_hard_reset_complete_entry(int port)
{
	prl_hr[port].state_id = PRL_HR_WAIT_FOR_PHY_HARD_RESET_COMPLETE;

	/* Start HardResetCompleteTimer */
	prl_hr[port].hard_reset_complete_timer =
			get_time().val + PD_T_PS_HARD_RESET;

	return 0;
}

static unsigned int prl_hr_wait_for_phy_hard_reset_complete_run(int port)
{
	/*
	 * Wait for hard reset from PHY
	 * or timeout
	 */
	if ((pdmsg[port].status_flags & PRL_FLAGS_TX_COMPLETE) ||
	    (get_time().val > prl_hr[port].hard_reset_complete_timer)) {
		/* PRL_HR_PHY_Hard_Reset_Requested */

		/* Inform Policy Engine Hard Reset was sent */
		pe_hard_reset_sent(port);
		set_state(port, PRL_HR_OBJ(port),
			prl_hr_wait_for_pe_hard_reset_complete);

		return 0;
	}

	return RUN_SUPER;
}

/*
 * PrlHrWaitForPeHardResetComplete
 */
static unsigned int
	prl_hr_wait_for_pe_hard_reset_complete(int port, enum signal sig)
{
	int ret;

	ret = (*prl_hr_wait_for_pe_hard_reset_complete_sig[sig])(port);
	return SUPER(ret, sig, 0);
}

static unsigned int prl_hr_wait_for_pe_hard_reset_complete_entry(int port)
{
	prl_hr[port].state_id = PRL_HR_WAIT_FOR_PE_HARD_RESET_COMPLETE;
	return 0;
}

static unsigned int prl_hr_wait_for_pe_hard_reset_complete_run(int port)
{
	/*
	 * Wait for Hard Reset complete indication from Policy Engine
	 */
	if (prl_hr[port].flags & PRL_FLAGS_HARD_RESET_COMPLETE)
		set_state(port, PRL_HR_OBJ(port), prl_hr_wait_for_request);

	return RUN_SUPER;
}

static unsigned int prl_hr_wait_for_pe_hard_reset_complete_exit(int port)
{
	/* Exit from Hard Reset */

	set_state(port, PRL_TX_OBJ(port), prl_tx_phy_layer_reset);
	set_state(port, RCH_OBJ(port),
				rch_wait_for_message_from_protocol_layer);
	set_state(port, TCH_OBJ(port), tch_wait_for_message_request_from_pe);

	return 0;
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
static unsigned int
	rch_wait_for_message_from_protocol_layer(int port, enum signal sig)
{
	int ret;

	ret = (*rch_wait_for_message_from_protocol_layer_sig[sig])(port);
	return SUPER(ret, sig, 0);
}

static inline void rch_clear_abort_set_chunking(int port)
{
	/* Clear Abort flag */
	pdmsg[port].status_flags &= ~PRL_FLAGS_ABORT;

	/* All Messages are chunked */
	rch[port].flags = PRL_FLAGS_CHUNKING;
}

static unsigned int rch_wait_for_message_from_protocol_layer_entry(int port)
{
	rch[port].state_id = RCH_WAIT_FOR_MESSAGE_FROM_PROTOCOL_LAYER;
	rch_clear_abort_set_chunking(port);
	return 0;
}

static unsigned int rch_wait_for_message_from_protocol_layer_run(int port)
{
	if (rch[port].flags & PRL_FLAGS_MSG_RECEIVED) {
		rch[port].flags &= ~PRL_FLAGS_MSG_RECEIVED;
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
			if ((rch[port].flags & PRL_FLAGS_CHUNKING) &&
					chunked) {
				set_state(port, RCH_OBJ(port),
				       rch_processing_extended_message);
				return 0;
			}
			/*
			 * (Received Extended Message &
			 * (Chunking = 0 & Chunked = 0))
			 */
			else if (!(rch[port].flags &
				      PRL_FLAGS_CHUNKING) && !chunked) {
				/* Copy chunk to extended buffer */
				copy_chunk_to_ext(port);
				/* Pass Message to Policy Engine */
				pe_pass_up_message(port);
				/* Clear Abort flag and set Chunking */
				rch_clear_abort_set_chunking(port);
			}
			/*
			 * Chunked != Chunking
			 */
			else {
				set_state(port, RCH_OBJ(port),
							rch_report_error);
				return 0;
			}
		}
		/*
		 * Received Non-Extended Message
		 */
		else if (!PD_HEADER_EXT(emsg[port].header)) {
			/* Copy chunk to extended buffer */
			copy_chunk_to_ext(port);
			/* Pass Message to Policy Engine */
			pe_pass_up_message(port);
			/* Clear Abort flag and set Chunking */
			rch_clear_abort_set_chunking(port);
		}
		/*
		 * Received an Extended Message while communicating at a
		 * revision lower than PD3.0
		 */
		else {
			set_state(port, RCH_OBJ(port),
						rch_report_error);
			return 0;
		}
	}

	return RUN_SUPER;
}

/*
 * RchProcessingExtendedMessage
 */
static unsigned int rch_processing_extended_message(int port, enum signal sig)
{
	int ret;

	ret = (*rch_processing_extended_message_sig[sig])(port);
	return SUPER(ret, sig, 0);
}

static unsigned int rch_processing_extended_message_entry(int port)
{
	uint32_t header = emsg[port].header;
	uint16_t exhdr = GET_EXT_HEADER(pdmsg[port].chk_buf[0]);
	uint8_t chunk_num = PD_EXT_HEADER_CHUNK_NUM(exhdr);

	rch[port].state_id = RCH_PROCESSING_EXTENDED_MESSAGE;

	/*
	 * If first chunk:
	 *   Set Chunk_number_expected = 0 and
	 *   Num_Bytes_Received = 0
	 */
	if (chunk_num == 0) {
		pdmsg[port].chunk_number_expected = 0;
		pdmsg[port].num_bytes_received = 0;
		pdmsg[port].msg_type = PD_HEADER_TYPE(header);
	}

	return 0;
}

static unsigned int rch_processing_extended_message_run(int port)
{
	uint16_t exhdr = GET_EXT_HEADER(pdmsg[port].chk_buf[0]);
	uint8_t chunk_num = PD_EXT_HEADER_CHUNK_NUM(exhdr);
	uint32_t data_size = PD_EXT_HEADER_DATA_SIZE(exhdr);
	uint32_t byte_num;

	/*
	 * Abort Flag Set
	 */
	if (pdmsg[port].status_flags & PRL_FLAGS_ABORT) {
		set_state(port, RCH_OBJ(port),
			rch_wait_for_message_from_protocol_layer);
	}
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
			set_state(port, RCH_OBJ(port), rch_report_error);
			return 0;
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
			pe_pass_up_message(port);
			set_state(port, RCH_OBJ(port),
			      rch_wait_for_message_from_protocol_layer);
		}
		/*
		 * Message not Complete
		 */
		else
			set_state(port, RCH_OBJ(port), rch_requesting_chunk);
	}
	/*
	 * Unexpected Chunk Number
	 */
	else
		set_state(port, RCH_OBJ(port), rch_report_error);

	return 0;
}

/*
 * RchRequestingChunk
 */
static unsigned int rch_requesting_chunk(int port, enum signal sig)
{
	int ret;

	ret = (*rch_requesting_chunk_sig[sig])(port);
	return SUPER(ret, sig, 0);
}

static unsigned int rch_requesting_chunk_entry(int port)
{
	rch[port].state_id = RCH_REQUESTING_CHUNK;

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
	prl_tx[port].flags |= PRL_FLAGS_MSG_XMIT;
	task_set_event(PD_PORT_TO_TASK_ID(port), PD_EVENT_TX, 0);

	return 0;
}

static unsigned int rch_requesting_chunk_run(int port)
{
	/*
	 * Transmission Error from Protocol Layer or
	 * Message Received From Protocol Layer
	 */
	if (rch[port].flags & PRL_FLAGS_MSG_RECEIVED ||
			pdmsg[port].status_flags & PRL_FLAGS_TX_ERROR) {
		/*
		 * Leave PRL_FLAGS_MSG_RECEIVED flag set. It'll be
		 * cleared in rch_report_error state
		 */
		set_state(port, RCH_OBJ(port), rch_report_error);
	}
	/*
	 * Message Transmitted received from Protocol Layer
	 */
	else if (pdmsg[port].status_flags & PRL_FLAGS_TX_COMPLETE) {
		pdmsg[port].status_flags &= ~PRL_FLAGS_TX_COMPLETE;
		set_state(port, RCH_OBJ(port), rch_waiting_chunk);
	} else
		return RUN_SUPER;

	return 0;
}

/*
 * RchWaitingChunk
 */
static unsigned int rch_waiting_chunk(int port, enum signal sig)
{
	int ret;

	ret = (*rch_waiting_chunk_sig[sig])(port);
	return SUPER(ret, sig, 0);
}

static unsigned int rch_waiting_chunk_entry(int port)
{
	rch[port].state_id = RCH_WAITING_CHUNK;

	/*
	 * Start ChunkSenderResponseTimer
	 */
	rch[port].chunk_sender_response_timer =
		get_time().val + PD_T_CHUNK_SENDER_RESPONSE;

	return 0;
}

static unsigned int rch_waiting_chunk_run(int port)
{
	if ((rch[port].flags & PRL_FLAGS_MSG_RECEIVED)) {
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
				set_state(port, RCH_OBJ(port),
							rch_report_error);
			}
			/*
			 * Chunk response Received from Protocol Layer
			 */
			else {
				/*
				 * No error wad detected, so clear
				 * PRL_FLAGS_MSG_RECEIVED flag.
				 */
				rch[port].flags &= ~PRL_FLAGS_MSG_RECEIVED;
				set_state(port, RCH_OBJ(port),
					rch_processing_extended_message);
			}

			return 0;
		}
	}
	/*
	 * ChunkSenderResponseTimer Timeout
	 */
	else if (get_time().val > rch[port].chunk_sender_response_timer) {
		set_state(port, RCH_OBJ(port), rch_report_error);
		return 0;
	}

	return RUN_SUPER;
}

/*
 * RchReportError
 */
static unsigned int rch_report_error(int port, enum signal sig)
{
	int ret;

	ret = (*rch_report_error_sig[sig])(port);
	return SUPER(ret, sig, 0);
}

static unsigned int rch_report_error_entry(int port)
{
	rch[port].state_id = RCH_REPORT_ERROR;

	/*
	 * If the state was entered because a message was received,
	 * this message is passed to the Policy Engine.
	 */
	if (rch[port].flags & PRL_FLAGS_MSG_RECEIVED) {
		rch[port].flags &= ~PRL_FLAGS_MSG_RECEIVED;

		/* Copy chunk to extended buffer */
		copy_chunk_to_ext(port);
		/* Pass Message to Policy Engine */
		pe_pass_up_message(port);
		/* Report error */
		pe_report_error(port, ERR_RCH_MSG_REC);
	} else {
		/* Report error */
		pe_report_error(port, ERR_RCH_CHUNKED);
	}

	return 0;
}

static unsigned int rch_report_error_run(int port)
{
	set_state(port, RCH_OBJ(port),
		rch_wait_for_message_from_protocol_layer);

	return 0;
}

/*
 * Chunked Tx State Machine
 */
static unsigned int
	tch_wait_for_message_request_from_pe(int port, enum signal sig)
{
	int ret;

	ret = (*tch_wait_for_message_request_from_pe_sig[sig])(port);
	return SUPER(ret, sig, 0);
}

static inline void tch_clear_abort_set_chunking(int port)
{
	/* Clear Abort flag */
	pdmsg[port].status_flags &= ~PRL_FLAGS_ABORT;

	/* All Messages are chunked */
	tch[port].flags = PRL_FLAGS_CHUNKING;
}

static unsigned int tch_wait_for_message_request_from_pe_entry(int port)
{
	tch[port].state_id = TCH_WAIT_FOR_MESSAGE_REQUEST_FROM_PE;
	tch_clear_abort_set_chunking(port);
	return 0;
}

static unsigned int tch_wait_for_message_request_from_pe_run(int port)
{
	/*
	 * Any message received and not in state TCH_Wait_Chunk_Request
	 */
	if (tch[port].flags & PRL_FLAGS_MSG_RECEIVED) {
		tch[port].flags &= ~PRL_FLAGS_MSG_RECEIVED;
		set_state(port, TCH_OBJ(port), tch_message_received);
		return 0;
	} else if (tch[port].flags & PRL_FLAGS_MSG_XMIT) {
		tch[port].flags &= ~PRL_FLAGS_MSG_XMIT;
		/*
		 * Rx Chunking State != RCH_Wait_For_Message_From_Protocol_Layer
		 * & Abort Supported
		 *
		 * Discard the Message
		 */
		if (rch[port].state_id !=
				RCH_WAIT_FOR_MESSAGE_FROM_PROTOCOL_LAYER) {
			/* Report Error To Policy Engine */
			pe_report_error(port, ERR_TCH_XMIT);
			tch_clear_abort_set_chunking(port);
		} else {
			/*
			 * Extended Message Request & Chunking
			 */
			if ((pdmsg[port].rev == PD_REV30) && pdmsg[port].ext &&
			     (tch[port].flags & PRL_FLAGS_CHUNKING)) {
				pdmsg[port].send_offset = 0;
				pdmsg[port].chunk_number_to_send = 0;
				set_state(port, TCH_OBJ(port),
					tch_construct_chunked_message);
			} else
			/*
			 * Non-Extended Message Request
			 */
			{
				/* Make sure buffer doesn't overflow */
				if (emsg[port].len > BUFFER_SIZE) {
					/* Report Error To Policy Engine */
					pe_report_error(port, ERR_TCH_XMIT);
					tch_clear_abort_set_chunking(port);
					return 0;
				}

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
				prl_tx[port].flags |= PRL_FLAGS_MSG_XMIT;
				set_state(port, TCH_OBJ(port),
					tch_wait_for_transmission_complete);
			}

			return 0;
		}
	}

	return RUN_SUPER;
}

/*
 * TchWaitForTransmissionComplete
 */
static unsigned int
	tch_wait_for_transmission_complete(int port, enum signal sig)
{
	int ret;

	ret = (*tch_wait_for_transmission_complete_sig[sig])(port);
	return SUPER(ret, sig, 0);
}

static unsigned int tch_wait_for_transmission_complete_entry(int port)
{
	tch[port].state_id = TCH_WAIT_FOR_TRANSMISSION_COMPLETE;
	return 0;
}

static unsigned int tch_wait_for_transmission_complete_run(int port)
{
	/*
	 * Any message received and not in state TCH_Wait_Chunk_Request
	 */
	if (tch[port].flags & PRL_FLAGS_MSG_RECEIVED) {
		tch[port].flags &= ~PRL_FLAGS_MSG_RECEIVED;
		set_state(port, TCH_OBJ(port), tch_message_received);
		return 0;
	}

	/*
	 * Inform Policy Engine that Message was sent.
	 */
	if (pdmsg[port].status_flags & PRL_FLAGS_TX_COMPLETE) {
		pdmsg[port].status_flags &= ~PRL_FLAGS_TX_COMPLETE;
		set_state(port, TCH_OBJ(port),
			tch_wait_for_message_request_from_pe);

		/* Tell PE message was sent */
		pe_message_sent(port);
	}
	/*
	 * Inform Policy Engine of Tx Error
	 */
	else if (pdmsg[port].status_flags & PRL_FLAGS_TX_ERROR) {
		pdmsg[port].status_flags &= ~PRL_FLAGS_TX_ERROR;
		/* Tell PE an error occurred */
		pe_report_error(port, ERR_TCH_XMIT);
		set_state(port, TCH_OBJ(port),
			tch_wait_for_message_request_from_pe);
	}

	return 0;
}

/*
 * TchConstructChunkedMessage
 */
static unsigned int tch_construct_chunked_message(int port, enum signal sig)
{
	int ret;

	ret = (*tch_construct_chunked_message_sig[sig])(port);
	return SUPER(ret, sig, 0);
}

static unsigned int tch_construct_chunked_message_entry(int port)
{
	uint16_t *ext_hdr;
	uint8_t *data;
	uint16_t num;

	tch[port].state_id = TCH_CONSTRUCT_CHUNKED_MESSAGE;

	/*
	 * Any message received and not in state TCH_Wait_Chunk_Request
	 */
	if (tch[port].flags & PRL_FLAGS_MSG_RECEIVED) {
		tch[port].flags &= ~PRL_FLAGS_MSG_RECEIVED;
		set_state(port, TCH_OBJ(port), tch_message_received);
		return 0;
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
	prl_tx[port].flags |= PRL_FLAGS_MSG_XMIT;
	task_set_event(PD_PORT_TO_TASK_ID(port), PD_EVENT_SM, 0);

	return 0;
}

static unsigned int tch_construct_chunked_message_run(int port)
{
	if (pdmsg[port].status_flags & PRL_FLAGS_ABORT)
		set_state(port, TCH_OBJ(port),
			tch_wait_for_message_request_from_pe);
	else
		set_state(port, TCH_OBJ(port),
				tch_sending_chunked_message);
	return 0;
}

/*
 * TchSendingChunkedMessage
 */
static unsigned int tch_sending_chunked_message(int port, enum signal sig)
{
	int ret;

	ret = (*tch_sending_chunked_message_sig[sig])(port);
	return SUPER(ret, sig, 0);
}

static unsigned int tch_sending_chunked_message_entry(int port)
{
	tch[port].state_id = TCH_SENDING_CHUNKED_MESSAGE;
	return 0;
}

static unsigned int tch_sending_chunked_message_run(int port)
{
	/*
	 * Any message received and not in state TCH_Wait_Chunk_Request
	 */
	if (tch[port].flags & PRL_FLAGS_MSG_RECEIVED) {
		tch[port].flags &= ~PRL_FLAGS_MSG_RECEIVED;
		set_state(port, TCH_OBJ(port), tch_message_received);
		return 0;
	}

	/*
	 * Transmission Error
	 */
	if (pdmsg[port].status_flags & PRL_FLAGS_TX_ERROR) {
		pe_report_error(port, ERR_TCH_XMIT);
		set_state(port, TCH_OBJ(port),
			tch_wait_for_message_request_from_pe);
	}
	/*
	 * Message Transmitted from Protocol Layer &
	 * Last Chunk
	 */
	else if (emsg[port].len == pdmsg[port].send_offset) {
		set_state(port, TCH_OBJ(port),
			tch_wait_for_message_request_from_pe);

		/* Tell PE message was sent */
		pe_message_sent(port);
	}
	/*
	 * Message Transmitted from Protocol Layer &
	 * Not Last Chunk
	 */
	else
		set_state(port, TCH_OBJ(port), tch_wait_chunk_request);

	return 0;
}

/*
 * TchWaitChunkRequest
 */
static unsigned int tch_wait_chunk_request(int port, enum signal sig)
{
	int ret;

	ret = (*tch_wait_chunk_request_sig[sig])(port);
	return SUPER(ret, sig, 0);
}

static unsigned int tch_wait_chunk_request_entry(int port)
{
	tch[port].state_id = TCH_WAIT_CHUNK_REQUEST;

	/* Increment Chunk Number to Send */
	pdmsg[port].chunk_number_to_send++;
	/* Start Chunk Sender Request Timer */
	tch[port].chunk_sender_request_timer =
		get_time().val + PD_T_CHUNK_SENDER_REQUEST;
	return 0;
}

static unsigned int tch_wait_chunk_request_run(int port)
{
	if (tch[port].flags & PRL_FLAGS_MSG_RECEIVED) {
		tch[port].flags &= ~PRL_FLAGS_MSG_RECEIVED;

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
					set_state(port, TCH_OBJ(port),
					tch_construct_chunked_message);
				}
				/*
				 * Chunk Request Received &
				 * Chunk Number != Chunk Number to Send
				 */
				else {
					pe_report_error(port, ERR_TCH_CHUNKED);
					set_state(port, TCH_OBJ(port),
					tch_wait_for_message_request_from_pe);
				}
				return 0;
			}
		}

		/*
		 * Other message received
		 */
		set_state(port, TCH_OBJ(port), tch_message_received);
	}
	/*
	 * ChunkSenderRequestTimer timeout
	 */
	else if (get_time().val >=
			tch[port].chunk_sender_request_timer) {
		set_state(port, TCH_OBJ(port),
			tch_wait_for_message_request_from_pe);

		/* Tell PE message was sent */
		pe_message_sent(port);
	}

	return 0;
}

/*
 * TchMessageReceived
 */
static unsigned int tch_message_received(int port, enum signal sig)
{
	int ret;

	ret = (*tch_message_received_sig[sig])(port);
	return SUPER(ret, sig, 0);
}

static unsigned int tch_message_received_entry(int port)
{
	tch[port].state_id = TCH_MESSAGE_RECEIVED;

	/* Pass message to chunked Rx */
	rch[port].flags |= PRL_FLAGS_MSG_RECEIVED;
	task_set_event(PD_PORT_TO_TASK_ID(port), PD_EVENT_SM, 0);

	return 0;
}

static unsigned int tch_message_received_run(int port)
{
	set_state(port, TCH_OBJ(port),
		tch_wait_for_message_request_from_pe);

	return 0;
}

/*
 * Protocol Layer Message Reception State Machine
 */
static unsigned int prl_rx_wait_for_phy_message(int port, int evt)
{
	uint32_t header;
	uint8_t type;
	uint8_t cnt;
	uint8_t sop;
	int8_t msid;
	int ret;

	/* process any potential incoming message */
	if (tcpm_has_pending_message(port)) {
		ret = tcpm_dequeue_message(port, pdmsg[port].chk_buf, &header);
		if (ret == 0) {
			emsg[port].header = header;
			type = PD_HEADER_TYPE(header);
			cnt = PD_HEADER_CNT(header);
			msid = PD_HEADER_ID(header);
			sop = PD_HEADER_GET_SOP(header);

			if (cnt == 0 && type == PD_CTRL_SOFT_RESET) {
				int i;

				for (i = 0; i < NUM_XMIT_TYPES; i++) {
					/* Clear MessageIdCounter */
					prl_tx[port].msg_id_counter[i] = 0;
					/* Clear stored MessageID value */
					prl_rx[port].msg_id[i] = -1;
				}

				/* Inform Policy Engine of Soft Reset */
				pe_got_soft_reset(port);

				/* Soft Reset occurred */
				set_state(port, PRL_TX_OBJ(port),
						prl_tx_phy_layer_reset);
				set_state(port, RCH_OBJ(port),
				rch_wait_for_message_from_protocol_layer);
				set_state(port, TCH_OBJ(port),
				tch_wait_for_message_request_from_pe);
			}

			/*
			 * Ignore if this is a duplicate message.
			 */
			if (prl_rx[port].msg_id[sop] != msid) {
				/*
				 * Discard any pending tx message if this is
				 * not a ping message
				 */
				if ((pdmsg[port].rev == PD_REV30) &&
					(cnt == 0) && type != PD_CTRL_PING) {
					if (prl_tx[port].state_id ==
						PRL_TX_SRC_PENDING ||
						prl_tx[port].state_id ==
						PRL_TX_SNK_PENDING) {
						set_state(port,
							PRL_TX_OBJ(port),
							prl_tx_discard_message);
					}
				}

				/* Store Message Id */
				prl_rx[port].msg_id[sop] = msid;

				/* RTR Chunked Message Router States. */
				/*
				 * Received Ping from Protocol Layer
				 */
				if (cnt == 0 && type == PD_CTRL_PING) {
					/* NOTE: RTR_PING State embedded
					 * here.
					 */
					emsg[port].len = 0;
					pe_pass_up_message(port);
					return 0;
				}
				/*
				 * Message (not Ping) Received from
				 * Protocol Layer & Doing Tx Chunks
				 */
				else if (tch[port].state_id !=
					TCH_WAIT_FOR_MESSAGE_REQUEST_FROM_PE) {
					/* NOTE: RTR_TX_CHUNKS State embedded
					 * here.
					 */
					/*
					 * Send Message to Tx Chunk
					 * Chunk State Machine
					 */
					tch[port].flags |=
							PRL_FLAGS_MSG_RECEIVED;
				}
				/*
				 * Message (not Ping) Received from
				 * Protocol Layer & Not Doing Tx Chunks
				 */
				else {
					/*
					 * NOTE: RTR_RX_CHUNKS State embedded
					 * here.
					 */
					/*
					 * Send Message to Rx
					 * Chunk State Machine
					 */
					rch[port].flags |=
							PRL_FLAGS_MSG_RECEIVED;
				}

				task_set_event(PD_PORT_TO_TASK_ID(port),
								PD_EVENT_SM, 0);
			}
		}
	}

	return 0;
}

static unsigned int do_nothing_exit(int port)
{
	return 0;
}

static unsigned int get_super_state(int port)
{
	return RUN_SUPER;
}
