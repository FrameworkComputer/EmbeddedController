/* Copyright 2019 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "battery.h"
#include "battery_smart.h"
#include "builtin/assert.h"
#include "charge_manager.h"
#include "charge_state.h"
#include "chipset.h"
#include "common.h"
#include "compile_time_macros.h"
#include "console.h"
#include "cros_version.h"
#include "ec_commands.h"
#include "gpio.h"
#include "hooks.h"
#include "host_command.h"
#include "registers.h"
#include "system.h"
#include "task.h"
#include "tcpm/tcpm.h"
#include "usb_charge.h"
#include "usb_emsg.h"
#include "usb_mux.h"
#include "usb_pd.h"
#include "usb_pd_timer.h"
#include "usb_pe_sm.h"
#include "usb_prl_sm.h"
#include "usb_sm.h"
#include "usb_tc_sm.h"
#include "util.h"
#include "vpd_api.h"

#ifdef CONFIG_COMMON_RUNTIME
#define CPRINTF(format, args...) cprintf(CC_USBPD, format, ##args)
#define CPRINTS(format, args...) cprints(CC_USBPD, format, ##args)
#else
#define CPRINTF(format, args...)
#define CPRINTS(format, args...)
#endif

/*
 * Define DEBUG_PRINT_FLAG_NAMES to print flag names when set and cleared.
 */
#undef DEBUG_PRINT_FLAG_NAMES

#ifdef DEBUG_PRINT_FLAG_NAMES
__maybe_unused static void print_flag(const char *group, int set_or_clear,
				      int flag);
#define SET_FLAG(group, flags, flag)        \
	do {                                \
		print_flag(group, 1, flag); \
		atomic_or(flags, (flag));   \
	} while (0)
#define CLR_FLAG(group, flags, flag)                \
	do {                                        \
		int before = *flags;                \
		atomic_clear_bits(flags, (flag));   \
		if (*flags != before)               \
			print_flag(group, 0, flag); \
	} while (0)
#else
#define SET_FLAG(group, flags, flag) atomic_or(flags, (flag))
#define CLR_FLAG(group, flags, flag) atomic_clear_bits(flags, (flag))
#endif

#define RCH_SET_FLAG(port, flag) SET_FLAG("RCH", &rch[port].flags, (flag))
#define RCH_CLR_FLAG(port, flag) CLR_FLAG("RCH", &rch[port].flags, (flag))
#define RCH_CHK_FLAG(port, flag) (rch[port].flags & (flag))

#define TCH_SET_FLAG(port, flag) SET_FLAG("TCH", &tch[port].flags, (flag))
#define TCH_CLR_FLAG(port, flag) CLR_FLAG("TCH", &tch[port].flags, (flag))
#define TCH_CHK_FLAG(port, flag) (tch[port].flags & (flag))

#define PRL_TX_SET_FLAG(port, flag) \
	SET_FLAG("PRL_TX", &prl_tx[port].flags, (flag))
#define PRL_TX_CLR_FLAG(port, flag) \
	CLR_FLAG("PRL_TX", &prl_tx[port].flags, (flag))
#define PRL_TX_CHK_FLAG(port, flag) (prl_tx[port].flags & (flag))

#define PRL_HR_SET_FLAG(port, flag) \
	SET_FLAG("PRL_HR", &prl_hr[port].flags, (flag))
#define PRL_HR_CLR_FLAG(port, flag) \
	CLR_FLAG("PRL_HR", &prl_hr[port].flags, (flag))
#define PRL_HR_CHK_FLAG(port, flag) (prl_hr[port].flags & (flag))

#define PDMSG_SET_FLAG(port, flag) SET_FLAG("PDMSG", &pdmsg[port].flags, (flag))
#define PDMSG_CLR_FLAG(port, flag) CLR_FLAG("PDMSG", &pdmsg[port].flags, (flag))
#define PDMSG_CHK_FLAG(port, flag) (pdmsg[port].flags & (flag))

/* Protocol Layer Flags */
/*
 * NOTE:
 *	These flags are used in multiple state machines and could have
 *	different meanings in each state machine.
 */
/* Flag to note message transmission completed */
#define PRL_FLAGS_TX_COMPLETE BIT(0)
/* Flag to note that PRL requested to set SINK_NG CC state */
#define PRL_FLAGS_SINK_NG BIT(1)
/* Flag to note PRL waited for SINK_OK CC state before transmitting */
#define PRL_FLAGS_WAIT_SINK_OK BIT(2)
/* Flag to note transmission error occurred */
#define PRL_FLAGS_TX_ERROR BIT(3)
/* Flag to note PE triggered a hard reset */
#define PRL_FLAGS_PE_HARD_RESET BIT(4)
/* Flag to note hard reset has completed */
#define PRL_FLAGS_HARD_RESET_COMPLETE BIT(5)
/* Flag to note port partner sent a hard reset */
#define PRL_FLAGS_PORT_PARTNER_HARD_RESET BIT(6)
/*
 * Flag to note a message transmission has been requested. It is only cleared
 * when we send the message to the TCPC layer.
 */
#define PRL_FLAGS_MSG_XMIT BIT(7)
/* Flag to note a message was received */
#define PRL_FLAGS_MSG_RECEIVED BIT(8)
/* Flag to note aborting current TX message, not currently set */
#define PRL_FLAGS_ABORT BIT(9)
/* Flag to note current TX message uses chunking */
#define PRL_FLAGS_CHUNKING BIT(10)
/* Flag to disable checking data role on incoming messages. */
#define PRL_FLAGS_IGNORE_DATA_ROLE BIT(11)

/* For checking flag_bit_names[] */
#define PRL_FLAGS_COUNT 12

struct bit_name {
	int value;
	const char *name;
};

static __const_data const struct bit_name flag_bit_names[] = {
	{ PRL_FLAGS_TX_COMPLETE, "PRL_FLAGS_TX_COMPLETE" },
	{ PRL_FLAGS_SINK_NG, "PRL_FLAGS_SINK_NG" },
	{ PRL_FLAGS_WAIT_SINK_OK, "PRL_FLAGS_WAIT_SINK_OK" },
	{ PRL_FLAGS_TX_ERROR, "PRL_FLAGS_TX_ERROR" },
	{ PRL_FLAGS_PE_HARD_RESET, "PRL_FLAGS_PE_HARD_RESET" },
	{ PRL_FLAGS_HARD_RESET_COMPLETE, "PRL_FLAGS_HARD_RESET_COMPLETE" },
	{ PRL_FLAGS_PORT_PARTNER_HARD_RESET,
	  "PRL_FLAGS_PORT_PARTNER_HARD_RESET" },
	{ PRL_FLAGS_MSG_XMIT, "PRL_FLAGS_MSG_XMIT" },
	{ PRL_FLAGS_MSG_RECEIVED, "PRL_FLAGS_MSG_RECEIVED" },
	{ PRL_FLAGS_ABORT, "PRL_FLAGS_ABORT" },
	{ PRL_FLAGS_CHUNKING, "PRL_FLAGS_CHUNKING" },
	{ PRL_FLAGS_IGNORE_DATA_ROLE, "PRL_FLAGS_IGNORE_DATA_ROLE" },
};
BUILD_ASSERT(ARRAY_SIZE(flag_bit_names) == PRL_FLAGS_COUNT);

__maybe_unused static void print_bits(const char *group, const char *desc,
				      int value, const struct bit_name *names,
				      int names_size)
{
	int i;

	CPRINTF("%s %s 0x%x : ", group, desc, value);
	for (i = 0; i < names_size; i++) {
		if (value & names[i].value)
			CPRINTF("%s | ", names[i].name);
		value &= ~names[i].value;
	}
	if (value != 0)
		CPRINTF("0x%x", value);
	CPRINTF("\n");
}

__maybe_unused static void print_flag(const char *group, int set_or_clear,
				      int flag)
{
	print_bits(group, set_or_clear ? "Set" : "Clr", flag, flag_bit_names,
		   ARRAY_SIZE(flag_bit_names));
}

/* PD counter definitions */
#define PD_MESSAGE_ID_COUNT 7

/* Size of PDMSG Chunk Buffer */
#define CHK_BUF_SIZE 7
#define CHK_BUF_SIZE_BYTES 28

/*
 * Debug log level - higher number == more log
 *   Level 0: disabled
 *   Level 1: not currently used
 *   Level 2: plus non-ping messages
 *   Level 3: plus ping packet and PRL states
 *
 * Note that higher log level causes timing changes and thus may affect
 * performance.
 */
#ifdef CONFIG_USB_PD_DEBUG_LEVEL
static const enum debug_level prl_debug_level = CONFIG_USB_PD_DEBUG_LEVEL;
#elif defined(CONFIG_USB_PD_INITIAL_DEBUG_LEVEL)
static enum debug_level prl_debug_level = CONFIG_USB_PD_INITIAL_DEBUG_LEVEL;
#else
static enum debug_level prl_debug_level = DEBUG_LEVEL_1;
#endif

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

static const char *const prl_tx_state_names[] = {
	[PRL_TX_PHY_LAYER_RESET] = "PRL_TX_PHY_LAYER_RESET",
	[PRL_TX_WAIT_FOR_MESSAGE_REQUEST] = "PRL_TX_WAIT_FOR_MESSAGE_REQUEST",
	[PRL_TX_LAYER_RESET_FOR_TRANSMIT] = "PRL_TX_LAYER_RESET_FOR_TRANSMIT",
	[PRL_TX_WAIT_FOR_PHY_RESPONSE] = "PRL_TX_WAIT_FOR_PHY_RESPONSE",
	[PRL_TX_SRC_SOURCE_TX] = "PRL_TX_SRC_SOURCE_TX",
	[PRL_TX_SNK_START_AMS] = "PRL_TX_SNK_START_AMS",
	[PRL_TX_SRC_PENDING] = "PRL_TX_SRC_PENDING",
	[PRL_TX_SNK_PENDING] = "PRL_TX_SNK_PENDING",
	[PRL_TX_DISCARD_MESSAGE] = "PRL_TX_DISCARD_MESSAGE",
};

static const char *const prl_hr_state_names[] = {
	[PRL_HR_WAIT_FOR_REQUEST] = "PRL_HR_WAIT_FOR_REQUEST",
	[PRL_HR_RESET_LAYER] = "PRL_HR_RESET_LAYER",
	[PRL_HR_WAIT_FOR_PHY_HARD_RESET_COMPLETE] =
		"PRL_HR_WAIT_FOR_PHY_HARD_RESET_COMPLETE",
	[PRL_HR_WAIT_FOR_PE_HARD_RESET_COMPLETE] =
		"PRL_HR_WAIT_FOR_PE_HARD_RESET_COMPLETE",
};

__maybe_unused static const char *const rch_state_names[] = {
	[RCH_WAIT_FOR_MESSAGE_FROM_PROTOCOL_LAYER] =
		"RCH_WAIT_FOR_MESSAGE_FROM_PROTOCOL_LAYER",
	[RCH_PASS_UP_MESSAGE] = "RCH_PASS_UP_MESSAGE",
	[RCH_PROCESSING_EXTENDED_MESSAGE] = "RCH_PROCESSING_EXTENDED_MESSAGE",
	[RCH_REQUESTING_CHUNK] = "RCH_REQUESTING_CHUNK",
	[RCH_WAITING_CHUNK] = "RCH_WAITING_CHUNK",
	[RCH_REPORT_ERROR] = "RCH_REPORT_ERROR",
};

__maybe_unused static const char *const tch_state_names[] = {
	[TCH_WAIT_FOR_MESSAGE_REQUEST_FROM_PE] =
		"TCH_WAIT_FOR_MESSAGE_REQUEST_FROM_PE",
	[TCH_WAIT_FOR_TRANSMISSION_COMPLETE] =
		"TCH_WAIT_FOR_TRANSMISSION_COMPLETE",
	[TCH_CONSTRUCT_CHUNKED_MESSAGE] = "TCH_CONSTRUCT_CHUNKED_MESSAGE",
	[TCH_SENDING_CHUNKED_MESSAGE] = "TCH_SENDING_CHUNKED_MESSAGE",
	[TCH_WAIT_CHUNK_REQUEST] = "TCH_WAIT_CHUNK_REQUEST",
	[TCH_MESSAGE_RECEIVED] = "TCH_MESSAGE_RECEIVED",
	[TCH_MESSAGE_SENT] = "TCH_MESSAGE_SENT",
	[TCH_REPORT_ERROR] = "TCH_REPORT_ERROR",
};

/* Forward declare full list of states. Index by above enums. */
static const struct usb_state prl_tx_states[];
static const struct usb_state prl_hr_states[];

__maybe_unused static const struct usb_state rch_states[];
__maybe_unused static const struct usb_state tch_states[];

/* Chunked Rx State Machine Object */
static struct rx_chunked {
	/* state machine context */
	struct sm_ctx ctx;
	/* PRL_FLAGS */
	atomic_t flags;
	/* error to report when moving to rch_report_error state */
	enum pe_error error;
} rch[CONFIG_USB_PD_PORT_MAX_COUNT];

/* Chunked Tx State Machine Object */
static struct tx_chunked {
	/* state machine context */
	struct sm_ctx ctx;
	/* state machine flags */
	atomic_t flags;
	/* error to report when moving to tch_report_error state */
	enum pe_error error;
} tch[CONFIG_USB_PD_PORT_MAX_COUNT];

/* Message Reception State Machine Object */
static struct protocol_layer_rx {
	/* received message type */
	enum tcpci_msg_type sop;
	/* message ids for all valid port partners */
	int msg_id[NUM_SOP_STAR_TYPES];
} prl_rx[CONFIG_USB_PD_PORT_MAX_COUNT];

/* Message Transmission State Machine Object */
static struct protocol_layer_tx {
	/* state machine context */
	struct sm_ctx ctx;
	/* state machine flags */
	atomic_t flags;
	/* last message type we transmitted */
	enum tcpci_msg_type last_xmit_type;
	/* message id counters for all 6 port partners */
	uint32_t msg_id_counter[NUM_SOP_STAR_TYPES];
	/* transmit status */
	int xmit_status;
} prl_tx[CONFIG_USB_PD_PORT_MAX_COUNT];

/* Hard Reset State Machine Object */
static struct protocol_hard_reset {
	/* state machine context */
	struct sm_ctx ctx;
	/* state machine flags */
	atomic_t flags;
} prl_hr[CONFIG_USB_PD_PORT_MAX_COUNT];

/* Chunking Message Object */
static struct pd_message {
	/* message status flags */
	atomic_t flags;
	/* SOP* */
	enum tcpci_msg_type xmit_type;
	/* type of message */
	uint8_t msg_type;
	/* PD revision */
	enum pd_rev_type rev[NUM_SOP_STAR_TYPES];
	/* Number of 32-bit objects in chk_buf */
	uint16_t data_objs;
	/* temp chunk buffer */
	uint32_t tx_chk_buf[CHK_BUF_SIZE];
	uint32_t rx_chk_buf[CHK_BUF_SIZE];
	uint32_t chunk_number_expected;
	uint32_t num_bytes_received;
#ifdef CONFIG_USB_PD_EXTENDED_MESSAGES
	/* extended message */
	uint8_t ext;
	uint32_t chunk_number_to_send;
	uint32_t send_offset;
#endif /* CONFIG_USB_PD_EXTENDED_MESSAGES */
} pdmsg[CONFIG_USB_PD_PORT_MAX_COUNT];

struct extended_msg rx_emsg[CONFIG_USB_PD_PORT_MAX_COUNT];
struct extended_msg tx_emsg[CONFIG_USB_PD_PORT_MAX_COUNT];

enum prl_event_log_state_kind {
	/* Identifies uninitialized entries */
	PRL_EVENT_LOG_STATE_NONE,
	PRL_EVENT_LOG_STATE_TX,
	PRL_EVENT_LOG_STATE_HR,
	PRL_EVENT_LOG_STATE_RCH,
	PRL_EVENT_LOG_STATE_TCH,
};

__maybe_unused static void
prl_event_log_append(enum prl_event_log_state_kind kind, int port);

/* Common Protocol Layer Message Transmission */
static void prl_tx_construct_message(int port);
static void prl_rx_wait_for_phy_message(const int port, int evt);
static void prl_copy_msg_to_buffer(int port);

#ifndef CONFIG_USB_PD_REV30
GEN_NOT_SUPPORTED(PRL_TX_SRC_SOURCE_TX);
#define PRL_TX_SRC_SOURCE_TX PRL_TX_SRC_SOURCE_TX_NOT_SUPPORTED
GEN_NOT_SUPPORTED(PRL_TX_SNK_START_AMS);
#define PRL_TX_SNK_START_AMS PRL_TX_SNK_START_AMS_NOT_SUPPORTED

GEN_NOT_SUPPORTED(RCH_WAIT_FOR_MESSAGE_FROM_PROTOCOL_LAYER);
#define RCH_WAIT_FOR_MESSAGE_FROM_PROTOCOL_LAYER \
	RCH_WAIT_FOR_MESSAGE_FROM_PROTOCOL_LAYER_NOT_SUPPORTED
GEN_NOT_SUPPORTED(RCH_PASS_UP_MESSAGE);
#define RCH_PASS_UP_MESSAGE RCH_PASS_UP_MESSAGE_NOT_SUPPORTED
GEN_NOT_SUPPORTED(RCH_PROCESSING_EXTENDED_MESSAGE);
#define RCH_PROCESSING_EXTENDED_MESSAGE \
	RCH_PROCESSING_EXTENDED_MESSAGE_NOT_SUPPORTED
GEN_NOT_SUPPORTED(RCH_REQUESTING_CHUNK);
#define RCH_REQUESTING_CHUNK RCH_REQUESTING_CHUNK_NOT_SUPPORTED
GEN_NOT_SUPPORTED(RCH_WAITING_CHUNK);
#define RCH_WAITING_CHUNK RCH_WAITING_CHUNK_NOT_SUPPORTED
GEN_NOT_SUPPORTED(RCH_REPORT_ERROR);
#define RCH_REPORT_ERROR RCH_REPORT_ERROR_NOT_SUPPORTED

GEN_NOT_SUPPORTED(TCH_WAIT_FOR_MESSAGE_REQUEST_FROM_PE);
#define TCH_WAIT_FOR_MESSAGE_REQUEST_FROM_PE \
	TCH_WAIT_FOR_MESSAGE_REQUEST_FROM_PE_NOT_SUPPORTED
GEN_NOT_SUPPORTED(TCH_WAIT_FOR_TRANSMISSION_COMPLETE);
#define TCH_WAIT_FOR_TRANSMISSION_COMPLETE \
	TCH_WAIT_FOR_TRANSMISSION_COMPLETE_NOT_SUPPORTED
GEN_NOT_SUPPORTED(TCH_CONSTRUCT_CHUNKED_MESSAGE);
#define TCH_CONSTRUCT_CHUNKED_MESSAGE \
	TCH_CONSTRUCT_CHUNKED_MESSAGE_NOT_SUPPORTED
GEN_NOT_SUPPORTED(TCH_SENDING_CHUNKED_MESSAGE);
#define TCH_SENDING_CHUNKED_MESSAGE TCH_SENDING_CHUNKED_MESSAGE_NOT_SUPPORTED
GEN_NOT_SUPPORTED(TCH_WAIT_CHUNK_REQUEST);
#define TCH_WAIT_CHUNK_REQUEST TCH_WAIT_CHUNK_REQUEST_NOT_SUPPORTED
GEN_NOT_SUPPORTED(TCH_MESSAGE_RECEIVED);
#define TCH_MESSAGE_RECEIVED TCH_MESSAGE_RECEIVED_NOT_SUPPORTED
GEN_NOT_SUPPORTED(TCH_MESSAGE_SENT);
#define TCH_MESSAGE_SENT TCH_MESSAGE_SENT_NOT_SUPPORTED
GEN_NOT_SUPPORTED(TCH_REPORT_ERROR);
#define TCH_REPORT_ERROR TCH_REPORT_ERROR_NOT_SUPPORTED
#endif /* !CONFIG_USB_PD_REV30 */

/* To store the time stamp when TCPC sets TX Complete Success */
static timestamp_t tcpc_tx_success_ts[CONFIG_USB_PD_PORT_MAX_COUNT];

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

/* Print the protocol transmit statemachine's current state. */
static void print_current_prl_tx_state(const int port)
{
	prl_event_log_append(PRL_EVENT_LOG_STATE_TX, port);
	if (prl_debug_level >= DEBUG_LEVEL_3)
		CPRINTS("C%d: %s", port,
			prl_tx_state_names[prl_tx_get_state(port)]);
}

/* Set the hard reset statemachine to a new state. */
static void set_state_prl_hr(const int port,
			     const enum usb_prl_hr_state new_state)
{
	set_state(port, &prl_hr[port].ctx, &prl_hr_states[new_state]);
}

/* Get the hard reset statemachine's current state. */
enum usb_prl_hr_state prl_hr_get_state(const int port)
{
	return prl_hr[port].ctx.current - &prl_hr_states[0];
}

/* Print the hard reset statemachine's current state. */
static void print_current_prl_hr_state(const int port)
{
	prl_event_log_append(PRL_EVENT_LOG_STATE_HR, port);
	if (prl_debug_level >= DEBUG_LEVEL_3)
		CPRINTS("C%d: %s", port,
			prl_hr_state_names[prl_hr_get_state(port)]);
}

/* Set the chunked Rx statemachine to a new state. */
static void set_state_rch(const int port, const enum usb_rch_state new_state)
{
	if (IS_ENABLED(CONFIG_USB_PD_EXTENDED_MESSAGES))
		set_state(port, &rch[port].ctx, &rch_states[new_state]);
}

#ifdef CONFIG_USB_PD_EXTENDED_MESSAGES
/* Get the chunked Rx statemachine's current state. */
test_export_static enum usb_rch_state rch_get_state(const int port)
{
	return rch[port].ctx.current - &rch_states[0];
}

/* Print the chunked Rx statemachine's current state. */
static void print_current_rch_state(const int port)
{
	prl_event_log_append(PRL_EVENT_LOG_STATE_RCH, port);
	if (prl_debug_level >= DEBUG_LEVEL_3)
		CPRINTS("C%d: %s", port, rch_state_names[rch_get_state(port)]);
}
#endif /* CONFIG_USB_PD_EXTENDED_MESSAGES */

/* Set the chunked Tx statemachine to a new state. */
static void set_state_tch(const int port, const enum usb_tch_state new_state)
{
	if (IS_ENABLED(CONFIG_USB_PD_EXTENDED_MESSAGES))
		set_state(port, &tch[port].ctx, &tch_states[new_state]);
}

/* Get the chunked Tx statemachine's current state. */
test_export_static enum usb_tch_state tch_get_state(const int port)
{
	if (IS_ENABLED(CONFIG_USB_PD_EXTENDED_MESSAGES))
		return tch[port].ctx.current - &tch_states[0];
	else
		return 0;
}

#ifdef CONFIG_USB_PD_EXTENDED_MESSAGES
/* Print the chunked Tx statemachine's current state. */
static void print_current_tch_state(const int port)
{
	prl_event_log_append(PRL_EVENT_LOG_STATE_TCH, port);
	if (prl_debug_level >= DEBUG_LEVEL_3)
		CPRINTS("C%d: %s", port, tch_state_names[tch_get_state(port)]);
}
#endif /* CONFIG_USB_PD_EXTENDED_MESSAGES */

timestamp_t prl_get_tcpc_tx_success_ts(int port)
{
	return tcpc_tx_success_ts[port];
}

/* Sets the time stamp when TCPC reports TX success. */
static void set_tcpc_tx_success_ts(int port)
{
	tcpc_tx_success_ts[port] = get_time();
}

void pd_transmit_complete(int port, int status)
{
	if (status == TCPC_TX_COMPLETE_SUCCESS)
		set_tcpc_tx_success_ts(port);
	prl_tx[port].xmit_status = status;
}

void pd_execute_hard_reset(int port)
{
	/* Only allow async. function calls when state machine is running */
	if (!prl_is_running(port))
		return;

	PRL_HR_SET_FLAG(port, PRL_FLAGS_PORT_PARTNER_HARD_RESET);
	set_state_prl_hr(port, PRL_HR_RESET_LAYER);
	task_wake(PD_PORT_TO_TASK_ID(port));
}

void prl_execute_hard_reset(int port)
{
	/* Only allow async. function calls when state machine is running */
	if (!prl_is_running(port))
		return;

	PRL_HR_SET_FLAG(port, PRL_FLAGS_PE_HARD_RESET);
	set_state_prl_hr(port, PRL_HR_RESET_LAYER);
	task_wake(PD_PORT_TO_TASK_ID(port));
}

void prl_set_data_role_check(int port, bool enable)
{
	if (enable)
		RCH_CLR_FLAG(port, PRL_FLAGS_IGNORE_DATA_ROLE);
	else
		RCH_SET_FLAG(port, PRL_FLAGS_IGNORE_DATA_ROLE);
}

int prl_is_running(int port)
{
	return local_state[port] == SM_RUN;
}

static void prl_init(int port)
{
	int i;
	const struct sm_ctx cleared = {};

	/*
	 * flags without PRL_FLAGS_SINK_NG present means we are initially
	 * in SinkTxOK state
	 */
	prl_tx[port].flags = 0;
	if (IS_ENABLED(CONFIG_USB_PD_REV30))
		typec_select_src_collision_rp(port, SINK_TX_OK);
	prl_tx[port].last_xmit_type = TCPCI_MSG_SOP;
	prl_tx[port].xmit_status = TCPC_TX_UNSET;

	if (IS_ENABLED(CONFIG_USB_PD_REV30)) {
		tch[port].flags = 0;
		rch[port].flags = 0;
	}

	pdmsg[port].flags = 0;

	prl_hr[port].flags = 0;

	for (i = 0; i < NUM_SOP_STAR_TYPES; i++)
		prl_reset_msg_ids(port, i);

	pd_timer_disable_range(port, PR_TIMER_RANGE);

	/* Clear state machines and set initial states */
	prl_tx[port].ctx = cleared;
	set_state_prl_tx(port, PRL_TX_PHY_LAYER_RESET);

	if (IS_ENABLED(CONFIG_USB_PD_EXTENDED_MESSAGES)) {
		rch[port].ctx = cleared;
		set_state_rch(port, RCH_WAIT_FOR_MESSAGE_FROM_PROTOCOL_LAYER);

		tch[port].ctx = cleared;
		set_state_tch(port, TCH_WAIT_FOR_MESSAGE_REQUEST_FROM_PE);
	}

	prl_hr[port].ctx = cleared;
	set_state_prl_hr(port, PRL_HR_WAIT_FOR_REQUEST);
}

bool prl_is_busy(int port)
{
#ifdef CONFIG_USB_PD_EXTENDED_MESSAGES
	return rch_get_state(port) !=
		       RCH_WAIT_FOR_MESSAGE_FROM_PROTOCOL_LAYER ||
	       tch_get_state(port) != TCH_WAIT_FOR_MESSAGE_REQUEST_FROM_PE;
#else
	return false;
#endif /* CONFIG_USB_PD_EXTENDED_MESSAGES */
}

void prl_set_debug_level(enum debug_level debug_level)
{
#ifndef CONFIG_USB_PD_DEBUG_LEVEL
	prl_debug_level = debug_level;
#endif
}

void prl_hard_reset_complete(int port)
{
	PRL_HR_SET_FLAG(port, PRL_FLAGS_HARD_RESET_COMPLETE);
	task_wake(PD_PORT_TO_TASK_ID(port));
}

void prl_send_ctrl_msg(int port, enum tcpci_msg_type type,
		       enum pd_ctrl_msg_type msg)
{
	pdmsg[port].xmit_type = type;
	pdmsg[port].msg_type = msg;
	pdmsg[port].data_objs = 0;
	tx_emsg[port].len = 0;

#ifdef CONFIG_USB_PD_EXTENDED_MESSAGES
	pdmsg[port].ext = 0;

	TCH_SET_FLAG(port, PRL_FLAGS_MSG_XMIT);
#else
	PRL_TX_SET_FLAG(port, PRL_FLAGS_MSG_XMIT);
#endif /* CONFIG_USB_PD_EXTENDED_MESSAGES */

	task_wake(PD_PORT_TO_TASK_ID(port));
}

void prl_send_data_msg(int port, enum tcpci_msg_type type,
		       enum pd_data_msg_type msg)
{
	pdmsg[port].xmit_type = type;
	pdmsg[port].msg_type = msg;

#ifdef CONFIG_USB_PD_EXTENDED_MESSAGES
	pdmsg[port].ext = 0;

	TCH_SET_FLAG(port, PRL_FLAGS_MSG_XMIT);
#else
	prl_copy_msg_to_buffer(port);
	PRL_TX_SET_FLAG(port, PRL_FLAGS_MSG_XMIT);
#endif /* CONFIG_USB_PD_EXTENDED_MESSAGES */

	task_wake(PD_PORT_TO_TASK_ID(port));
}

#ifdef CONFIG_USB_PD_EXTENDED_MESSAGES
void prl_send_ext_data_msg(int port, enum tcpci_msg_type type,
			   enum pd_ext_msg_type msg)
{
	pdmsg[port].xmit_type = type;
	pdmsg[port].msg_type = msg;
	pdmsg[port].ext = 1;

	TCH_SET_FLAG(port, PRL_FLAGS_MSG_XMIT);
	task_wake(PD_PORT_TO_TASK_ID(port));
}
#endif /* CONFIG_USB_PD_EXTENDED_MESSAGES */

void prl_set_default_pd_revision(int port)
{
	/*
	 * Initialize to highest revision supported. If the port or cable
	 * partner doesn't support this revision, the Protocol Engine will
	 * lower this value to the revision supported by the partner.
	 */
	pdmsg[port].rev[TCPCI_MSG_SOP] = PD_REVISION;
	pdmsg[port].rev[TCPCI_MSG_SOP_PRIME] = PD_REVISION;
	pdmsg[port].rev[TCPCI_MSG_SOP_PRIME_PRIME] = PD_REVISION;
	pdmsg[port].rev[TCPCI_MSG_SOP_DEBUG_PRIME] = PD_REVISION;
	pdmsg[port].rev[TCPCI_MSG_SOP_DEBUG_PRIME_PRIME] = PD_REVISION;
}

void prl_reset_soft(int port)
{
	/* Do not change negotiated PD Revision Specification level */
	local_state[port] = SM_INIT;

	/* Ensure we process the reset quickly */
	task_wake(PD_PORT_TO_TASK_ID(port));
}

void prl_run(int port, int evt, int en)
{
	switch (local_state[port]) {
	case SM_PAUSED:
		if (!en)
			break;
		__fallthrough;
	case SM_INIT:
		prl_init(port);
		local_state[port] = SM_RUN;
		__fallthrough;
	case SM_RUN:
		if (!en) {
			/* Disable RX */
			if (IS_ENABLED(CONFIG_USB_CTVPD) ||
			    IS_ENABLED(CONFIG_USB_VPD))
				vpd_rx_enable(0);
			else
				tcpm_set_rx_enable(port, 0);

			local_state[port] = SM_PAUSED;
			break;
		}

		/* Run Protocol Layer Hard Reset state machine */
		run_state(port, &prl_hr[port].ctx);

		/*
		 * If the Hard Reset state machine is active, then there is no
		 * need to execute any other PRL state machines. When the hard
		 * reset is complete, all PRL state machines will have been
		 * reset.
		 */
		if (prl_hr_get_state(port) == PRL_HR_WAIT_FOR_REQUEST) {
			/* Run Protocol Layer Message Reception */
			prl_rx_wait_for_phy_message(port, evt);

			if (IS_ENABLED(CONFIG_USB_PD_EXTENDED_MESSAGES)) {
				/*
				 * Run RX Chunked state machine after prl_rx.
				 * This is what informs the PE of incoming
				 * message. Its input is prl_rx
				 */
				run_state(port, &rch[port].ctx);

				/*
				 * Run TX Chunked state machine before prl_tx
				 * in case we need to split an extended message
				 * and prl_tx can send it for us
				 */
				run_state(port, &tch[port].ctx);
			}

			/* Run Protocol Layer Message Tx state machine */
			run_state(port, &prl_tx[port].ctx);

			if (IS_ENABLED(CONFIG_USB_PD_EXTENDED_MESSAGES))
				/*
				 * Run TX Chunked state machine again after
				 * prl_tx so we can handle passing TX_COMPLETE
				 * (or failure) up to PE in a single iteration.
				 */
				run_state(port, &tch[port].ctx);
		}
		break;
	}
}

void prl_set_rev(int port, enum tcpci_msg_type type, enum pd_rev_type rev)
{
	/* We only store revisions for SOP* types. */
	ASSERT(type < NUM_SOP_STAR_TYPES);

	pdmsg[port].rev[type] = rev;
}

enum pd_rev_type prl_get_rev(int port, enum tcpci_msg_type type)
{
	/* We only store revisions for SOP* types. */
	ASSERT(type < NUM_SOP_STAR_TYPES);

	return pdmsg[port].rev[type];
}

void prl_reset_msg_ids(int port, enum tcpci_msg_type type)
{
	prl_tx[port].msg_id_counter[type] = 0;
	prl_rx[port].msg_id[type] = -1;
}

static void prl_copy_msg_to_buffer(int port)
{
	/*
	 * Control Messages will have a length of 0 and
	 * no need to spend time with the tx_chk_buf
	 * for this path
	 */
	if (tx_emsg[port].len == 0) {
		pdmsg[port].data_objs = 0;
		return;
	}

	/*
	 * Make sure the Policy Engine isn't sending
	 * more than CHK_BUF_SIZE_BYTES. If so,
	 * truncate len. This will surely send a
	 * malformed packet resulting in the port
	 * partner soft\hard resetting us.
	 */
	if (tx_emsg[port].len > CHK_BUF_SIZE_BYTES)
		tx_emsg[port].len = CHK_BUF_SIZE_BYTES;

	/* Copy message to chunked buffer */
	memset((uint8_t *)pdmsg[port].tx_chk_buf, 0, CHK_BUF_SIZE_BYTES);
	memcpy((uint8_t *)pdmsg[port].tx_chk_buf, (uint8_t *)tx_emsg[port].buf,
	       tx_emsg[port].len);
	/*
	 * Pad length to 4-byte boundary and
	 * convert to number of 32-bit objects.
	 * Since the value is shifted right by 2,
	 * no need to explicitly clear the lower
	 * 2-bits.
	 */
	pdmsg[port].data_objs = (tx_emsg[port].len + 3) >> 2;
}

static __maybe_unused int pdmsg_xmit_type_is_rev30(const int port)
{
	if (IS_ENABLED(CONFIG_USB_PD_REV30))
		return ((pdmsg[port].xmit_type < NUM_SOP_STAR_TYPES) &&
			(prl_get_rev(port, pdmsg[port].xmit_type) == PD_REV30));
	else
		return 0;
}

/* Returns true if the SOP port partner operates at PD rev3.0 */
static bool is_sop_rev30(const int port)
{
	return IS_ENABLED(CONFIG_USB_PD_REV30) &&
	       prl_get_rev(port, TCPCI_MSG_SOP) == PD_REV30;
}

/* Common Protocol Layer Message Transmission */
static void prl_tx_phy_layer_reset_entry(const int port)
{
	print_current_prl_tx_state(port);

	if (IS_ENABLED(CONFIG_USB_CTVPD) || IS_ENABLED(CONFIG_USB_VPD)) {
		vpd_rx_enable(pd_is_connected(port));
	} else {
		/* Note: can't clear PHY messages due to TCPC architecture */
		/* Enable communications*/
		tcpm_set_rx_enable(port, pd_is_connected(port));
	}
	set_state_prl_tx(port, PRL_TX_WAIT_FOR_MESSAGE_REQUEST);
}

static void prl_tx_wait_for_message_request_entry(const int port)
{
	/* No phy layer response is pending */
	prl_tx[port].xmit_status = TCPC_TX_UNSET;
	print_current_prl_tx_state(port);
}

static void prl_tx_wait_for_message_request_run(const int port)
{
	/* Clear any AMS flags and state if we are no longer in an AMS */
	if (IS_ENABLED(CONFIG_USB_PD_REV30) && !pe_in_local_ams(port)) {
		/* Note PRL_Tx_Src_Sink_Tx is embedded here. */
		if (PRL_TX_CHK_FLAG(port, PRL_FLAGS_SINK_NG)) {
			typec_select_src_collision_rp(port, SINK_TX_OK);
			typec_update_cc(port);
		}
		PRL_TX_CLR_FLAG(port,
				PRL_FLAGS_SINK_NG | PRL_FLAGS_WAIT_SINK_OK);
	}

	/*
	 * Check if we are starting an AMS and need to wait and/or set the CC
	 * lines appropriately.
	 */
	if (IS_ENABLED(CONFIG_USB_PD_REV30) && is_sop_rev30(port) &&
	    pe_in_local_ams(port)) {
		if (PRL_TX_CHK_FLAG(port, PRL_FLAGS_SINK_NG |
						  PRL_FLAGS_WAIT_SINK_OK)) {
			/*
			 * If we are already in an AMS then allow the
			 * multi-message AMS to continue, even if we
			 * swap power roles.
			 *
			 * Fall Through using the current AMS
			 */
		} else {
			/*
			 * Start of SRC AMS notification received from
			 * Policy Engine
			 */
			if (pd_get_power_role(port) == PD_ROLE_SOURCE) {
				PRL_TX_SET_FLAG(port, PRL_FLAGS_SINK_NG);
				set_state_prl_tx(port, PRL_TX_SRC_SOURCE_TX);
			} else {
				PRL_TX_SET_FLAG(port, PRL_FLAGS_WAIT_SINK_OK);
				set_state_prl_tx(port, PRL_TX_SNK_START_AMS);
			}
			return;
		}
	}

	/* Handle non Rev 3.0 or subsequent messages in AMS sequence */
	if (PRL_TX_CHK_FLAG(port, PRL_FLAGS_MSG_XMIT)) {
		PRL_TX_CLR_FLAG(port, PRL_FLAGS_MSG_XMIT);
		/*
		 * Soft Reset Message Message pending
		 */
		if ((pdmsg[port].msg_type == PD_CTRL_SOFT_RESET) &&
		    (tx_emsg[port].len == 0)) {
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
	print_current_prl_tx_state(port);

	/*
	 * Discard queued message
	 * Note: We differ from spec here, which allows us to not discard on
	 * incoming SOP' or SOP''.  However this would get the TCH out of sync.
	 *
	 * prl_tx will be set to this state following message reception in
	 * prl_rx. So this path will be entered following each rx message. If
	 * this state is entered, and there is either a message from the PE
	 * pending, or if a message was passed to the phy and there is either no
	 * response yet, or it was discarded in the phy layer, then a tx message
	 * discard event has been detected.
	 */
	if (PRL_TX_CHK_FLAG(port, PRL_FLAGS_MSG_XMIT) ||
	    prl_tx[port].xmit_status == TCPC_TX_WAIT ||
	    prl_tx[port].xmit_status == TCPC_TX_COMPLETE_DISCARDED) {
		PRL_TX_CLR_FLAG(port, PRL_FLAGS_MSG_XMIT);
		increment_msgid_counter(port);
		pe_report_discard(port);
	}

	set_state_prl_tx(port, PRL_TX_PHY_LAYER_RESET);
}

#ifdef CONFIG_USB_PD_REV30
/*
 * PrlTxSrcSourceTx
 */
static void prl_tx_src_source_tx_entry(const int port)
{
	print_current_prl_tx_state(port);

	/* Set Rp = SinkTxNG */
	typec_select_src_collision_rp(port, SINK_TX_NG);
	typec_update_cc(port);
}

static void prl_tx_src_source_tx_run(const int port)
{
	if (PRL_TX_CHK_FLAG(port, PRL_FLAGS_MSG_XMIT)) {
		/*
		 * Don't clear pending XMIT flag here. Wait until we send so
		 * we can detect if we dropped this message or not.
		 */
		set_state_prl_tx(port, PRL_TX_SRC_PENDING);
	}
}

/*
 * PrlTxSnkStartAms
 */
static void prl_tx_snk_start_ams_entry(const int port)
{
	print_current_prl_tx_state(port);
}

static void prl_tx_snk_start_ams_run(const int port)
{
	if (PRL_TX_CHK_FLAG(port, PRL_FLAGS_MSG_XMIT)) {
		/*
		 * Don't clear pending XMIT flag here. Wait until we send so
		 * we can detect if we dropped this message or not.
		 */
		set_state_prl_tx(port, PRL_TX_SNK_PENDING);
	}
}
#endif /* CONFIG_USB_PD_REV30 */

/*
 * PrlTxLayerResetForTransmit
 */
static void prl_tx_layer_reset_for_transmit_entry(const int port)
{
	print_current_prl_tx_state(port);

	if (pdmsg[port].xmit_type < NUM_SOP_STAR_TYPES) {
		/*
		 * This state is only used during soft resets. Reset only the
		 * matching message type.
		 *
		 * From section 6.3.13 Soft Reset Message in the USB PD 3.0
		 * v2.0 spec, Soft_Reset Message Shall be targeted at a
		 * specific entity depending on the type of SOP* Packet used.
		 *
		 *
		 * From section 6.11.2.3.2, the MessageID should be cleared
		 * from the PRL_Rx_Layer_Reset_for_Receive state. However, we
		 * don't implement a full state machine for PRL RX states so
		 * clear the MessageID here.
		 */
		prl_reset_msg_ids(port, pdmsg[port].xmit_type);
	}
}

static void prl_tx_layer_reset_for_transmit_run(const int port)
{
	/* NOTE: PRL_Tx_Construct_Message State embedded here */
	prl_tx_construct_message(port);
	set_state_prl_tx(port, PRL_TX_WAIT_FOR_PHY_RESPONSE);
}

static uint32_t get_sop_star_header(const int port)
{
	const int is_sop_packet = pdmsg[port].xmit_type == TCPCI_MSG_SOP;
	int ext;

#ifdef CONFIG_USB_PD_EXTENDED_MESSAGES
	ext = pdmsg[port].ext;
#else
	ext = 0;
#endif

	/* SOP vs SOP'/SOP" headers are different. Replace fields as needed */
	return PD_HEADER(pdmsg[port].msg_type,
			 is_sop_packet ? pd_get_power_role(port) :
					 tc_get_cable_plug(port),
			 is_sop_packet ? pd_get_data_role(port) : 0,
			 prl_tx[port].msg_id_counter[pdmsg[port].xmit_type],
			 pdmsg[port].data_objs,
			 pdmsg[port].rev[pdmsg[port].xmit_type], ext);
}

static void prl_tx_construct_message(const int port)
{
	/* The header is unused for hard reset, etc. */
	const uint32_t header = pdmsg[port].xmit_type < NUM_SOP_STAR_TYPES ?
					get_sop_star_header(port) :
					0;

	/* Save SOP* so the correct msg_id_counter can be incremented */
	prl_tx[port].last_xmit_type = pdmsg[port].xmit_type;

	/* Indicate that a tx message is being passed to the phy layer */
	prl_tx[port].xmit_status = TCPC_TX_WAIT;
	/*
	 * PRL_FLAGS_TX_COMPLETE could be set if this function is called before
	 * the Policy Engine is informed of the previous transmission. Clear the
	 * flag so that this message can be sent.
	 */
	PDMSG_CLR_FLAG(port, PRL_FLAGS_TX_COMPLETE);

	/*
	 * Pass message to PHY Layer. It handles retries in hardware as the EC
	 * cannot handle the required timing ~ 1ms (tReceive + tRetry).
	 *
	 * Note if we ever start sending large, extendend messages, then we
	 * should not retry those messages. We do not support that and probably
	 * never will (since we support chunking).
	 */
	tcpm_transmit(port, pdmsg[port].xmit_type, header,
		      pdmsg[port].tx_chk_buf);
}

/*
 * PrlTxWaitForPhyResponse
 */
static void prl_tx_wait_for_phy_response_entry(const int port)
{
	print_current_prl_tx_state(port);

	pd_timer_enable(port, PR_TIMER_TCPC_TX_TIMEOUT, PD_T_TCPC_TX_TIMEOUT);
}

static void prl_tx_wait_for_phy_response_run(const int port)
{
	/* Wait until TX is complete */

	/*
	 * NOTE: The TCPC will set xmit_status to TCPC_TX_COMPLETE_DISCARDED
	 *       when a GoodCRC containing an incorrect MessageID is received.
	 *       This condition satisfies the PRL_Tx_Match_MessageID state
	 *       requirement.
	 */

	if (prl_tx[port].xmit_status == TCPC_TX_COMPLETE_SUCCESS) {
		/* NOTE: PRL_TX_Message_Sent State embedded here. */
		/* Increment messageId counter */
		increment_msgid_counter(port);

		/* Inform Policy Engine Message was sent */
		if (IS_ENABLED(CONFIG_USB_PD_EXTENDED_MESSAGES))
			PDMSG_SET_FLAG(port, PRL_FLAGS_TX_COMPLETE);
		else
			pe_message_sent(port);

		/*
		 * This event reduces the time of informing the policy engine of
		 * the transmission by one state machine cycle
		 */
		task_wake(PD_PORT_TO_TASK_ID(port));
		set_state_prl_tx(port, PRL_TX_WAIT_FOR_MESSAGE_REQUEST);
	} else if (pd_timer_is_expired(port, PR_TIMER_TCPC_TX_TIMEOUT) ||
		   prl_tx[port].xmit_status == TCPC_TX_COMPLETE_FAILED) {
		/*
		 * NOTE: PRL_Tx_Transmission_Error State embedded
		 * here.
		 */

		if (IS_ENABLED(CONFIG_USB_PD_EXTENDED_MESSAGES)) {
			/*
			 * State tch_wait_for_transmission_complete will
			 * inform policy engine of error
			 */
			PDMSG_SET_FLAG(port, PRL_FLAGS_TX_ERROR);
		} else {
			/* Report Error To Policy Engine */
			pe_report_error(port, ERR_TCH_XMIT,
					prl_tx[port].last_xmit_type);
		}

		/* Increment message id counter */
		increment_msgid_counter(port);
		set_state_prl_tx(port, PRL_TX_WAIT_FOR_MESSAGE_REQUEST);
	}
}

static void prl_tx_wait_for_phy_response_exit(const int port)
{
	pd_timer_disable(port, PR_TIMER_TCPC_TX_TIMEOUT);
}

/* Source Protocol Layer Message Transmission */
/*
 * PrlTxSrcPending
 */
static void prl_tx_src_pending_entry(const int port)
{
	print_current_prl_tx_state(port);

	/* Start SinkTxTimer */
	pd_timer_enable(port, PR_TIMER_SINK_TX, PD_T_SINK_TX);
}

static void prl_tx_src_pending_run(const int port)
{
	if (pd_timer_is_expired(port, PR_TIMER_SINK_TX)) {
		/*
		 * We clear the pending XMIT flag here right before we send so
		 * we can detect if we discarded this message or not
		 */
		PRL_TX_CLR_FLAG(port, PRL_FLAGS_MSG_XMIT);

		/*
		 * Soft Reset Message pending &
		 * SinkTxTimer timeout
		 */
		if ((tx_emsg[port].len == 0) &&
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

static void prl_tx_src_pending_exit(int port)
{
	pd_timer_disable(port, PR_TIMER_SINK_TX);
}

/*
 * PrlTxSnkPending
 */
static void prl_tx_snk_pending_entry(const int port)
{
	print_current_prl_tx_state(port);
}

static void prl_tx_snk_pending_run(const int port)
{
	bool start_tx = false;

	/*
	 * Wait unit the SRC applies SINK_TX_OK so we can transmit. In FRS mode,
	 * don't wait for SINK_TX_OK since either the source (and Rp) could be
	 * gone or the TCPC CC_STATUS update time could be too long to meet
	 * tFRSwapInit.
	 */
	if (pe_in_frs_mode(port)) {
		/* shortcut to save some i2c_xfer calls on the FRS path. */
		start_tx = true;
	} else {
		enum tcpc_cc_voltage_status cc1, cc2;

		tcpm_get_cc(port, &cc1, &cc2);
		start_tx = (cc1 == TYPEC_CC_VOLT_RP_3_0 ||
			    cc2 == TYPEC_CC_VOLT_RP_3_0);
	}
	if (start_tx) {
		/*
		 * We clear the pending XMIT flag here right before we send so
		 * we can detect if we discarded this message or not
		 */
		PRL_TX_CLR_FLAG(port, PRL_FLAGS_MSG_XMIT);

		/*
		 * Soft Reset Message Message pending &
		 * Rp = SinkTxOk
		 */
		if ((pdmsg[port].msg_type == PD_CTRL_SOFT_RESET) &&
		    (tx_emsg[port].len == 0)) {
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
void prl_hr_send_msg_to_phy(const int port)
{
	/* Header is not used for hard reset */
	const uint32_t header = 0;

	pdmsg[port].xmit_type = TCPCI_MSG_TX_HARD_RESET;

	/*
	 * These flags could be set if this function is called before the
	 * Policy Engine is informed of the previous transmission. Clear the
	 * flags so that this message can be sent.
	 */
	prl_tx[port].xmit_status = TCPC_TX_UNSET;
	PDMSG_CLR_FLAG(port, PRL_FLAGS_TX_COMPLETE);

	/* Pass message to PHY Layer */
	tcpm_transmit(port, pdmsg[port].xmit_type, header,
		      pdmsg[port].tx_chk_buf);
}

static void prl_hr_wait_for_request_entry(const int port)
{
	print_current_prl_hr_state(port);

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

	print_current_prl_hr_state(port);

	if (IS_ENABLED(CONFIG_USB_PD_EXTENDED_MESSAGES)) {
		tch[port].flags = 0;
		rch[port].flags = 0;
	}

	pdmsg[port].flags = 0;

	/* Hard reset resets messageIDCounters for all TX types */
	for (i = 0; i < NUM_SOP_STAR_TYPES; i++) {
		prl_reset_msg_ids(port, i);
	}

	/* Disable RX */
	if (IS_ENABLED(CONFIG_USB_CTVPD) || IS_ENABLED(CONFIG_USB_VPD))
		vpd_rx_enable(0);
	else
		tcpm_set_rx_enable(port, 0);

	/*
	 * PD r3.0 v2.0, ss6.2.1.1.5:
	 * After a physical or logical (USB Type-C Error Recovery) Attach, a
	 * Port discovers the common Specification Revision level between itself
	 * and its Port Partner and/or the Cable Plug(s), and uses this
	 * Specification Revision level until a Detach, Hard Reset or Error
	 * Recovery happens.
	 *
	 * This covers the Hard Reset case.
	 */
	prl_set_default_pd_revision(port);

	/* Inform the AP of Hard Reset */
	if (IS_ENABLED(CONFIG_USB_PD_HOST_CMD))
		pd_notify_event(port, PD_STATUS_EVENT_HARD_RESET);

	/*
	 * Protocol Layer message transmission transitions to
	 * PRL_Tx_Wait_For_Message_Request state.
	 */
	set_state_prl_tx(port, PRL_TX_WAIT_FOR_MESSAGE_REQUEST);

	return;
}

static void prl_hr_reset_layer_run(const int port)
{
	/*
	 * Protocol Layer reset Complete &
	 * Hard Reset was initiated by Policy Engine
	 */
	if (PRL_HR_CHK_FLAG(port, PRL_FLAGS_PE_HARD_RESET)) {
		/*
		 * Request PHY to perform a Hard Reset. Note
		 * PRL_HR_Request_Reset state is embedded here.
		 */
		prl_hr_send_msg_to_phy(port);
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
	print_current_prl_hr_state(port);

	/* Start HardResetCompleteTimer */
	pd_timer_enable(port, PR_TIMER_HARD_RESET_COMPLETE, PD_T_PS_HARD_RESET);
}

static void prl_hr_wait_for_phy_hard_reset_complete_run(const int port)
{
	/*
	 * Wait for hard reset from PHY
	 * or timeout
	 */
	if (PDMSG_CHK_FLAG(port, PRL_FLAGS_TX_COMPLETE) ||
	    pd_timer_is_expired(port, PR_TIMER_HARD_RESET_COMPLETE)) {
		/* PRL_HR_PHY_Hard_Reset_Requested */

		/* Inform Policy Engine Hard Reset was sent */
		pe_hard_reset_sent(port);
		set_state_prl_hr(port, PRL_HR_WAIT_FOR_PE_HARD_RESET_COMPLETE);

		return;
	}
}

static void prl_hr_wait_for_phy_hard_reset_complete_exit(int port)
{
	pd_timer_disable(port, PR_TIMER_HARD_RESET_COMPLETE);
}

/*
 * PrlHrWaitForPeHardResetComplete
 */
static void prl_hr_wait_for_pe_hard_reset_complete_entry(const int port)
{
	print_current_prl_hr_state(port);
}

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
	if (IS_ENABLED(CONFIG_USB_PD_EXTENDED_MESSAGES)) {
		set_state_rch(port, RCH_WAIT_FOR_MESSAGE_FROM_PROTOCOL_LAYER);
		set_state_tch(port, TCH_WAIT_FOR_MESSAGE_REQUEST_FROM_PE);
	}
}

static void copy_chunk_to_ext(int port)
{
	/* Calculate number of bytes */
	pdmsg[port].num_bytes_received =
		(PD_HEADER_CNT(rx_emsg[port].header) * 4);

	/* Copy chunk into extended message */
	memcpy((uint8_t *)rx_emsg[port].buf, (uint8_t *)pdmsg[port].rx_chk_buf,
	       pdmsg[port].num_bytes_received);

	/* Set extended message length */
	rx_emsg[port].len = pdmsg[port].num_bytes_received;
}

#ifdef CONFIG_USB_PD_EXTENDED_MESSAGES
/*
 * Chunked Rx State Machine
 */
/*
 * RchWaitForMessageFromProtocolLayer
 */
static void rch_wait_for_message_from_protocol_layer_entry(const int port)
{
	print_current_rch_state(port);

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
		if (pdmsg_xmit_type_is_rev30(port) &&
		    PD_HEADER_EXT(rx_emsg[port].header)) {
			uint16_t exhdr =
				GET_EXT_HEADER(*pdmsg[port].rx_chk_buf);
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
					PD_HEADER_TYPE(rx_emsg[port].header);

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
				rch[port].error = ERR_RCH_CHUNKED;
				set_state_rch(port, RCH_REPORT_ERROR);
			}
		}
		/*
		 * Received Non-Extended Message
		 */
		else if (!PD_HEADER_EXT(rx_emsg[port].header)) {
			/* Copy chunk to extended buffer */
			copy_chunk_to_ext(port);
			set_state_rch(port, RCH_PASS_UP_MESSAGE);
		}
		/*
		 * Received an Extended Message while communicating at a
		 * revision lower than PD3.0
		 */
		else {
			rch[port].error = ERR_RCH_CHUNKED;
			set_state_rch(port, RCH_REPORT_ERROR);
		}
	}
}

/*
 * RchPassUpMessage
 */
static void rch_pass_up_message_entry(const int port)
{
	print_current_rch_state(port);

	/* Pass Message to Policy Engine */
	pe_message_received(port);
	set_state_rch(port, RCH_WAIT_FOR_MESSAGE_FROM_PROTOCOL_LAYER);
}

/*
 * RchProcessingExtendedMessage
 */
static void rch_processing_extended_message_entry(const int port)
{
	print_current_rch_state(port);
}

static void rch_processing_extended_message_run(const int port)
{
	uint16_t exhdr = GET_EXT_HEADER(pdmsg[port].rx_chk_buf[0]);
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

		if (byte_num >= PD_MAX_EXTENDED_MSG_CHUNK_LEN)
			byte_num = PD_MAX_EXTENDED_MSG_CHUNK_LEN;

		/* Make sure extended message buffer does not overflow */
		if (pdmsg[port].num_bytes_received + byte_num >
		    EXTENDED_BUFFER_SIZE) {
			rch[port].error = ERR_RCH_CHUNKED;
			set_state_rch(port, RCH_REPORT_ERROR);
			return;
		}

		/* Append data */
		/* Add 2 to chk_buf to skip over extended message header */
		memcpy(((uint8_t *)rx_emsg[port].buf +
			pdmsg[port].num_bytes_received),
		       (uint8_t *)pdmsg[port].rx_chk_buf + 2, byte_num);
		/* increment chunk number expected */
		pdmsg[port].chunk_number_expected++;
		/* adjust num bytes received */
		pdmsg[port].num_bytes_received += byte_num;

		/* Was that the last chunk? */
		if (pdmsg[port].num_bytes_received >= data_size) {
			rx_emsg[port].len = pdmsg[port].num_bytes_received;
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
	else {
		rch[port].error = ERR_RCH_CHUNKED;
		set_state_rch(port, RCH_REPORT_ERROR);
	}
}

/*
 * RchRequestingChunk
 */
static void rch_requesting_chunk_entry(const int port)
{
	print_current_rch_state(port);

	/*
	 * Send Chunk Request to Protocol Layer
	 * with chunk number = Chunk_Number_Expected
	 */
	pdmsg[port].tx_chk_buf[0] =
		PD_EXT_HEADER(pdmsg[port].chunk_number_expected, 1, /* Request
								       Chunk */
			      0 /* Data Size */
		);

	pdmsg[port].data_objs = 1;
	pdmsg[port].ext = 1;
	pdmsg[port].xmit_type = prl_rx[port].sop;
	PRL_TX_SET_FLAG(port, PRL_FLAGS_MSG_XMIT);
	task_set_event(PD_PORT_TO_TASK_ID(port), PD_EVENT_TX);
}

static void rch_requesting_chunk_run(const int port)
{
	/*
	 * Message Transmitted received from Protocol Layer
	 */
	if (PDMSG_CHK_FLAG(port, PRL_FLAGS_TX_COMPLETE)) {
		PDMSG_CLR_FLAG(port, PRL_FLAGS_TX_COMPLETE);
		set_state_rch(port, RCH_WAITING_CHUNK);
	} else if (PDMSG_CHK_FLAG(port, PRL_FLAGS_TX_ERROR)) {
		/* Transmission Error from Protocol Layer detetected */
		rch[port].error = ERR_RCH_CHUNKED;
		set_state_rch(port, RCH_REPORT_ERROR);
	} else if (RCH_CHK_FLAG(port, PRL_FLAGS_MSG_RECEIVED)) {
		/*
		 * It is possible to have both message received and the chunk
		 * request transmit complete before a full PRL SM run. But, the
		 * PRL_RX state machine runs prior to RCH, but before PRL_TX, so
		 * PRL_FLAGS_MSG_RECEIVED can be set without
		 * PRL_FLAGS_TX_COMPLETE set at this point (though it will be
		 * set as soon as PRL_TX is executed next.
		 */
		set_state_rch(port, RCH_WAITING_CHUNK);
	}
}

/*
 * RchWaitingChunk
 */
static void rch_waiting_chunk_entry(const int port)
{
	print_current_rch_state(port);

	/*
	 * Start ChunkSenderResponseTimer
	 */
	pd_timer_enable(port, PR_TIMER_CHUNK_SENDER_RESPONSE,
			PD_T_CHUNK_SENDER_RESPONSE);
}

static void rch_waiting_chunk_run(const int port)
{
	if (RCH_CHK_FLAG(port, PRL_FLAGS_MSG_RECEIVED)) {
		/*
		 * Because of the 5 msec tick time, it is possible to have both
		 * msg_received and tx_complete flags set for a given PRL sm
		 * run. Since prl_rx runs prior to the tx state machines, clear
		 * the tx_complete flag as the next chunk has already been
		 * received.
		 */
		if (PDMSG_CHK_FLAG(port, PRL_FLAGS_TX_COMPLETE))
			PDMSG_CLR_FLAG(port, PRL_FLAGS_TX_COMPLETE);

		/*
		 * Leave PRL_FLAGS_MSG_RECEIVED flag set just in case an error
		 * is detected. If an error is detected, PRL_FLAGS_MSG_RECEIVED
		 * will be cleared in rch_report_error state.
		 */

		if (PD_HEADER_EXT(rx_emsg[port].header)) {
			uint16_t exhdr =
				GET_EXT_HEADER(pdmsg[port].rx_chk_buf[0]);
			/*
			 * Other Message Received from Protocol Layer
			 */
			if (PD_EXT_HEADER_REQ_CHUNK(exhdr) ||
			    !PD_EXT_HEADER_CHUNKED(exhdr)) {
				rch[port].error = ERR_RCH_CHUNKED;
				set_state_rch(port, RCH_REPORT_ERROR);
			}
			/*
			 * Chunk response Received from Protocol Layer
			 */
			else {
				/*
				 * No error was detected, so clear
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
	else if (pd_timer_is_expired(port, PR_TIMER_CHUNK_SENDER_RESPONSE)) {
		rch[port].error = ERR_RCH_CHUNK_WAIT_TIMEOUT;
		set_state_rch(port, RCH_REPORT_ERROR);
	}
}

static void rch_waiting_chunk_exit(int port)
{
	pd_timer_disable(port, PR_TIMER_CHUNK_SENDER_RESPONSE);
}

/*
 * RchReportError
 */
static void rch_report_error_entry(const int port)
{
	print_current_rch_state(port);

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
		pe_report_error(port, ERR_RCH_MSG_REC, prl_rx[port].sop);
	} else {
		pe_report_error(port, rch[port].error, prl_rx[port].sop);
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
	print_current_tch_state(port);

	/* Clear Abort flag */
	PDMSG_CLR_FLAG(port, PRL_FLAGS_ABORT);

	/* All Messages are chunked */
	TCH_SET_FLAG(port, PRL_FLAGS_CHUNKING);
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
			if (pdmsg_xmit_type_is_rev30(port) && pdmsg[port].ext &&
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
				/* NOTE: TCH_Pass_Down_Message embedded here */
				prl_copy_msg_to_buffer(port);

				/* Pass Message to Protocol Layer */
				PRL_TX_SET_FLAG(port, PRL_FLAGS_MSG_XMIT);
				set_state_tch(
					port,
					TCH_WAIT_FOR_TRANSMISSION_COMPLETE);
			}
		}
	}
}

/*
 * TchWaitForTransmissionComplete
 */
static void tch_wait_for_transmission_complete_entry(const int port)
{
	print_current_tch_state(port);
}

static void tch_wait_for_transmission_complete_run(const int port)
{
	/*
	 * Inform Policy Engine that Message was sent.
	 */
	if (PDMSG_CHK_FLAG(port, PRL_FLAGS_TX_COMPLETE)) {
		PDMSG_CLR_FLAG(port, PRL_FLAGS_TX_COMPLETE);
		set_state_tch(port, TCH_MESSAGE_SENT);
		return;
	}
	/*
	 * Inform Policy Engine of Tx Error
	 */
	else if (PDMSG_CHK_FLAG(port, PRL_FLAGS_TX_ERROR)) {
		PDMSG_CLR_FLAG(port, PRL_FLAGS_TX_ERROR);
		tch[port].error = ERR_TCH_XMIT;
		set_state_tch(port, TCH_REPORT_ERROR);
		return;
	}
	/*
	 * A message was received while TCH is waiting for the phy to complete
	 * sending a tx message.
	 *
	 * Because of our prl_sm architecture and I2C access delays for TCPCs,
	 * it's possible to have a message received and the prl_tx state not be
	 * in its default waiting state. To avoid a false protocol error, only
	 * jump to TCH_MESSAGE_RECEIVED if the phy layer has not indicated that
	 * the tx message was sent successfully.
	 */
	if (TCH_CHK_FLAG(port, PRL_FLAGS_MSG_RECEIVED) &&
	    prl_tx[port].xmit_status != TCPC_TX_COMPLETE_SUCCESS) {
		TCH_CLR_FLAG(port, PRL_FLAGS_MSG_RECEIVED);
		set_state_tch(port, TCH_MESSAGE_RECEIVED);
		return;
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

	print_current_tch_state(port);

	/*
	 * Any message received and not in state TCH_Wait_Chunk_Request
	 */
	if (TCH_CHK_FLAG(port, PRL_FLAGS_MSG_RECEIVED)) {
		TCH_CLR_FLAG(port, PRL_FLAGS_MSG_RECEIVED);
		set_state_tch(port, TCH_MESSAGE_RECEIVED);
		return;
	}

	/* Prepare to copy chunk into chk_buf */

	ext_hdr = (uint16_t *)pdmsg[port].tx_chk_buf;
	data = ((uint8_t *)pdmsg[port].tx_chk_buf + 2);
	num = tx_emsg[port].len - pdmsg[port].send_offset;

	if (num > PD_MAX_EXTENDED_MSG_CHUNK_LEN)
		num = PD_MAX_EXTENDED_MSG_CHUNK_LEN;

	/* Set the chunks extended header */
	*ext_hdr = PD_EXT_HEADER(pdmsg[port].chunk_number_to_send, 0, /* Chunk
									 Request
								       */
				 tx_emsg[port].len);

	/* Copy the message chunk into chk_buf */
	memset(data, 0, 28);
	memcpy(data, tx_emsg[port].buf + pdmsg[port].send_offset, num);
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
static void tch_sending_chunked_message_entry(const int port)
{
	print_current_tch_state(port);
}

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
	else if (tx_emsg[port].len == pdmsg[port].send_offset &&
		 PDMSG_CHK_FLAG(port, PRL_FLAGS_TX_COMPLETE)) {
		PDMSG_CLR_FLAG(port, PRL_FLAGS_TX_COMPLETE);
		set_state_tch(port, TCH_MESSAGE_SENT);
		/*
		 * Any message received and not in state TCH_Wait_Chunk_Request
		 */
	} else if (TCH_CHK_FLAG(port, PRL_FLAGS_MSG_RECEIVED)) {
		TCH_CLR_FLAG(port, PRL_FLAGS_MSG_RECEIVED);
		set_state_tch(port, TCH_MESSAGE_RECEIVED);
	}
	/*
	 * Message Transmitted from Protocol Layer &
	 * Not Last Chunk
	 */
	else if (pdmsg[port].send_offset < tx_emsg[port].len &&
		 PDMSG_CHK_FLAG(port, PRL_FLAGS_TX_COMPLETE)) {
		PDMSG_CLR_FLAG(port, PRL_FLAGS_TX_COMPLETE);
		set_state_tch(port, TCH_WAIT_CHUNK_REQUEST);
	}
}

/*
 * TchWaitChunkRequest
 */
static void tch_wait_chunk_request_entry(const int port)
{
	print_current_tch_state(port);

	/* Increment Chunk Number to Send */
	pdmsg[port].chunk_number_to_send++;
	/* Start Chunk Sender Request Timer */
	pd_timer_enable(port, PR_TIMER_CHUNK_SENDER_REQUEST,
			PD_T_CHUNK_SENDER_REQUEST);
}

static void tch_wait_chunk_request_run(const int port)
{
	if (TCH_CHK_FLAG(port, PRL_FLAGS_MSG_RECEIVED)) {
		TCH_CLR_FLAG(port, PRL_FLAGS_MSG_RECEIVED);

		if (PD_HEADER_EXT(rx_emsg[port].header)) {
			uint16_t exthdr;

			exthdr = GET_EXT_HEADER(pdmsg[port].rx_chk_buf[0]);
			if (PD_EXT_HEADER_REQ_CHUNK(exthdr)) {
				/*
				 * Chunk Request Received &
				 * Chunk Number = Chunk Number to Send
				 */
				if (PD_EXT_HEADER_CHUNK_NUM(exthdr) ==
				    pdmsg[port].chunk_number_to_send) {
					set_state_tch(
						port,
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
	else if (pd_timer_is_expired(port, PR_TIMER_CHUNK_SENDER_REQUEST))
		set_state_tch(port, TCH_MESSAGE_SENT);
}

static void tch_wait_chunk_request_exit(int port)
{
	pd_timer_disable(port, PR_TIMER_CHUNK_SENDER_REQUEST);
}

/*
 * TchMessageReceived
 */
static void tch_message_received_entry(const int port)
{
	print_current_tch_state(port);

	/* Pass message to chunked Rx */
	RCH_SET_FLAG(port, PRL_FLAGS_MSG_RECEIVED);

	/* Clear extended message objects */
	if (TCH_CHK_FLAG(port, PRL_FLAGS_MSG_XMIT)) {
		TCH_CLR_FLAG(port, PRL_FLAGS_MSG_XMIT);
		pe_report_discard(port);
	}
	pdmsg[port].data_objs = 0;
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
	print_current_tch_state(port);

	/* Tell PE message was sent */
	pe_message_sent(port);

	/*
	 * Any message received and not in state TCH_Wait_Chunk_Request
	 * MUST be checked after notifying PE
	 */
	if (TCH_CHK_FLAG(port, PRL_FLAGS_MSG_RECEIVED)) {
		TCH_CLR_FLAG(port, PRL_FLAGS_MSG_RECEIVED);
		set_state_tch(port, TCH_MESSAGE_RECEIVED);
		return;
	}

	set_state_tch(port, TCH_WAIT_FOR_MESSAGE_REQUEST_FROM_PE);
}

/*
 * TchReportError
 */
static void tch_report_error_entry(const int port)
{
	print_current_tch_state(port);

	/* Report Error To Policy Engine */
	pe_report_error(port, tch[port].error, prl_tx[port].last_xmit_type);

	/*
	 * Any message received and not in state TCH_Wait_Chunk_Request
	 * MUST be checked after notifying PE
	 */
	if (TCH_CHK_FLAG(port, PRL_FLAGS_MSG_RECEIVED)) {
		TCH_CLR_FLAG(port, PRL_FLAGS_MSG_RECEIVED);
		set_state_tch(port, TCH_MESSAGE_RECEIVED);
		return;
	}

	set_state_tch(port, TCH_WAIT_FOR_MESSAGE_REQUEST_FROM_PE);
}
#endif /* CONFIG_USB_PD_EXTENDED_MESSAGES */

/*
 * Protocol Layer Message Reception State Machine
 */
static void prl_rx_wait_for_phy_message(const int port, int evt)
{
	uint32_t header;
	uint8_t type;
	uint8_t cnt;
	int8_t msid;

	/*
	 * If PD3, wait for the RX chunk SM to copy the pdmsg into the extended
	 * buffer before overwriting pdmsg.
	 */
	if (IS_ENABLED(CONFIG_USB_PD_EXTENDED_MESSAGES) &&
	    RCH_CHK_FLAG(port, PRL_FLAGS_MSG_RECEIVED))
		return;

	/* If we don't have any message, just stop processing now. */
	if (!tcpm_has_pending_message(port) ||
	    tcpm_dequeue_message(port, pdmsg[port].rx_chk_buf, &header))
		return;

	rx_emsg[port].header = header;
	type = PD_HEADER_TYPE(header);
	cnt = PD_HEADER_CNT(header);
	msid = PD_HEADER_ID(header);
	prl_rx[port].sop = PD_HEADER_GET_SOP(header);

	/* Make sure an incorrect count doesn't overflow the chunk buffer */
	if (cnt > CHK_BUF_SIZE)
		cnt = CHK_BUF_SIZE;

	/* dump received packet content (only dump ping at debug level MAX) */
	if ((prl_debug_level >= DEBUG_LEVEL_2 && type != PD_CTRL_PING) ||
	    prl_debug_level >= DEBUG_LEVEL_3) {
		int p;

		CPRINTF("C%d: RECV %04x/%d ", port, header, cnt);
		for (p = 0; p < cnt; p++)
			CPRINTF("[%d]%08x ", p, pdmsg[port].rx_chk_buf[p]);
		CPRINTF("\n");
	}

	/*
	 * Ignore messages sent to the cable from our
	 * port partner if we aren't Vconn powered device.
	 */
	if (!IS_ENABLED(CONFIG_USB_CTVPD) && !IS_ENABLED(CONFIG_USB_VPD) &&
	    PD_HEADER_GET_SOP(header) != TCPCI_MSG_SOP &&
	    PD_HEADER_PROLE(header) == PD_PLUG_FROM_DFP_UFP)
		return;

	/*
	 * From 6.2.1.1.6 Port Data Role in USB PD Rev 3.1, Ver 1.3
	 * "If a USB Type-C Port receives a Message with the Port Data Role
	 * field set to the same Data Role as its current Data Role, except
	 * for the GoodCRC Message, USB Type-C Error Recovery actions as
	 * defined in [USB Type-C 2.0] Shall be performed."
	 *
	 * The spec lists no required state for this check, so centralize it by
	 * processing this requirement in the PRL RX. Because the TCPM does not
	 * swap data roles instantaneously, disable this check during the
	 * transition.
	 */
	if (!RCH_CHK_FLAG(port, PRL_FLAGS_IGNORE_DATA_ROLE) &&
	    PD_HEADER_GET_SOP(header) == TCPCI_MSG_SOP &&
	    PD_HEADER_DROLE(header) == pd_get_data_role(port)) {
		CPRINTS("C%d Error: Data role mismatch (0x%08x)", port, header);
		tc_start_error_recovery(port);
		return;
	}

	/* Handle incoming soft reset as special case */
	if (cnt == 0 && type == PD_CTRL_SOFT_RESET) {
		/* Clear MessageIdCounter and stored MessageID value. */
		prl_reset_msg_ids(port, prl_rx[port].sop);

		/* Soft Reset occurred */
		set_state_prl_tx(port, PRL_TX_PHY_LAYER_RESET);

		if (IS_ENABLED(CONFIG_USB_PD_EXTENDED_MESSAGES)) {
			set_state_rch(port,
				      RCH_WAIT_FOR_MESSAGE_FROM_PROTOCOL_LAYER);
			set_state_tch(port,
				      TCH_WAIT_FOR_MESSAGE_REQUEST_FROM_PE);
		}

		/*
		 * Inform Policy Engine of Soft Reset. Note perform this after
		 * performing the protocol layer reset, otherwise we will lose
		 * the PE's outgoing ACCEPT message to the soft reset.
		 */
		pe_got_soft_reset(port);

		return;
	}

	/*
	 * Ignore if this is a duplicate message. Stop processing.
	 */
	if (prl_rx[port].msg_id[prl_rx[port].sop] == msid)
		return;

	/*
	 * Discard any pending tx message if this is
	 * not a ping message (length must be checked to verify this is a
	 * control message, rather than data)
	 */
	if ((cnt > 0) || (type != PD_CTRL_PING)) {
		/*
		 * Note: Spec dictates that we always go into
		 * PRL_Tx_Discard_Message upon receivng a message.  However, due
		 * to our TCPC architecture we may be receiving a transmit
		 * complete at the same time as a response so only do this if a
		 * message is pending.
		 */
		if (prl_tx[port].xmit_status != TCPC_TX_COMPLETE_SUCCESS ||
		    PRL_TX_CHK_FLAG(port, PRL_FLAGS_MSG_XMIT))
			set_state_prl_tx(port, PRL_TX_DISCARD_MESSAGE);
	}

	/* Store Message Id */
	prl_rx[port].msg_id[prl_rx[port].sop] = msid;

	if (IS_ENABLED(CONFIG_USB_PD_EXTENDED_MESSAGES)) {
		/* RTR Chunked Message Router States. */
		/*
		 * Received Ping from Protocol Layer
		 */
		if (cnt == 0 && type == PD_CTRL_PING) {
			/* NOTE: RTR_PING State embedded here. */
			rx_emsg[port].len = 0;
			pe_message_received(port);
			return;
		}
		/*
		 * Message (not Ping) Received from
		 * Protocol Layer & Doing Tx Chunks
		 *
		 * Also, handle the case where a message has been
		 * queued for sending but a message is received before
		 * tch_wait_for_message_request_from_pe has been run
		 */
		else if (tch_get_state(port) !=
				 TCH_WAIT_FOR_MESSAGE_REQUEST_FROM_PE ||
			 TCH_CHK_FLAG(port, PRL_FLAGS_MSG_XMIT)) {
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
	} else {
		/* Copy chunk to extended buffer */
		copy_chunk_to_ext(port);
		/* Send message to Policy Engine */
		pe_message_received(port);
	}

	task_wake(PD_PORT_TO_TASK_ID(port));
}

/* All necessary Protocol Transmit States (Section 6.11.2.2) */
static __const_data const struct usb_state prl_tx_states[] = {
	[PRL_TX_PHY_LAYER_RESET] = {
		.entry  = prl_tx_phy_layer_reset_entry,
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
#ifdef CONFIG_USB_PD_REV30
	[PRL_TX_SRC_SOURCE_TX] = {
		.entry  = prl_tx_src_source_tx_entry,
		.run    = prl_tx_src_source_tx_run,
	},
	[PRL_TX_SNK_START_AMS] = {
		.entry  = prl_tx_snk_start_ams_entry,
		.run    = prl_tx_snk_start_ams_run,
	},
#endif /* CONFIG_USB_PD_REV30 */
	[PRL_TX_SRC_PENDING] = {
		.entry  = prl_tx_src_pending_entry,
		.run    = prl_tx_src_pending_run,
		.exit	= prl_tx_src_pending_exit,
	},
	[PRL_TX_SNK_PENDING] = {
		.entry  = prl_tx_snk_pending_entry,
		.run    = prl_tx_snk_pending_run,
	},
	[PRL_TX_DISCARD_MESSAGE] = {
		.entry  = prl_tx_discard_message_entry,
	},
};

/* All necessary Protocol Hard Reset States (Section 6.11.2.4) */
static __const_data const struct usb_state prl_hr_states[] = {
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
		.exit	= prl_hr_wait_for_phy_hard_reset_complete_exit,
	},
	[PRL_HR_WAIT_FOR_PE_HARD_RESET_COMPLETE] = {
		.entry  = prl_hr_wait_for_pe_hard_reset_complete_entry,
		.run    = prl_hr_wait_for_pe_hard_reset_complete_run,
		.exit   = prl_hr_wait_for_pe_hard_reset_complete_exit,
	},
};

/* All necessary Chunked Rx states (Section 6.11.2.1.2) */
__maybe_unused static const struct usb_state rch_states[] = {
#ifdef CONFIG_USB_PD_EXTENDED_MESSAGES
	[RCH_WAIT_FOR_MESSAGE_FROM_PROTOCOL_LAYER] = {
		.entry  = rch_wait_for_message_from_protocol_layer_entry,
		.run    = rch_wait_for_message_from_protocol_layer_run,
	},
	[RCH_PASS_UP_MESSAGE] = {
		.entry  = rch_pass_up_message_entry,
	},
	[RCH_PROCESSING_EXTENDED_MESSAGE] = {
		.entry  = rch_processing_extended_message_entry,
		.run    = rch_processing_extended_message_run,
	},
	[RCH_REQUESTING_CHUNK] = {
		.entry  = rch_requesting_chunk_entry,
		.run    = rch_requesting_chunk_run,
	},
	[RCH_WAITING_CHUNK] = {
		.entry  = rch_waiting_chunk_entry,
		.run    = rch_waiting_chunk_run,
		.exit	= rch_waiting_chunk_exit,
	},
	[RCH_REPORT_ERROR] = {
		.entry  = rch_report_error_entry,
		.run    = rch_report_error_run,
	},
#endif /* CONFIG_USB_PD_EXTENDED_MESSAGES */
};

/* All necessary Chunked Tx states (Section 6.11.2.1.3) */
__maybe_unused static const struct usb_state tch_states[] = {
#ifdef CONFIG_USB_PD_EXTENDED_MESSAGES
	[TCH_WAIT_FOR_MESSAGE_REQUEST_FROM_PE] = {
		.entry  = tch_wait_for_message_request_from_pe_entry,
		.run    = tch_wait_for_message_request_from_pe_run,
	},
	[TCH_WAIT_FOR_TRANSMISSION_COMPLETE] = {
		.entry  = tch_wait_for_transmission_complete_entry,
		.run    = tch_wait_for_transmission_complete_run,
	},
	[TCH_CONSTRUCT_CHUNKED_MESSAGE] = {
		.entry  = tch_construct_chunked_message_entry,
		.run    = tch_construct_chunked_message_run,
	},
	[TCH_SENDING_CHUNKED_MESSAGE] = {
		.entry  = tch_sending_chunked_message_entry,
		.run    = tch_sending_chunked_message_run,
	},
	[TCH_WAIT_CHUNK_REQUEST] = {
		.entry  = tch_wait_chunk_request_entry,
		.run    = tch_wait_chunk_request_run,
		.exit	= tch_wait_chunk_request_exit,
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
#endif /* CONFIG_USB_PD_EXTENDED_MESSAGES */
};

#ifdef CONFIG_USB_PD_PRL_EVENT_LOG
struct prl_event_log_entry {
	/*
	 * Truncating to 32 bits still covers a time range of about an hour and
	 * saves 4 bytes of RAM per entry, which is significant.
	 */
	uint32_t timestamp;
	uint16_t flags;
	uint8_t port;
	enum prl_event_log_state_kind kind;
	union {
		enum usb_prl_tx_state tx;
		enum usb_prl_hr_state hr;
		enum usb_rch_state rch;
		enum usb_tch_state tch;
	};
};
BUILD_ASSERT(CONFIG_USB_PD_PORT_MAX_COUNT <= UINT8_MAX);

static struct prl_event_log_entry
	prl_event_log_buffer[CONFIG_USB_PD_PRL_EVENT_LOG_CAPACITY];
static atomic_t prl_event_log_next;

static void prl_event_log_append(enum prl_event_log_state_kind kind, int port)
{
	struct prl_event_log_entry entry = {
		.timestamp = get_time().val,
		.port = port,
		.kind = kind,
	};

	switch (kind) {
	case PRL_EVENT_LOG_STATE_TX:
		entry.flags = prl_tx[port].flags;
		entry.tx = prl_tx_get_state(port);
		break;
	case PRL_EVENT_LOG_STATE_HR:
		entry.flags = prl_hr[port].flags;
		entry.hr = prl_hr_get_state(port);
		break;
	case PRL_EVENT_LOG_STATE_RCH:
		entry.flags = rch[port].flags;
		entry.rch = rch_get_state(port);
		break;
	case PRL_EVENT_LOG_STATE_TCH:
		entry.flags = tch[port].flags;
		entry.tch = tch_get_state(port);
		break;
	case PRL_EVENT_LOG_STATE_NONE:
		/* Should never be written to the log */
		return;
	}

	prl_event_log_buffer[atomic_add(&prl_event_log_next, 1) %
			     ARRAY_SIZE(prl_event_log_buffer)] = entry;
}

static int command_prllog(int argc, const char **argv)
{
	if (argc == 2 && strcmp("clear", argv[1]) == 0) {
		/* Clear buffer contents */
		for (int i = 0; i < ARRAY_SIZE(prl_event_log_buffer); i++) {
			memset(prl_event_log_buffer, 0,
			       sizeof(prl_event_log_buffer));
		}
		return EC_SUCCESS;
	} else if (argc != 1) {
		return EC_ERROR_PARAM1;
	}

	/* Locate the oldest entry in the buffer, to start output there. */
	unsigned int oldest_index = INT_MAX;

	for (int i = 0; i < ARRAY_SIZE(prl_event_log_buffer); i++) {
		const struct prl_event_log_entry *entry =
			&prl_event_log_buffer[i];
		if (entry->kind == PRL_EVENT_LOG_STATE_NONE) {
			continue;
		}

		if (oldest_index == INT_MAX) {
			oldest_index = i;
		} else if (entry->timestamp <
			   prl_event_log_buffer[oldest_index].timestamp) {
			oldest_index = i;
			/*
			 * Timestamps increase monotonically, so any decrease in
			 * time indicates we rolled over to old entries and any
			 * after this will always be newer.
			 */
			break;
		}
	}
	if (oldest_index == INT_MAX) {
		/* No valid entries in the buffer. */
		return EC_SUCCESS;
	}

	/* Dump buffer contents */
	for (unsigned int i = 0; i < ARRAY_SIZE(prl_event_log_buffer); i++) {
		const struct prl_event_log_entry *entry =
			&prl_event_log_buffer[(oldest_index + i) %
					      ARRAY_SIZE(prl_event_log_buffer)];

		if (entry->kind == PRL_EVENT_LOG_STATE_NONE) {
			/*
			 * oldest_index refers to the oldest non-empty entry, so
			 * if we find an empty one later we know the buffer is
			 * empty past that point.
			 */
			break;
		}

		CPRINTF("%" PRIu32 " C%d ", entry->timestamp, entry->port);
		switch (entry->kind) {
		case PRL_EVENT_LOG_STATE_TX:
			CPRINTF("%s ", prl_tx_state_names[entry->tx]);
			print_flag("PRL_TX", 1, entry->flags);
			break;
		case PRL_EVENT_LOG_STATE_HR:
			CPRINTF("%s ", prl_hr_state_names[entry->hr]);
			print_flag("PRL_HR", 1, entry->flags);
			break;
		case PRL_EVENT_LOG_STATE_RCH:
			CPRINTF("%s ", rch_state_names[entry->rch]);
			print_flag("RCH", 1, entry->flags);
			break;
		case PRL_EVENT_LOG_STATE_TCH:
			CPRINTF("%s ", tch_state_names[entry->tch]);
			print_flag("TCH", 1, entry->flags);
			break;
		default:
			CPRINTF("unrecognized event kind\n");
			continue;
		}
	}
	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(prllog, command_prllog, "[clear]",
			"Dump USB-PD PRL state log");
#else
__maybe_unused static void
prl_event_log_append(enum prl_event_log_state_kind kind, int port)
{
}
#endif

#ifdef TEST_BUILD

const struct test_sm_data test_prl_sm_data[] = {
	{
		.base = prl_tx_states,
		.size = ARRAY_SIZE(prl_tx_states),
		.names = prl_tx_state_names,
		.names_size = ARRAY_SIZE(prl_tx_state_names),
	},
	{
		.base = prl_hr_states,
		.size = ARRAY_SIZE(prl_hr_states),
		.names = prl_hr_state_names,
		.names_size = ARRAY_SIZE(prl_hr_state_names),
	},
#ifdef CONFIG_USB_PD_EXTENDED_MESSAGES
	{
		.base = rch_states,
		.size = ARRAY_SIZE(rch_states),
		.names = rch_state_names,
		.names_size = ARRAY_SIZE(rch_state_names),
	},
	{
		.base = tch_states,
		.size = ARRAY_SIZE(tch_states),
		.names = tch_state_names,
		.names_size = ARRAY_SIZE(tch_state_names),
	},
#endif /* CONFIG_USB_PD_EXTENDED_MESSAGES */
};
BUILD_ASSERT(ARRAY_SIZE(prl_tx_states) == ARRAY_SIZE(prl_tx_state_names));
BUILD_ASSERT(ARRAY_SIZE(prl_hr_states) == ARRAY_SIZE(prl_hr_state_names));
#ifdef CONFIG_USB_PD_EXTENDED_MESSAGES
BUILD_ASSERT(ARRAY_SIZE(rch_states) == ARRAY_SIZE(rch_state_names));
BUILD_ASSERT(ARRAY_SIZE(tch_states) == ARRAY_SIZE(tch_state_names));
#endif /* CONFIG_USB_PD_EXTENDED_MESSAGES */
const int test_prl_sm_data_size = ARRAY_SIZE(test_prl_sm_data);
#endif
