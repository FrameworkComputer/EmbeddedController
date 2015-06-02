/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "battery.h"
#include "board.h"
#include "case_closed_debug.h"
#include "charge_manager.h"
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
#include "util.h"
#include "usb_pd.h"
#include "usb_pd_tcpm.h"
#include "version.h"

#ifdef CONFIG_COMMON_RUNTIME
#define CPRINTF(format, args...) cprintf(CC_USBPD, format, ## args)
#define CPRINTS(format, args...) cprints(CC_USBPD, format, ## args)

/*
 * Debug log level - higher number == more log
 *   Level 0: Log state transitions
 *   Level 1: Level 0, plus packet info
 *   Level 2: Level 1, plus ping packet and packet dump on error
 *
 * Note that higher log level causes timing changes and thus may affect
 * performance.
 */
static int debug_level;

/*
 * PD communication enabled flag. When false, PD state machine still
 * detects source/sink connection and disconnection, and will still
 * provide VBUS, but never sends any PD communication.
 */
static uint8_t pd_comm_enabled = CONFIG_USB_PD_COMM_ENABLED;
#else
#define CPRINTF(format, args...)
static const int debug_level;
static const uint8_t pd_comm_enabled = 1;
#endif

#ifdef CONFIG_USB_PD_DUAL_ROLE
#define DUAL_ROLE_IF_ELSE(port, sink_clause, src_clause) \
	(pd[port].power_role == PD_ROLE_SINK ? (sink_clause) : (src_clause))
#else
#define DUAL_ROLE_IF_ELSE(port, sink_clause, src_clause) (src_clause)
#endif

#define READY_RETURN_STATE(port) DUAL_ROLE_IF_ELSE(port, PD_STATE_SNK_READY, \
							 PD_STATE_SRC_READY)

/* Type C supply voltage (mV) */
#define TYPE_C_VOLTAGE	5000 /* mV */

/* PD counter definitions */
#define PD_MESSAGE_ID_COUNT 7
#define PD_HARD_RESET_COUNT 2
#define PD_CAPS_COUNT 50

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
enum pd_dual_role_states drp_state = PD_DRP_TOGGLE_OFF;

/* Last received source cap */
static uint32_t pd_src_caps[CONFIG_USB_PD_PORT_COUNT][PDO_MAX_OBJECTS];
static int pd_src_cap_cnt[CONFIG_USB_PD_PORT_COUNT];
#endif

static struct pd_protocol {
	/* current port power role (SOURCE or SINK) */
	uint8_t power_role;
	/* current port data role (DFP or UFP) */
	uint8_t data_role;
	/* port flags, see PD_FLAGS_* */
	uint16_t flags;
	/* 3-bit rolling message ID counter */
	uint8_t msg_id;
	/* Port polarity : 0 => CC1 is CC line, 1 => CC2 is CC line */
	uint8_t polarity;
	/* PD state for port */
	enum pd_states task_state;
	/* PD state when we run state handler the last time */
	enum pd_states last_state;
	/* The state to go to after timeout */
	enum pd_states timeout_state;
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
#endif

	/* PD state for Vendor Defined Messages */
	enum vdm_states vdm_state;
	/* Timeout for the current vdm state.  Set to 0 for no timeout. */
	timestamp_t vdm_timeout;
	/* next Vendor Defined Message to send */
	uint32_t vdo_data[VDO_MAX_SIZE];
	uint8_t vdo_count;
	/* VDO to retry if UFP responder replied busy. */
	uint32_t vdo_retry;

	/* Attached ChromeOS device id, RW hash, and current RO / RW image */
	uint16_t dev_id;
	uint32_t dev_rw_hash[PD_RW_HASH_SIZE/4];
	enum ec_current_image current_image;
} pd[CONFIG_USB_PD_PORT_COUNT];

#ifdef CONFIG_COMMON_RUNTIME
static const char * const pd_state_names[] = {
	"DISABLED", "SUSPENDED",
#ifdef CONFIG_USB_PD_DUAL_ROLE
	"SNK_DISCONNECTED", "SNK_DISCONNECTED_DEBOUNCE",
	"SNK_HARD_RESET_RECOVER",
	"SNK_DISCOVERY", "SNK_REQUESTED", "SNK_TRANSITION", "SNK_READY",
	"SNK_SWAP_INIT", "SNK_SWAP_SNK_DISABLE",
	"SNK_SWAP_SRC_DISABLE", "SNK_SWAP_STANDBY", "SNK_SWAP_COMPLETE",
#endif /* CONFIG_USB_PD_DUAL_ROLE */
	"SRC_DISCONNECTED", "SRC_DISCONNECTED_DEBOUNCE", "SRC_ACCESSORY",
	"SRC_HARD_RESET_RECOVER", "SRC_STARTUP",
	"SRC_DISCOVERY", "SRC_NEGOCIATE", "SRC_ACCEPTED", "SRC_POWERED",
	"SRC_TRANSITION", "SRC_READY", "SRC_GET_SNK_CAP", "DR_SWAP",
#ifdef CONFIG_USB_PD_DUAL_ROLE
	"SRC_SWAP_INIT", "SRC_SWAP_SNK_DISABLE", "SRC_SWAP_SRC_DISABLE",
	"SRC_SWAP_STANDBY",
#ifdef CONFIG_USBC_VCONN_SWAP
	"VCONN_SWAP_SEND", "VCONN_SWAP_INIT", "VCONN_SWAP_READY",
#endif /* CONFIG_USBC_VCONN_SWAP */
#endif /* CONFIG_USB_PD_DUAL_ROLE */
	"SOFT_RESET", "HARD_RESET_SEND", "HARD_RESET_EXECUTE", "BIST_RX",
	"BIST_TX",
};
BUILD_ASSERT(ARRAY_SIZE(pd_state_names) == PD_STATE_COUNT);
#endif

/*
 * 4 entry rw_hash table of type-C devices that AP has firmware updates for.
 */
#ifdef CONFIG_COMMON_RUNTIME
#define RW_HASH_ENTRIES 4
static struct ec_params_usb_pd_rw_hash_entry rw_hash_table[RW_HASH_ENTRIES];
#endif

static inline void set_state_timeout(int port,
				     uint64_t timeout,
				     enum pd_states timeout_state)
{
	pd[port].timeout = timeout;
	pd[port].timeout_state = timeout_state;
}

/* Return flag for pd state is connected */
int pd_is_connected(int port)
{
	if (pd[port].task_state == PD_STATE_DISABLED)
		return 0;

	return DUAL_ROLE_IF_ELSE(port,
		/* sink */
		pd[port].task_state != PD_STATE_SNK_DISCONNECTED &&
		pd[port].task_state != PD_STATE_SNK_DISCONNECTED_DEBOUNCE,
		/* source */
		pd[port].task_state != PD_STATE_SRC_DISCONNECTED &&
		pd[port].task_state != PD_STATE_SRC_DISCONNECTED_DEBOUNCE &&
		pd[port].task_state != PD_STATE_SRC_ACCESSORY);
}

static inline void set_state(int port, enum pd_states next_state)
{
	enum pd_states last_state = pd[port].task_state;
#ifdef CONFIG_LOW_POWER_IDLE
	int i;
#endif

	set_state_timeout(port, 0, 0);
	pd[port].task_state = next_state;

	if (last_state == next_state)
		return;
#ifdef CONFIG_USB_PD_DUAL_ROLE
	/* Ignore dual-role toggling between sink and source */
	if ((last_state == PD_STATE_SNK_DISCONNECTED &&
	     next_state == PD_STATE_SRC_DISCONNECTED) ||
	    (last_state == PD_STATE_SRC_DISCONNECTED &&
	     next_state == PD_STATE_SNK_DISCONNECTED))
		return;
#endif

#ifdef CONFIG_USB_PD_DUAL_ROLE
	if (next_state == PD_STATE_SRC_DISCONNECTED ||
	    next_state == PD_STATE_SNK_DISCONNECTED) {
		/* Clear the input current limit */
		pd_set_input_current_limit(port, 0, 0);
#ifdef CONFIG_CHARGE_MANAGER
		typec_set_input_current_limit(port, 0, 0);
		charge_manager_set_ceil(port, CHARGE_CEIL_NONE);
#endif
#else /* CONFIG_USB_PD_DUAL_ROLE */
	if (next_state == PD_STATE_SRC_DISCONNECTED) {
#endif
		pd[port].dev_id = 0;
		pd[port].flags &= ~PD_FLAGS_RESET_ON_DISCONNECT_MASK;
#ifdef CONFIG_CHARGE_MANAGER
		charge_manager_update_dualrole(port, CAP_UNKNOWN);
#endif
#ifdef CONFIG_USB_PD_ALT_MODE_DFP
		pd_dfp_exit_mode(port, 0, 0);
#endif
#ifdef CONFIG_USBC_SS_MUX
		board_set_usb_mux(port, TYPEC_MUX_NONE, USB_SWITCH_DISCONNECT,
				  pd[port].polarity);
#endif
#ifdef CONFIG_USBC_VCONN
		tcpm_set_vconn(port, 0);
#endif
		/* Disable TCPC RX */
		tcpm_set_rx_enable(port, 0);
	}

#ifdef CONFIG_LOW_POWER_IDLE
	/* If any PD port is connected, then disable deep sleep */
	for (i = 0; i < CONFIG_USB_PD_PORT_COUNT; i++) {
		if (pd_is_connected(i))
			break;
	}
	if (i == CONFIG_USB_PD_PORT_COUNT)
		enable_sleep(SLEEP_MASK_USB_PD);
	else
		disable_sleep(SLEEP_MASK_USB_PD);
#endif

	CPRINTF("C%d st%d\n", port, next_state);
}

/* increment message ID counter */
static void inc_id(int port)
{
	pd[port].msg_id = (pd[port].msg_id + 1) & PD_MESSAGE_ID_COUNT;
}

static void pd_transmit_complete(int port, int status)
{
	if (status & TCPC_REG_ALERT1_TX_SUCCESS)
		inc_id(port);

	pd[port].tx_status = status;
	task_set_event(PD_PORT_TO_TASK_ID(port), PD_EVENT_TX, 0);
}

static int pd_transmit(int port, enum tcpm_transmit_type type,
		       uint16_t header, const uint32_t *data)
{
	int evt;

	/* If comms are disabled, do not transmit, return error */
	if (!pd_comm_enabled)
		return -1;

	tcpm_transmit(port, type, header, data);

	/* Wait until TX is complete */
	do {
		evt = task_wait_event(PD_T_TCPC_TX_TIMEOUT);
	} while (!(evt & (TASK_EVENT_TIMER | PD_EVENT_TX)));

	if (evt & TASK_EVENT_TIMER)
		return -1;

	/* TODO: give different error condition for failed vs discarded */
	return pd[port].tx_status & TCPC_REG_ALERT1_TX_SUCCESS ? 1 : -1;
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
			pd[port].data_role, pd[port].msg_id, 0);

	bit_len = pd_transmit(port, TRANSMIT_SOP, header, NULL);

	if (debug_level >= 1)
		CPRINTF("CTRL[%d]>%d\n", type, bit_len);

	return bit_len;
}

static int send_source_cap(int port)
{
	int bit_len;
#ifdef CONFIG_USB_PD_DYNAMIC_SRC_CAP
	const uint32_t *src_pdo;
	const int src_pdo_cnt = pd_get_source_pdo(&src_pdo);
#else
	const uint32_t *src_pdo = pd_src_pdo;
	const int src_pdo_cnt = pd_src_pdo_cnt;
#endif
	uint16_t header;

	if (src_pdo_cnt == 0)
		/* No source capabilities defined, sink only */
		header = PD_HEADER(PD_CTRL_REJECT, pd[port].power_role,
			pd[port].data_role, pd[port].msg_id, 0);
	else
		header = PD_HEADER(PD_DATA_SOURCE_CAP, pd[port].power_role,
			pd[port].data_role, pd[port].msg_id, src_pdo_cnt);

	bit_len = pd_transmit(port, TRANSMIT_SOP, header, src_pdo);
	if (debug_level >= 1)
		CPRINTF("srcCAP>%d\n", bit_len);

	return bit_len;
}

#ifdef CONFIG_USB_PD_DUAL_ROLE
static void send_sink_cap(int port)
{
	int bit_len;
	uint16_t header = PD_HEADER(PD_DATA_SINK_CAP, pd[port].power_role,
			pd[port].data_role, pd[port].msg_id, pd_snk_pdo_cnt);

	bit_len = pd_transmit(port, TRANSMIT_SOP, header, pd_snk_pdo);
	if (debug_level >= 1)
		CPRINTF("snkCAP>%d\n", bit_len);
}

static int send_request(int port, uint32_t rdo)
{
	int bit_len;
	uint16_t header = PD_HEADER(PD_DATA_REQUEST, pd[port].power_role,
			pd[port].data_role, pd[port].msg_id, 1);

	bit_len = pd_transmit(port, TRANSMIT_SOP, header, &rdo);
	if (debug_level >= 1)
		CPRINTF("REQ%d>\n", bit_len);

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
			pd[port].data_role, pd[port].msg_id, 1);

	bit_len = pd_transmit(port, TRANSMIT_SOP, header, &bdo);
	CPRINTF("BIST>%d\n", bit_len);

	return bit_len;
}
#endif

static void queue_vdm(int port, uint32_t *header, const uint32_t *data,
			     int data_cnt)
{
	pd[port].vdo_count = data_cnt + 1;
	pd[port].vdo_data[0] = header[0];
	memcpy(&pd[port].vdo_data[1], data, sizeof(uint32_t) * data_cnt);
	/* Set ready, pd task will actually send */
	pd[port].vdm_state = VDM_STATE_READY;
}

static void handle_vdm_request(int port, int cnt, uint32_t *payload)
{
	int rlen = 0;
	uint32_t *rdata;

	if (pd[port].vdm_state == VDM_STATE_BUSY) {
		/* If UFP responded busy retry after timeout */
		if (PD_VDO_CMDT(payload[0]) == CMDT_RSP_BUSY) {
			pd[port].vdm_timeout.val = get_time().val +
				PD_T_VDM_BUSY;
			pd[port].vdm_state = VDM_STATE_WAIT_RSP_BUSY;
			pd[port].vdo_retry = (payload[0] & ~VDO_CMDT_MASK) |
				CMDT_INIT;
			return;
		} else {
			pd[port].vdm_state = VDM_STATE_DONE;
		}
	}

	if (PD_VDO_SVDM(payload[0]))
		rlen = pd_svdm(port, cnt, payload, &rdata);
	else
		rlen = pd_custom_vdm(port, cnt, payload, &rdata);

	if (rlen > 0) {
		queue_vdm(port, rdata, &rdata[1], rlen - 1);
		return;
	}
	if (debug_level >= 1)
		CPRINTF("Unhandled VDM VID %04x CMD %04x\n",
			PD_VDO_VID(payload[0]), payload[0] & 0xFFFF);
}

static void execute_hard_reset(int port)
{
	if (pd[port].last_state == PD_STATE_HARD_RESET_SEND)
		CPRINTF("C%d HARD RST TX\n", port);
	else
		CPRINTF("C%d HARD RST RX\n", port);

	pd[port].msg_id = 0;
#ifdef CONFIG_USB_PD_ALT_MODE_DFP
	pd_dfp_exit_mode(port, 0, 0);
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
		/* Clear the input current limit */
		pd_set_input_current_limit(port, 0, 0);
#ifdef CONFIG_CHARGE_MANAGER
		charge_manager_set_ceil(port, CHARGE_CEIL_NONE);
#endif /* CONFIG_CHARGE_MANAGER */

		set_state(port, PD_STATE_SNK_HARD_RESET_RECOVER);
		return;
	}
#endif /* CONFIG_USB_PD_DUAL_ROLE */

	/* We are a source, cut power */
	pd_power_supply_reset(port);
	pd[port].src_recover = get_time().val + PD_T_SRC_RECOVER;
	set_state(port, PD_STATE_SRC_HARD_RESET_RECOVER);
}

static void execute_soft_reset(int port)
{
	pd[port].msg_id = 0;
	set_state(port, DUAL_ROLE_IF_ELSE(port, PD_STATE_SNK_DISCOVERY,
						PD_STATE_SRC_DISCOVERY));
#ifdef CONFIG_COMMON_RUNTIME
	/* if flag to disable PD comms after soft reset, then disable comms */
	if (pd[port].flags & PD_FLAGS_SFT_RST_DIS_COMM)
		pd_comm_enable(0);
#endif

	CPRINTF("C%d Soft Rst\n", port);
}

void pd_soft_reset(void)
{
	int i;

	for (i = 0; i < CONFIG_USB_PD_PORT_COUNT; ++i)
		if (pd_is_connected(i)) {
			set_state(i, PD_STATE_SOFT_RESET);
			task_wake(PD_PORT_TO_TASK_ID(i));
		}
}

void pd_prepare_sysjump(void)
{
	int i;

	/*
	 * On sysjump, we are most definitely going to drop pings (if any)
	 * and lose all of our PD state. Instead of trying to remember all
	 * the states and deal with on-going transmission, let's send soft
	 * reset here and then disable PD communication until after sysjump
	 * is complete so that the communication starts over without dropping
	 * power.
	 */
	for (i = 0; i < CONFIG_USB_PD_PORT_COUNT; ++i)
		if (pd_is_connected(i))
			pd[i].flags |= PD_FLAGS_SFT_RST_DIS_COMM;

	pd_soft_reset();
}

#ifdef CONFIG_USB_PD_DUAL_ROLE
static void pd_store_src_cap(int port, int cnt, uint32_t *src_caps)
{
	int i;

	pd_src_cap_cnt[port] = cnt;
	for (i = 0; i < cnt; i++)
		pd_src_caps[port][i] = *src_caps++;
}

static void pd_send_request_msg(int port, int always_send_request)
{
	uint32_t rdo, curr_limit, supply_voltage;
	int res;

#ifdef CONFIG_CHARGE_MANAGER
	int charging = (charge_manager_get_active_charge_port() == port);
#else
	const int charging = 1;
#endif

#ifdef CONFIG_USB_PD_CHECK_MAX_REQUEST_ALLOWED
	int max_request_allowed = pd_is_max_request_allowed();
#else
	const int max_request_allowed = 1;
#endif

	/* Clear new power request */
	pd[port].new_power_request = 0;

	/* Build and send request RDO */
	/*
	 * If this port is not actively charging or we are not allowed to
	 * request the max voltage, then select vSafe5V
	 */
	res = pd_build_request(pd_src_cap_cnt[port], pd_src_caps[port],
			       &rdo, &curr_limit, &supply_voltage,
			       charging && max_request_allowed ?
					PD_REQUEST_MAX : PD_REQUEST_VSAFE5V);

	if (res != EC_SUCCESS)
		/*
		 * If fail to choose voltage, do nothing, let source re-send
		 * source cap
		 */
		return;

	/* Don't re-request the same voltage */
	if (!always_send_request && pd[port].prev_request_mv == supply_voltage)
		return;

	CPRINTF("Req C%d [%d] %dmV %dmA", port, RDO_POS(rdo),
		supply_voltage, curr_limit);
	if (rdo & RDO_CAP_MISMATCH)
		CPRINTF(" Mismatch");
	CPRINTF("\n");

	pd[port].curr_limit = curr_limit;
	pd[port].supply_voltage = supply_voltage;
	pd[port].prev_request_mv = supply_voltage;
	res = send_request(port, rdo);
	if (res >= 0)
		set_state(port, PD_STATE_SNK_REQUESTED);
	/* If fail send request, do nothing, let source re-send source cap */
}
#endif

static void pd_update_pdo_flags(int port, uint32_t pdo)
{
#ifdef CONFIG_CHARGE_MANAGER
#ifdef CONFIG_USB_PD_ALT_MODE_DFP
	int charge_whitelisted =
		(pd[port].power_role == PD_ROLE_SINK &&
		 pd_charge_from_device(pd_get_identity_vid(port),
				       pd_get_identity_pid(port)));
#else
	const int charge_whitelisted = 0;
#endif
#endif

	/* can only parse PDO flags if type is fixed */
	if ((pdo & PDO_TYPE_MASK) != PDO_TYPE_FIXED)
		return;

#ifdef CONFIG_USB_PD_DUAL_ROLE
	if (pdo & PDO_FIXED_DUAL_ROLE)
		pd[port].flags |= PD_FLAGS_PARTNER_DR_POWER;
	else
		pd[port].flags &= ~PD_FLAGS_PARTNER_DR_POWER;

	if (pdo & PDO_FIXED_EXTERNAL)
		pd[port].flags |= PD_FLAGS_PARTNER_EXTPOWER;
	else
		pd[port].flags &= ~PD_FLAGS_PARTNER_EXTPOWER;
#endif

	if (pdo & PDO_FIXED_DATA_SWAP)
		pd[port].flags |= PD_FLAGS_PARTNER_DR_DATA;
	else
		pd[port].flags &= ~PD_FLAGS_PARTNER_DR_DATA;

#ifdef CONFIG_CHARGE_MANAGER
	/*
	 * Treat device as a dedicated charger (meaning we should charge
	 * from it) if it does not support power swap, or if it is externally
	 * powered, or if we are a sink and the device identity matches a
	 * charging white-list.
	 */
	if (!(pd[port].flags & PD_FLAGS_PARTNER_DR_POWER) ||
	    (pd[port].flags & PD_FLAGS_PARTNER_EXTPOWER) ||
	    charge_whitelisted)
		charge_manager_update_dualrole(port, CAP_DEDICATED);
	else
		charge_manager_update_dualrole(port, CAP_DUALROLE);
#endif
}

static void handle_data_request(int port, uint16_t head,
		uint32_t *payload)
{
	int type = PD_HEADER_TYPE(head);
	int cnt = PD_HEADER_CNT(head);

	switch (type) {
#ifdef CONFIG_USB_PD_DUAL_ROLE
	case PD_DATA_SOURCE_CAP:
		if ((pd[port].task_state == PD_STATE_SNK_DISCOVERY)
			|| (pd[port].task_state == PD_STATE_SNK_TRANSITION)
#ifdef CONFIG_USB_PD_NO_VBUS_DETECT
			|| (pd[port].task_state ==
			    PD_STATE_SNK_HARD_RESET_RECOVER)
#endif
			|| (pd[port].task_state == PD_STATE_SNK_READY)) {
			/* Port partner is now known to be PD capable */
			pd[port].flags |= PD_FLAGS_PREVIOUS_PD_CONN;

			pd_store_src_cap(port, cnt, payload);
			/* src cap 0 should be fixed PDO */
			pd_update_pdo_flags(port, payload[0]);

			pd_process_source_cap(port, pd_src_cap_cnt[port],
					      pd_src_caps[port]);
			pd_send_request_msg(port, 1);
		}
		break;
#endif /* CONFIG_USB_PD_DUAL_ROLE */
	case PD_DATA_REQUEST:
		if ((pd[port].power_role == PD_ROLE_SOURCE) && (cnt == 1))
			if (!pd_check_requested_voltage(payload[0])) {
				if (send_control(port, PD_CTRL_ACCEPT) < 0)
					/*
					 * if we fail to send accept, do
					 * nothing and let sink timeout and
					 * send hard reset
					 */
					return;

				/* explicit contract is now in place */
				pd[port].flags |= PD_FLAGS_EXPLICIT_CONTRACT;
				pd[port].requested_idx = payload[0] >> 28;
				set_state(port, PD_STATE_SRC_ACCEPTED);
				return;
			}
		/* the message was incorrect or cannot be satisfied */
		send_control(port, PD_CTRL_REJECT);
		/* keep last contract in place (whether implicit or explicit) */
		set_state(port, PD_STATE_SRC_READY);
		break;
	case PD_DATA_BIST:
		/* If not in READY state, then don't start BIST */
		if (DUAL_ROLE_IF_ELSE(port,
				pd[port].task_state == PD_STATE_SNK_READY,
				pd[port].task_state == PD_STATE_SRC_READY)) {
			/* currently only support sending bist carrier mode 2 */
			if ((payload[0] >> 28) == 5) {
				/* bist data object mode is 2 */
				pd_transmit(port, TRANSMIT_BIST_MODE_2, 0,
					    NULL);
				/* Set to appropriate port disconnected state */
				set_state(port, DUAL_ROLE_IF_ELSE(port,
						PD_STATE_SNK_DISCONNECTED,
						PD_STATE_SRC_DISCONNECTED));
			}
		}
		break;
	case PD_DATA_SINK_CAP:
		pd[port].flags |= PD_FLAGS_SNK_CAP_RECVD;
		/* snk cap 0 should be fixed PDO */
		pd_update_pdo_flags(port, payload[0]);
		if (pd[port].task_state == PD_STATE_SRC_GET_SINK_CAP)
			set_state(port, PD_STATE_SRC_READY);
		break;
	case PD_DATA_VENDOR_DEF:
		handle_vdm_request(port, cnt, payload);
		break;
	default:
		CPRINTF("Unhandled data message type %d\n", type);
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
#endif

void pd_request_data_swap(int port)
{
	if (DUAL_ROLE_IF_ELSE(port,
				pd[port].task_state == PD_STATE_SNK_READY,
				pd[port].task_state == PD_STATE_SRC_READY))
		set_state(port, PD_STATE_DR_SWAP);
	task_wake(PD_PORT_TO_TASK_ID(port));
}

static void pd_set_data_role(int port, int role)
{
	pd[port].data_role = role;
	pd_execute_data_swap(port, role);

#ifdef CONFIG_USBC_SS_MUX
#ifdef CONFIG_USBC_SS_MUX_DFP_ONLY
	/*
	 * Need to connect SS mux for if new data role is DFP.
	 * If new data role is UFP, then disconnect the SS mux.
	 */
	if (role == PD_ROLE_DFP)
		board_set_usb_mux(port, TYPEC_MUX_USB, USB_SWITCH_CONNECT,
				  pd[port].polarity);
	else
		board_set_usb_mux(port, TYPEC_MUX_NONE, USB_SWITCH_DISCONNECT,
				  pd[port].polarity);
#else
	board_set_usb_mux(port, TYPEC_MUX_USB, USB_SWITCH_CONNECT,
			  pd[port].polarity);
#endif
#endif
	pd_update_roles(port);
}

static void pd_dr_swap(int port)
{
	pd_set_data_role(port, !pd[port].data_role);
	pd[port].flags |= PD_FLAGS_DATA_SWAPPED;
}

static void handle_ctrl_request(int port, uint16_t head,
		uint32_t *payload)
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
		res = send_source_cap(port);
		if ((res >= 0) &&
		    (pd[port].task_state == PD_STATE_SRC_DISCOVERY))
			set_state(port, PD_STATE_SRC_NEGOCIATE);
		break;
	case PD_CTRL_GET_SINK_CAP:
#ifdef CONFIG_USB_PD_DUAL_ROLE
		send_sink_cap(port);
#else
		send_control(port, PD_CTRL_REJECT);
#endif
		break;
#ifdef CONFIG_USB_PD_DUAL_ROLE
	case PD_CTRL_GOTO_MIN:
		break;
	case PD_CTRL_PS_RDY:
		if (pd[port].task_state == PD_STATE_SNK_SWAP_SRC_DISABLE) {
			set_state(port, PD_STATE_SNK_SWAP_STANDBY);
		} else if (pd[port].task_state == PD_STATE_SRC_SWAP_STANDBY) {
			/* reset message ID and swap roles */
			pd[port].msg_id = 0;
			pd[port].power_role = PD_ROLE_SINK;
			pd_update_roles(port);
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
			set_state(port, PD_STATE_SNK_READY);
#ifdef CONFIG_CHARGE_MANAGER
			/* Set ceiling based on what's negotiated */
			charge_manager_set_ceil(port, pd[port].curr_limit);
#else
			pd_set_input_current_limit(port, pd[port].curr_limit,
						   pd[port].supply_voltage);
#endif
		}
		break;
#endif
	case PD_CTRL_REJECT:
	case PD_CTRL_WAIT:
		if (pd[port].task_state == PD_STATE_DR_SWAP)
			set_state(port, READY_RETURN_STATE(port));
#ifdef CONFIG_USBC_VCONN_SWAP
		else if (pd[port].task_state == PD_STATE_VCONN_SWAP_SEND)
			set_state(port, READY_RETURN_STATE(port));
#endif
#ifdef CONFIG_USB_PD_DUAL_ROLE
		else if (pd[port].task_state == PD_STATE_SRC_SWAP_INIT)
			set_state(port, PD_STATE_SRC_READY);
		else if (pd[port].task_state == PD_STATE_SNK_SWAP_INIT)
			set_state(port, PD_STATE_SNK_READY);
		else if (pd[port].task_state == PD_STATE_SNK_REQUESTED)
			/* no explicit contract */
			set_state(port, PD_STATE_SNK_READY);
#endif
		break;
	case PD_CTRL_ACCEPT:
		if (pd[port].task_state == PD_STATE_SOFT_RESET) {
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
			set_state(port, PD_STATE_SRC_SWAP_SNK_DISABLE);
		} else if (pd[port].task_state == PD_STATE_SNK_SWAP_INIT) {
			/* explicit contract goes away for power swap */
			pd[port].flags &= ~PD_FLAGS_EXPLICIT_CONTRACT;
			set_state(port, PD_STATE_SNK_SWAP_SNK_DISABLE);
		} else if (pd[port].task_state == PD_STATE_SNK_REQUESTED) {
			/* explicit contract is now in place */
			pd[port].flags |= PD_FLAGS_EXPLICIT_CONTRACT;
			set_state(port, PD_STATE_SNK_TRANSITION);
#endif
		}
		break;
	case PD_CTRL_SOFT_RESET:
		execute_soft_reset(port);
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
				  DUAL_ROLE_IF_ELSE(port,
					PD_STATE_SNK_SWAP_SNK_DISABLE,
					PD_STATE_SRC_SWAP_SNK_DISABLE));
		} else {
			send_control(port, PD_CTRL_REJECT);
		}
#else
		send_control(port, PD_CTRL_REJECT);
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
		send_control(port, PD_CTRL_REJECT);
#endif
		break;
	default:
		CPRINTF("Unhandled ctrl message type %d\n", type);
	}
}

static void handle_request(int port, uint16_t head,
		uint32_t *payload)
{
	int cnt = PD_HEADER_CNT(head);
	int p;

	/* dump received packet content (only dump ping at debug level 2) */
	if ((debug_level == 1 && PD_HEADER_TYPE(head) != PD_CTRL_PING) ||
	    debug_level >= 2) {
		CPRINTF("RECV %04x/%d ", head, cnt);
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

	if (cnt)
		handle_data_request(port, head, payload);
	else
		handle_ctrl_request(port, head, payload);
}

void pd_send_vdm(int port, uint32_t vid, int cmd, const uint32_t *data,
		 int count)
{
	if (count > VDO_MAX_SIZE - 1) {
		CPRINTF("VDM over max size\n");
		return;
	}

	/* set VDM header with VID & CMD */
	pd[port].vdo_data[0] = VDO(vid, ((vid & USB_SID_PD) == USB_SID_PD) ?
				   1 : (PD_VDO_CMD(cmd) < CMD_ATTENTION), cmd);
	queue_vdm(port, pd[port].vdo_data, data, count);

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
		return 500*MSEC;

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

		/* Prepare and send VDM */
		header = PD_HEADER(PD_DATA_VENDOR_DEF, pd[port].power_role,
				   pd[port].data_role, pd[port].msg_id,
				   (int)pd[port].vdo_count);
		res = pd_transmit(port, TRANSMIT_SOP, header,
				  pd[port].vdo_data);
		if (res < 0) {
			pd[port].vdm_state = VDM_STATE_ERR_SEND;
		} else {
			pd[port].vdm_state = VDM_STATE_BUSY;
			pd[port].vdm_timeout.val = get_time().val +
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
	if (debug_level >= 1)
		pd_dev_dump_info(dev_id, (uint8_t *)rw_hash);
#endif
	pd[port].current_image = current_image;

#ifdef CONFIG_COMMON_RUNTIME
	/* Search table for matching device / hash */
	for (i = 0; i < RW_HASH_ENTRIES; i++)
		if (dev_id == rw_hash_table[i].dev_id)
			return !memcmp(rw_hash,
				       rw_hash_table[i].dev_rw_hash,
				       PD_RW_HASH_SIZE);
#endif
	return 0;
}

#ifdef CONFIG_USB_PD_DUAL_ROLE
enum pd_dual_role_states pd_get_dual_role(void)
{
	return drp_state;
}

void pd_set_dual_role(enum pd_dual_role_states state)
{
	int i;
	drp_state = state;

	for (i = 0; i < CONFIG_USB_PD_PORT_COUNT; i++) {
		/*
		 * Change to sink if port is currently a source AND (new DRP
		 * state is force sink OR new DRP state is toggle off and we
		 * are in the source disconnected state).
		 */
		if (pd[i].power_role == PD_ROLE_SOURCE &&
		    (drp_state == PD_DRP_FORCE_SINK ||
		     (drp_state == PD_DRP_TOGGLE_OFF
		      && pd[i].task_state == PD_STATE_SRC_DISCONNECTED))) {
			pd[i].power_role = PD_ROLE_SINK;
			set_state(i, PD_STATE_SNK_DISCONNECTED);
			tcpm_set_cc(i, TYPEC_CC_RD);
			task_wake(PD_PORT_TO_TASK_ID(i));
		}

		/*
		 * Change to source if port is currently a sink and the
		 * new DRP state is force source.
		 */
		if (pd[i].power_role == PD_ROLE_SINK &&
		    drp_state == PD_DRP_FORCE_SOURCE) {
			pd[i].power_role = PD_ROLE_SOURCE;
			set_state(i, PD_STATE_SRC_DISCONNECTED);
			tcpm_set_cc(i, TYPEC_CC_RP);
			task_wake(PD_PORT_TO_TASK_ID(i));
		}
	}
}

int pd_get_role(int port)
{
	return pd[port].power_role;
}

static int pd_is_power_swapping(int port)
{
	/* return true if in the act of swapping power roles */
	return  pd[port].task_state == PD_STATE_SNK_SWAP_SNK_DISABLE ||
		pd[port].task_state == PD_STATE_SNK_SWAP_SRC_DISABLE ||
		pd[port].task_state == PD_STATE_SNK_SWAP_STANDBY ||
		pd[port].task_state == PD_STATE_SNK_SWAP_COMPLETE ||
		pd[port].task_state == PD_STATE_SRC_SWAP_SNK_DISABLE ||
		pd[port].task_state == PD_STATE_SRC_SWAP_SRC_DISABLE ||
		pd[port].task_state == PD_STATE_SRC_SWAP_STANDBY;
}

#endif /* CONFIG_USB_PD_DUAL_ROLE */

int pd_get_polarity(int port)
{
	return pd[port].polarity;
}

int pd_get_partner_data_swap_capable(int port)
{
	/* return data swap capable status of port partner */
	return pd[port].flags & PD_FLAGS_PARTNER_DR_DATA;
}

#ifdef CONFIG_COMMON_RUNTIME
void pd_comm_enable(int enable)
{
	int i;

	pd_comm_enabled = enable;

	for (i = 0; i < CONFIG_USB_PD_PORT_COUNT; i++) {
		/* If type-C connection, then update the TCPC RX enable */
		if (pd_is_connected(i))
			tcpm_set_rx_enable(i, enable);

#ifdef CONFIG_USB_PD_DUAL_ROLE
		/*
		 * If communications are enabled, start hard reset timer for
		 * any port in PD_SNK_DISCOVERY.
		 */
		if (enable && pd[i].task_state == PD_STATE_SNK_DISCOVERY)
			set_state_timeout(i,
					  get_time().val + PD_T_SINK_WAIT_CAP,
					  PD_STATE_HARD_RESET_SEND);
#endif
	}
}
#endif

void pd_ping_enable(int port, int enable)
{
	if (enable)
		pd[port].flags |= PD_FLAGS_PING_ENABLED;
	else
		pd[port].flags &= ~PD_FLAGS_PING_ENABLED;
}

#ifdef CONFIG_CHARGE_MANAGER
/**
 * Returns type C current limit (mA) based upon cc_voltage (mV).
 */
static inline int get_typec_current_limit(int cc)
{
	int charge;

	/* Detect type C charger current limit based upon vbus voltage. */
	if (cc == TYPEC_CC_VOLT_SNK_3_0)
		charge = 3000;
	else if (cc == TYPEC_CC_VOLT_SNK_1_5)
		charge = 1500;
	else
		charge = 0;

	return charge;
}

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

void pd_task(void)
{
	int head;
	int port = TASK_ID_TO_PD_PORT(task_get_current());
	uint32_t payload[7];
	int timeout = 10*MSEC;
	int cc1, cc2;
	int res, incoming_packet = 0;
	int hard_reset_count = 0;
#ifdef CONFIG_USB_PD_DUAL_ROLE
	uint64_t next_role_swap = PD_T_DRP_SNK;
#ifndef CONFIG_USB_PD_NO_VBUS_DETECT
	int snk_hard_reset_vbus_off = 0;
#endif
#ifdef CONFIG_CHARGE_MANAGER
	int typec_curr = 0, typec_curr_change = 0;
#endif /* CONFIG_CHARGE_MANAGER */
#endif /* CONFIG_USB_PD_DUAL_ROLE */
	enum pd_states this_state;
	enum pd_cc_states new_cc_state;
	timestamp_t now;
	int caps_count = 0, hard_reset_sent = 0;
	int evt;

	/* Ensure the power supply is in the default state */
	pd_power_supply_reset(port);

#ifdef CONFIG_USB_PD_TCPC
	/* Initialize port controller */
	tcpc_init(port);
#endif

	/* Disable TCPC RX until connection is established */
	tcpm_set_rx_enable(port, 0);

	/* Initialize PD protocol state variables for each port. */
	pd[port].power_role = PD_ROLE_DEFAULT;
	pd[port].vdm_state = VDM_STATE_DONE;
	pd[port].flags = 0;
	set_state(port, PD_DEFAULT_STATE);
	tcpm_set_cc(port, PD_ROLE_DEFAULT == PD_ROLE_SOURCE ? TYPEC_CC_RP :
							      TYPEC_CC_RD);

#ifdef CONFIG_USB_PD_ALT_MODE_DFP
	/* Initialize PD Policy engine */
	pd_dfp_pe_init(port);
#endif

#ifdef CONFIG_CHARGE_MANAGER
	/* Initialize PD and type-C supplier current limits to 0 */
	pd_set_input_current_limit(port, 0, 0);
	typec_set_input_current_limit(port, 0, 0);
	charge_manager_update_dualrole(port, CAP_UNKNOWN);
#endif

	while (1) {
		/* process VDM messages last */
		pd_vdm_send_state_machine(port);

		/* Verify board specific health status : current, voltages... */
		res = pd_board_checks();
		if (res != EC_SUCCESS) {
			/* cut the power */
			execute_hard_reset(port);
			/* notify the other side of the issue */
			pd_transmit(port, TRANSMIT_HARD_RESET, 0, NULL);
		}

		/* wait for next event/packet or timeout expiration */
		evt = task_wait_event(timeout);

#ifdef CONFIG_USB_PD_TCPC
		/*
		 * run port controller task to check CC and/or read incoming
		 * messages
		 */
		tcpc_run(port, evt);
#endif

		/* process any potential incoming message */
		incoming_packet = evt & PD_EVENT_RX;
		if (incoming_packet) {
			tcpm_get_message(port, payload, &head);
			if (head > 0)
				handle_request(port, head, payload);
		}
		/* if nothing to do, verify the state of the world in 500ms */
		this_state = pd[port].task_state;
		timeout = 500*MSEC;
		switch (this_state) {
		case PD_STATE_DISABLED:
			/* Nothing to do */
			break;
		case PD_STATE_SRC_DISCONNECTED:
			timeout = 10*MSEC;
			tcpm_get_cc(port, &cc1, &cc2);

			/* Vnc monitoring */
			if ((TYPEC_CC_IS_RD(cc1) ||
			     TYPEC_CC_IS_RD(cc2)) ||
			    (cc1 == TYPEC_CC_VOLT_RA &&
			     cc2 == TYPEC_CC_VOLT_RA)) {
#ifdef CONFIG_USBC_BACKWARDS_COMPATIBLE_DFP
				/* Enable VBUS */
				if (pd_set_power_supply_ready(port))
					break;
#endif
				pd[port].cc_state = PD_CC_NONE;
				set_state(port,
					PD_STATE_SRC_DISCONNECTED_DEBOUNCE);
			}
#ifdef CONFIG_USB_PD_DUAL_ROLE
			/* Swap roles if time expired */
			else if (drp_state != PD_DRP_FORCE_SOURCE &&
				 get_time().val >= next_role_swap) {
				pd[port].power_role = PD_ROLE_SINK;
				set_state(port, PD_STATE_SNK_DISCONNECTED);
				tcpm_set_cc(port, TYPEC_CC_RD);
				next_role_swap = get_time().val + PD_T_DRP_SNK;

				/* Swap states quickly */
				timeout = 2*MSEC;
			}
#endif
			break;
		case PD_STATE_SRC_DISCONNECTED_DEBOUNCE:
			timeout = 20*MSEC;
			tcpm_get_cc(port, &cc1, &cc2);

			if (TYPEC_CC_IS_RD(cc1) && TYPEC_CC_IS_RD(cc2)) {
				/* Debug accessory */
				new_cc_state = PD_CC_DEBUG_ACC;
			} else if (TYPEC_CC_IS_RD(cc1) ||
				   TYPEC_CC_IS_RD(cc2)) {
				/* UFP attached */
				new_cc_state = PD_CC_UFP_ATTACHED;
			} else if (cc1 == TYPEC_CC_VOLT_RA &&
				   cc2 == TYPEC_CC_VOLT_RA) {
				/* Audio accessory */
				new_cc_state = PD_CC_AUDIO_ACC;
			} else {
				/* No UFP */
#ifdef CONFIG_USBC_BACKWARDS_COMPATIBLE_DFP
				/* No connection any more, remove VBUS */
				pd_power_supply_reset(port);
#endif
				set_state(port, PD_STATE_SRC_DISCONNECTED);
				timeout = 5*MSEC;
				break;
			}

			/* Debounce the cc state */
			if (new_cc_state != pd[port].cc_state) {
				pd[port].cc_debounce = get_time().val +
						       PD_T_CC_DEBOUNCE;
				pd[port].cc_state = new_cc_state;
				break;
			} else if (get_time().val < pd[port].cc_debounce) {
				break;
			}

			/* Debounce complete */
			/* UFP is attached */
			if (new_cc_state == PD_CC_UFP_ATTACHED) {
				pd[port].polarity = (TYPEC_CC_IS_RD(cc2));
				tcpm_set_polarity(port, pd[port].polarity);

				/* initial data role for source is DFP */
				pd_set_data_role(port, PD_ROLE_DFP);

#ifndef CONFIG_USBC_BACKWARDS_COMPATIBLE_DFP
				/* Enable VBUS */
				if (pd_set_power_supply_ready(port)) {
#ifdef CONFIG_USBC_SS_MUX
					board_set_usb_mux(port, TYPEC_MUX_NONE,
							  USB_SWITCH_DISCONNECT,
							  pd[port].polarity);
#endif
					break;
				}
#endif
				/* If PD comm is enabled, enable TCPC RX */
				if (pd_comm_enabled)
					tcpm_set_rx_enable(port, 1);

#ifdef CONFIG_USBC_VCONN
				tcpm_set_vconn(port, 1);
				pd[port].flags |= PD_FLAGS_VCONN_ON;
#endif

				pd[port].flags |= PD_FLAGS_CHECK_PR_ROLE |
						  PD_FLAGS_CHECK_DR_ROLE;
				hard_reset_count = 0;
				timeout = 5*MSEC;
				set_state(port, PD_STATE_SRC_STARTUP);
			}
			/* Accessory is attached */
			else if (new_cc_state == PD_CC_AUDIO_ACC ||
				 new_cc_state == PD_CC_DEBUG_ACC) {
#ifdef CONFIG_USBC_BACKWARDS_COMPATIBLE_DFP
				/* Remove VBUS */
				pd_power_supply_reset(port);
#endif

#ifdef CONFIG_USBC_SS_MUX
				board_set_usb_mux(port, TYPEC_MUX_USB,
						  USB_SWITCH_CONNECT,
						  pd[port].polarity);
#endif

#ifdef CONFIG_CASE_CLOSED_DEBUG
				if (new_cc_state == PD_CC_DEBUG_ACC)
					ccd_set_mode(CCD_MODE_ENABLED);
#endif
				set_state(port, PD_STATE_SRC_ACCESSORY);
			}
			break;
		case PD_STATE_SRC_ACCESSORY:
			/* Combined audio / debug accessory state */
			timeout = 100*MSEC;

			tcpm_get_cc(port, &cc1, &cc2);

			/* If accessory becomes detached */
			if ((pd[port].cc_state == PD_CC_AUDIO_ACC &&
			     (cc1 != TYPEC_CC_VOLT_RA ||
			      cc2 != TYPEC_CC_VOLT_RA)) ||
			    (pd[port].cc_state == PD_CC_DEBUG_ACC &&
			     (!TYPEC_CC_IS_RD(cc1) ||
			      !TYPEC_CC_IS_RD(cc2)))) {
				set_state(port, PD_STATE_SRC_DISCONNECTED);
#ifdef CONFIG_CASE_CLOSED_DEBUG
				ccd_set_mode(CCD_MODE_DISABLED);
#endif
				timeout = 10*MSEC;
			}
			break;
		case PD_STATE_SRC_HARD_RESET_RECOVER:
			/* Do not continue until hard reset recovery time */
			if (get_time().val < pd[port].src_recover) {
				timeout = 50*MSEC;
				break;
			}

			/* Enable VBUS */
			timeout = 10*MSEC;
			if (pd_set_power_supply_ready(port)) {
				set_state(port, PD_STATE_SRC_DISCONNECTED);
				break;
			}
			set_state(port, PD_STATE_SRC_STARTUP);
			break;
		case PD_STATE_SRC_STARTUP:
			/* Wait for power source to enable */
			if (pd[port].last_state != pd[port].task_state) {
				/*
				 * fake set data role swapped flag so we send
				 * discover identity when we enter SRC_READY
				 */
				pd[port].flags |= PD_FLAGS_DATA_SWAPPED;
				/* reset various counters */
				caps_count = 0;
				pd[port].msg_id = 0;
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
					   PD_STATE_SRC_DISCONNECTED_DEBOUNCE
						? PD_T_CC_DEBOUNCE : 0),
#else
					get_time().val +
					PD_POWER_SUPPLY_TURN_ON_DELAY,
#endif
					PD_STATE_SRC_DISCOVERY);
			}
			break;
		case PD_STATE_SRC_DISCOVERY:
			if (pd[port].last_state != pd[port].task_state) {
				/*
				 * If we have had PD connection with this port
				 * partner, then start NoResponseTimer.
				 */
				if (pd[port].flags & PD_FLAGS_PREVIOUS_PD_CONN)
					set_state_timeout(port,
						get_time().val +
						PD_T_NO_RESPONSE,
						hard_reset_count <
						  PD_HARD_RESET_COUNT ?
						    PD_STATE_HARD_RESET_SEND :
						    PD_STATE_SRC_DISCONNECTED);
			}

			/* Send source cap some minimum number of times */
			if (caps_count < PD_CAPS_COUNT) {
				/* Query capabilites of the other side */
				res = send_source_cap(port);
				/* packet was acked => PD capable device) */
				if (res >= 0) {
					set_state(port,
						  PD_STATE_SRC_NEGOCIATE);
					timeout = 10*MSEC;
					hard_reset_count = 0;
					caps_count = 0;
					/* Port partner is PD capable */
					pd[port].flags |=
						PD_FLAGS_PREVIOUS_PD_CONN;
				} else { /* failed, retry later */
					timeout = PD_T_SEND_SOURCE_CAP;
					caps_count++;
				}
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
				set_state_timeout(
					port,
					get_time().val +
					PD_T_SINK_TRANSITION,
					PD_STATE_SRC_POWERED);
			break;
		case PD_STATE_SRC_POWERED:
			/* Switch to the new requested voltage */
			if (pd[port].last_state != pd[port].task_state) {
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
				timeout = 10*MSEC;
				/* it'a time to ping regularly the sink */
				set_state(port, PD_STATE_SRC_READY);
			} else {
				/* The sink did not ack, cut the power... */
				pd_power_supply_reset(port);
				set_state(port, PD_STATE_SRC_DISCONNECTED);
			}
			break;
		case PD_STATE_SRC_READY:
			timeout = PD_T_SOURCE_ACTIVITY;

			if (pd[port].last_state != pd[port].task_state)
				pd[port].flags |= PD_FLAGS_GET_SNK_CAP_SENT;

			/*
			 * Don't send any PD traffic if we woke up due to
			 * incoming packet or if VDO response pending to avoid
			 * collisions.
			 */
			if (incoming_packet ||
			    (pd[port].vdm_state == VDM_STATE_BUSY))
				break;

			/* Send get sink cap if haven't received it yet */
			if ((pd[port].flags & PD_FLAGS_GET_SNK_CAP_SENT) &&
			    !(pd[port].flags & PD_FLAGS_SNK_CAP_RECVD)) {
				/* Get sink cap to know if dual-role device */
				send_control(port, PD_CTRL_GET_SINK_CAP);
				set_state(port, PD_STATE_SRC_GET_SINK_CAP);
				pd[port].flags &= ~PD_FLAGS_GET_SNK_CAP_SENT;
				break;
			}

			/* Check power role policy, which may trigger a swap */
			if (pd[port].flags & PD_FLAGS_CHECK_PR_ROLE) {
				pd_check_pr_role(port, PD_ROLE_SOURCE,
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

			/* Send discovery SVDMs last */
			if (pd[port].data_role == PD_ROLE_DFP &&
			    (pd[port].flags & PD_FLAGS_DATA_SWAPPED)) {
#ifndef CONFIG_USB_PD_SIMPLE_DFP
				pd_send_vdm(port, USB_SID_PD,
					    CMD_DISCOVER_IDENT, NULL, 0);
#endif
				pd[port].flags &= ~PD_FLAGS_DATA_SWAPPED;
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
					timeout = 10*MSEC;
					/*
					 * If failed to get goodCRC, send
					 * soft reset, otherwise ignore
					 * failure.
					 */
					set_state(port, res == -1 ?
						   PD_STATE_SOFT_RESET :
						   READY_RETURN_STATE(port));
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
					timeout = 10*MSEC;
					/*
					 * If failed to get goodCRC, send
					 * soft reset, otherwise ignore
					 * failure.
					 */
					set_state(port, res == -1 ?
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
				set_state_timeout(port,
						  get_time().val +
						  PD_T_SINK_TRANSITION,
						  PD_STATE_SRC_SWAP_SRC_DISABLE);
			break;
		case PD_STATE_SRC_SWAP_SRC_DISABLE:
			/* Turn power off */
			if (pd[port].last_state != pd[port].task_state) {
				pd_power_supply_reset(port);
				set_state_timeout(port,
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
					timeout = 10*MSEC;
					set_state(port,
						  PD_STATE_SRC_DISCONNECTED);
					break;
				}
				/* Switch to Rd and swap roles to sink */
				tcpm_set_cc(port, TYPEC_CC_RD);
				pd[port].power_role = PD_ROLE_SINK;
				/* Wait for PS_RDY from new source */
				set_state_timeout(port,
						  get_time().val +
						  PD_T_PS_SOURCE_ON,
						  PD_STATE_SNK_DISCONNECTED);
			}
			break;
		case PD_STATE_SUSPENDED:
			/*
			 * TODO: Suspend state only supported if we are also
			 * the TCPC.
			 */
#ifdef CONFIG_USB_PD_TCPC
			pd_rx_disable_monitoring(port);
			pd_hw_release(port);
			pd_power_supply_reset(port);

			/* Wait for resume */
			while (pd[port].task_state == PD_STATE_SUSPENDED)
				task_wait_event(-1);

			pd_hw_init(port, PD_ROLE_DEFAULT);
#endif
			break;
		case PD_STATE_SNK_DISCONNECTED:
			timeout = 10*MSEC;
			tcpm_get_cc(port, &cc1, &cc2);

			/* Source connection monitoring */
			if (cc1 != TYPEC_CC_VOLT_OPEN ||
			    cc2 != TYPEC_CC_VOLT_OPEN) {
				pd[port].cc_state = PD_CC_NONE;
				hard_reset_count = 0;
				new_cc_state = PD_CC_DFP_ATTACHED;
				pd[port].cc_debounce = get_time().val +
							PD_T_CC_DEBOUNCE;
				set_state(port,
					PD_STATE_SNK_DISCONNECTED_DEBOUNCE);
				break;
			}

			/*
			 * If no source detected, check for role toggle.
			 * Do not toggle if VBUS is present.
			 */
			if (drp_state == PD_DRP_TOGGLE_ON &&
			    get_time().val >= next_role_swap &&
			    !pd_snk_is_vbus_provided(port)) {
				/* Swap roles to source */
				pd[port].power_role = PD_ROLE_SOURCE;
				set_state(port, PD_STATE_SRC_DISCONNECTED);
				tcpm_set_cc(port, TYPEC_CC_RP);
				next_role_swap = get_time().val + PD_T_DRP_SRC;

				/* Swap states quickly */
				timeout = 2*MSEC;
			}
			break;
		case PD_STATE_SNK_DISCONNECTED_DEBOUNCE:
			tcpm_get_cc(port, &cc1, &cc2);
			if (cc1 == TYPEC_CC_VOLT_OPEN &&
			    cc2 == TYPEC_CC_VOLT_OPEN) {
				/* No connection any more */
				set_state(port, PD_STATE_SNK_DISCONNECTED);
				timeout = 5*MSEC;
				break;
			}

			timeout = 20*MSEC;

			/* Wait for CC debounce and VBUS present */
			if (get_time().val < pd[port].cc_debounce ||
			    !pd_snk_is_vbus_provided(port))
				break;

			/* We are attached */
			pd[port].polarity = (cc2 != TYPEC_CC_VOLT_OPEN);
			tcpm_set_polarity(port, pd[port].polarity);
			/* reset message ID  on connection */
			pd[port].msg_id = 0;
			/* initial data role for sink is UFP */
			pd_set_data_role(port, PD_ROLE_UFP);
#ifdef CONFIG_CHARGE_MANAGER
			typec_curr = get_typec_current_limit(
				pd[port].polarity ? cc2 : cc1);
			typec_set_input_current_limit(
				port, typec_curr, TYPE_C_VOLTAGE);
#endif
			/* If PD comm is enabled, enable TCPC RX */
			if (pd_comm_enabled)
				tcpm_set_rx_enable(port, 1);

			/*
			 * fake set data role swapped flag so we send
			 * discover identity when we enter SRC_READY
			 */
			pd[port].flags |= PD_FLAGS_CHECK_PR_ROLE |
					  PD_FLAGS_CHECK_DR_ROLE |
					  PD_FLAGS_DATA_SWAPPED;
			set_state(port, PD_STATE_SNK_DISCOVERY);
			timeout = 10*MSEC;
			hook_call_deferred(
				pd_usb_billboard_deferred,
				PD_T_AME);
			break;
		case PD_STATE_SNK_HARD_RESET_RECOVER:
			if (pd[port].last_state != pd[port].task_state)
				pd[port].flags |= PD_FLAGS_DATA_SWAPPED;
#ifdef CONFIG_USB_PD_NO_VBUS_DETECT
			/*
			 * Can't measure vbus state so this is the maximum
			 * recovery time for the source.
			 */
			if (pd[port].last_state != pd[port].task_state)
				set_state_timeout(port, get_time().val +
						  PD_T_SAFE_0V +
						  PD_T_SRC_RECOVER_MAX +
						  PD_T_SRC_TURN_ON,
						  PD_STATE_SNK_DISCONNECTED);
#else
			/* Wait for VBUS to go low and then high*/
			if (pd[port].last_state != pd[port].task_state) {
				snk_hard_reset_vbus_off = 0;
				set_state_timeout(port,
						  get_time().val +
						  PD_T_SAFE_0V,
						  hard_reset_count <
						    PD_HARD_RESET_COUNT ?
						     PD_STATE_HARD_RESET_SEND :
						     PD_STATE_SNK_DISCOVERY);
			}

			if (!pd_snk_is_vbus_provided(port) &&
			    !snk_hard_reset_vbus_off) {
				/* VBUS has gone low, reset timeout */
				snk_hard_reset_vbus_off = 1;
				set_state_timeout(port,
						  get_time().val +
						  PD_T_SRC_RECOVER_MAX +
						  PD_T_SRC_TURN_ON,
						  PD_STATE_SNK_DISCONNECTED);
			}
			if (pd_snk_is_vbus_provided(port) &&
			    snk_hard_reset_vbus_off) {
				/* VBUS went high again */
				set_state(port, PD_STATE_SNK_DISCOVERY);
				timeout = 10*MSEC;
			}

			/*
			 * Don't need to set timeout because VBUS changing
			 * will trigger an interrupt and wake us up.
			 */
#endif
			break;
		case PD_STATE_SNK_DISCOVERY:
			/* Wait for source cap expired only if we are enabled */
			if ((pd[port].last_state != pd[port].task_state)
			    && pd_comm_enabled) {
				/*
				 * If we haven't passed hard reset counter,
				 * start SinkWaitCapTimer, otherwise start
				 * NoResponseTimer.
				 */
				if (hard_reset_count < PD_HARD_RESET_COUNT)
					set_state_timeout(port,
						  get_time().val +
						  PD_T_SINK_WAIT_CAP,
						  PD_STATE_HARD_RESET_SEND);
				else if (pd[port].flags &
					 PD_FLAGS_PREVIOUS_PD_CONN)
					/* ErrorRecovery */
					set_state_timeout(port,
						  get_time().val +
						  PD_T_NO_RESPONSE,
						  PD_STATE_SNK_DISCONNECTED);
#ifdef CONFIG_CHARGE_MANAGER
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

#ifdef CONFIG_CHARGE_MANAGER
			timeout = PD_T_SINK_ADJ - PD_T_DEBOUNCE;

			/* Check if CC pull-up has changed */
			tcpm_get_cc(port, &cc1, &cc2);
			if (pd[port].polarity)
				cc1 = cc2;
			if (typec_curr != get_typec_current_limit(cc1)) {
				/* debounce signal by requiring two reads */
				if (typec_curr_change) {
					/* set new input current limit */
					typec_curr = get_typec_current_limit(
							cc1);
					typec_set_input_current_limit(
					  port, typec_curr, TYPE_C_VOLTAGE);
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
			timeout = 20*MSEC;

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
				pd_send_request_msg(port, 0);
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

			/* If DFP, send discovery SVDMs */
			if (pd[port].data_role == PD_ROLE_DFP &&
			     (pd[port].flags & PD_FLAGS_DATA_SWAPPED)) {
				pd_send_vdm(port, USB_SID_PD,
					    CMD_DISCOVER_IDENT, NULL, 0);
				pd[port].flags &= ~PD_FLAGS_DATA_SWAPPED;
				break;
			}

			/* Sent all messages, don't need to wake very often */
			timeout = 200*MSEC;
			break;
		case PD_STATE_SNK_SWAP_INIT:
			if (pd[port].last_state != pd[port].task_state) {
				res = send_control(port, PD_CTRL_PR_SWAP);
				if (res < 0) {
					timeout = 10*MSEC;
					/*
					 * If failed to get goodCRC, send
					 * soft reset, otherwise ignore
					 * failure.
					 */
					set_state(port, res == -1 ?
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
			charge_manager_set_ceil(port, CHARGE_CEIL_NONE);
#endif
			set_state(port, PD_STATE_SNK_SWAP_SRC_DISABLE);
			timeout = 10*MSEC;
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
				/* Switch to Rp and enable power supply */
				tcpm_set_cc(port, TYPEC_CC_RP);
				if (pd_set_power_supply_ready(port)) {
					/* Restore Rd */
					tcpm_set_cc(port, TYPEC_CC_RD);
					timeout = 10*MSEC;
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

			caps_count = 0;
			pd[port].msg_id = 0;
			pd[port].power_role = PD_ROLE_SOURCE;
			pd_update_roles(port);
			set_state(port, PD_STATE_SRC_DISCOVERY);
			timeout = 10*MSEC;
			break;
#ifdef CONFIG_USBC_VCONN_SWAP
		case PD_STATE_VCONN_SWAP_SEND:
			if (pd[port].last_state != pd[port].task_state) {
				res = send_control(port, PD_CTRL_VCONN_SWAP);
				if (res < 0) {
					timeout = 10*MSEC;
					/*
					 * If failed to get goodCRC, send
					 * soft reset, otherwise ignore
					 * failure.
					 */
					set_state(port, res == -1 ?
						   PD_STATE_SOFT_RESET :
						   READY_RETURN_STATE(port));
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
					tcpm_set_vconn(port, 1);
					set_state_timeout(port,
					  get_time().val + PD_VCONN_SWAP_DELAY,
					  PD_STATE_VCONN_SWAP_READY);
				} else {
					set_state_timeout(port,
					  get_time().val + PD_T_VCONN_SOURCE_ON,
					  READY_RETURN_STATE(port));
				}
			}
			break;
		case PD_STATE_VCONN_SWAP_READY:
			if (pd[port].last_state != pd[port].task_state) {
				if (!(pd[port].flags & PD_FLAGS_VCONN_ON)) {
					/* VCONN is now on, send PS_RDY */
					pd[port].flags |= PD_FLAGS_VCONN_ON;
					res = send_control(port,
							   PD_CTRL_PS_RDY);
					if (res == -1) {
						timeout = 10*MSEC;
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
					tcpm_set_vconn(port, 0);
					pd[port].flags &= ~PD_FLAGS_VCONN_ON;
					set_state_timeout(port,
					  get_time().val + PD_VCONN_SWAP_DELAY,
					  READY_RETURN_STATE(port));
				}
			}
			break;
#endif /* CONFIG_USBC_VCONN_SWAP */
#endif /* CONFIG_USB_PD_DUAL_ROLE */
		case PD_STATE_SOFT_RESET:
			if (pd[port].last_state != pd[port].task_state) {
				res = send_control(port, PD_CTRL_SOFT_RESET);

				/* if soft reset failed, try hard reset. */
				if (res < 0) {
					set_state(port,
						  PD_STATE_HARD_RESET_SEND);
					timeout = 5*MSEC;
					break;
				}

				set_state_timeout(
					port,
					get_time().val + PD_T_SENDER_RESPONSE,
					PD_STATE_HARD_RESET_SEND);
			}
			break;
		case PD_STATE_HARD_RESET_SEND:
			hard_reset_count++;
			if (pd[port].last_state != pd[port].task_state)
				hard_reset_sent = 0;
#ifdef CONFIG_CHARGE_MANAGER
			if (pd[port].last_state == PD_STATE_SNK_DISCOVERY) {
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

			/* try sending hard reset until it succeeds */
			if (!hard_reset_sent) {
				if (pd_transmit(port, TRANSMIT_HARD_RESET,
						0, NULL) < 0) {
					timeout = 10*MSEC;
					break;
				}

				/* successfully sent hard reset */
				hard_reset_sent = 1;
				/*
				 * If we are source, delay before cutting power
				 * to allow sink time to get hard reset.
				 */
				if (pd[port].power_role == PD_ROLE_SOURCE) {
					set_state_timeout(port,
					  get_time().val + PD_T_PS_HARD_RESET,
					  PD_STATE_HARD_RESET_EXECUTE);
				} else {
					set_state(port,
						  PD_STATE_HARD_RESET_EXECUTE);
					timeout = 10*MSEC;
				}
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
			execute_hard_reset(port);
			timeout = 10*MSEC;
			break;
#ifdef CONFIG_COMMON_RUNTIME
		case PD_STATE_BIST_RX:
			send_bist_cmd(port);
			/* Delay at least enough for partner to finish BIST */
			timeout = PD_T_BIST_RECEIVE + 20*MSEC;
			/* Set to appropriate port disconnected state */
			set_state(port, DUAL_ROLE_IF_ELSE(port,
						PD_STATE_SNK_DISCONNECTED,
						PD_STATE_SRC_DISCONNECTED));
			break;
		case PD_STATE_BIST_TX:
			pd_transmit(port, TRANSMIT_BIST_MODE_2, 0, NULL);
			/* Delay at least enough to finish sending BIST */
			timeout = PD_T_BIST_TRANSMIT + 20*MSEC;
			/* Set to appropriate port disconnected state */
			set_state(port, DUAL_ROLE_IF_ELSE(port,
						PD_STATE_SNK_DISCONNECTED,
						PD_STATE_SRC_DISCONNECTED));
			break;
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
				timeout = timeout < 10*MSEC ? timeout : 10*MSEC;
			} else if (pd[port].timeout - now.val < timeout) {
				timeout = pd[port].timeout - now.val;
			}
		}

		/* Check for disconnection */
#ifdef CONFIG_USB_PD_DUAL_ROLE
		if (!pd_is_connected(port) || pd_is_power_swapping(port))
			continue;
#endif
		if (pd[port].power_role == PD_ROLE_SOURCE) {
			/* Source: detect disconnect by monitoring CC */
			tcpm_get_cc(port, &cc1, &cc2);
			if (pd[port].polarity)
				cc1 = cc2;
			if (cc1 == TYPEC_CC_VOLT_OPEN) {
				pd_power_supply_reset(port);
				set_state(port, PD_STATE_SRC_DISCONNECTED);
				/* Debouncing */
				timeout = 10*MSEC;
			}
		}
#ifdef CONFIG_USB_PD_DUAL_ROLE
		/*
		 * Sink disconnect if VBUS is low and we are not recovering
		 * a hard reset.
		 */
		if (pd[port].power_role == PD_ROLE_SINK &&
		    !pd_snk_is_vbus_provided(port) &&
		    pd[port].task_state != PD_STATE_SNK_HARD_RESET_RECOVER &&
		    pd[port].task_state != PD_STATE_HARD_RESET_EXECUTE) {
			/* Sink: detect disconnect by monitoring VBUS */
			set_state(port, PD_STATE_SNK_DISCONNECTED);
			/* set timeout small to reconnect fast */
			timeout = 5*MSEC;
		}
#endif /* CONFIG_USB_PD_DUAL_ROLE */
	}
}

void tcpc_alert(void)
{
	int status, i;

	/* loop over ports and check alert status */
	for (i = 0; i < CONFIG_USB_PD_PORT_COUNT; i++) {
		tcpm_alert_status(i, TCPC_REG_ALERT1, (uint8_t *)&status);
		if (status & TCPC_REG_ALERT1_CC_STATUS) {
			/* CC status changed, wake task */
			task_set_event(PD_PORT_TO_TASK_ID(i), PD_EVENT_CC, 0);
		}
		if (status & TCPC_REG_ALERT1_RX_STATUS) {
			/* message received */
			/*
			 * If TCPC is compiled in, then we will have already
			 * received PD_EVENT_RX from phy layer in
			 * pd_rx_event(), so we don't need to set another
			 * event. If TCPC is not running on this MCU, then
			 * this needs to wake the PD task.
			 */
#ifndef CONFIG_USB_PD_TCPC
			task_set_event(PD_PORT_TO_TASK_ID(i), PD_EVENT_RX, 0);
#endif
		}
		if (status & TCPC_REG_ALERT1_RX_HARD_RST) {
			/* hard reset received */
			execute_hard_reset(i);
			task_wake(PD_PORT_TO_TASK_ID(i));
		}
		if (status & TCPC_REG_ALERT1_TX_COMPLETE) {
			/* transmit complete */
			pd_transmit_complete(i, status);
		}
	}
}
#ifndef CONFIG_USB_PD_TCPC
DECLARE_DEFERRED(tcpc_alert);
#endif

#ifdef CONFIG_USB_PD_DUAL_ROLE
static void dual_role_on(void)
{
	int i;

	pd_set_dual_role(PD_DRP_TOGGLE_ON);
	CPRINTS("chipset -> S0");

	for (i = 0; i < CONFIG_USB_PD_PORT_COUNT; i++) {
#ifdef CONFIG_CHARGE_MANAGER
		if (charge_manager_get_active_charge_port() != i)
#endif
			pd[i].flags |= PD_FLAGS_CHECK_PR_ROLE |
				       PD_FLAGS_CHECK_DR_ROLE;
	}
}
DECLARE_HOOK(HOOK_CHIPSET_RESUME, dual_role_on, HOOK_PRIO_DEFAULT);

static void dual_role_off(void)
{
	pd_set_dual_role(PD_DRP_TOGGLE_OFF);
	CPRINTS("chipset -> S3");
}
DECLARE_HOOK(HOOK_CHIPSET_SUSPEND, dual_role_off, HOOK_PRIO_DEFAULT);
DECLARE_HOOK(HOOK_CHIPSET_STARTUP, dual_role_off, HOOK_PRIO_DEFAULT);

static void dual_role_force_sink(void)
{
	pd_set_dual_role(PD_DRP_FORCE_SINK);
	CPRINTS("chipset -> S5");
}
DECLARE_HOOK(HOOK_CHIPSET_SHUTDOWN, dual_role_force_sink, HOOK_PRIO_DEFAULT);

#ifdef HAS_TASK_CHIPSET
static void dual_role_init(void)
{
	if (chipset_in_state(CHIPSET_STATE_ANY_OFF))
		dual_role_force_sink();
	else if (chipset_in_state(CHIPSET_STATE_SUSPEND))
		dual_role_off();
	else /* CHIPSET_STATE_ON */
		dual_role_on();
}
DECLARE_HOOK(HOOK_INIT, dual_role_init, HOOK_PRIO_DEFAULT);
#endif /* HAS_TASK_CHIPSET */
#endif /* CONFIG_USB_PD_DUAL_ROLE */

#ifdef CONFIG_COMMON_RUNTIME
void pd_set_suspend(int port, int enable)
{
	set_state(port, enable ? PD_STATE_SUSPENDED : PD_DEFAULT_STATE);

	task_wake(PD_PORT_TO_TASK_ID(port));
}

#if defined(CONFIG_CMD_PD) && defined(CONFIG_CMD_PD_FLASH)
static int hex8tou32(char *str, uint32_t *val)
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

static int remote_flashing(int argc, char **argv)
{
	int port, cnt, cmd;
	uint32_t data[VDO_MAX_SIZE-1];
	char *e;
	static int flash_offset[CONFIG_USB_PD_PORT_COUNT];

	if (argc < 4 || argc > (VDO_MAX_SIZE + 4 - 1))
		return EC_ERROR_PARAM_COUNT;

	port = strtoi(argv[1], &e, 10);
	if (*e || port >= CONFIG_USB_PD_PORT_COUNT)
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
			if (hex8tou32(argv[i+3], data + i))
				return EC_ERROR_INVAL;
		cmd = VDO_CMD_FLASH_WRITE;
		cnt = argc;
		ccprintf("WRITE %d @%04x ...", argc * 4,
			 flash_offset[port]);
		flash_offset[port] += argc * 4;
	}

	pd_send_vdm(port, USB_VID_GOOGLE, cmd, data, cnt);

	/* Wait until VDM is done */
	while (pd[port].vdm_state > 0)
		task_wait_event(100*MSEC);

	ccprintf("DONE %d\n", pd[port].vdm_state);
	return EC_SUCCESS;
}
#endif /* defined(CONFIG_CMD_PD) && defined(CONFIG_CMD_PD_FLASH) */

#if defined(CONFIG_USB_PD_ALT_MODE) && !defined(CONFIG_USB_PD_ALT_MODE_DFP)
void pd_send_hpd(int port, enum hpd_event hpd)
{
	uint32_t data[1];
	int opos = pd_alt_mode(port, USB_SID_DISPLAYPORT);
	if (!opos)
		return;

	data[0] = VDO_DP_STATUS((hpd == hpd_irq),  /* IRQ_HPD */
				(hpd != hpd_low),  /* HPD_HI|LOW */
				0,		      /* request exit DP */
				0,		      /* request exit USB */
				0,		      /* MF pref */
				1,                    /* enabled */
				0,		      /* power low */
				0x2);
	pd_send_vdm(port, USB_SID_DISPLAYPORT,
		    VDO_OPOS(opos) | CMD_ATTENTION, data, 1);
	/* Wait until VDM is done. */
	while (pd[0].vdm_state > 0)
		task_wait_event(USB_PD_RX_TMOUT_US * (PD_RETRY_COUNT + 1));
}
#endif

int pd_fetch_acc_log_entry(int port)
{
	timestamp_t timeout;

	/* Cannot send a VDM now, the host should retry */
	if (pd[port].vdm_state > 0)
		return pd[port].vdm_state == VDM_STATE_BUSY ?
				EC_RES_BUSY : EC_RES_UNAVAILABLE;

	pd_send_vdm(port, USB_VID_GOOGLE, VDO_CMD_GET_LOG, NULL, 0);
	timeout.val = get_time().val + 500*MSEC;

	/* Wait until VDM is done */
	while ((pd[port].vdm_state > 0) &&
	       (get_time().val < timeout.val))
		task_wait_event(10*MSEC);

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
		pd[port].power_role = PD_ROLE_SINK;
		tcpm_set_cc(port, TYPEC_CC_RD);
		set_state(port, PD_STATE_SNK_DISCONNECTED);
	}

	task_wake(PD_PORT_TO_TASK_ID(port));
}
#endif /* CONFIG_USB_PD_DUAL_ROLE */

static int command_pd(int argc, char **argv)
{
	int port;
	char *e;

	if (argc < 2)
		return EC_ERROR_PARAM_COUNT;

#if defined(CONFIG_CMD_PD) && defined(CONFIG_USB_PD_DUAL_ROLE)
	/* command: pd <subcmd> <args> */
	if (!strcasecmp(argv[1], "dualrole")) {
		if (argc < 3) {
			ccprintf("dual-role toggling: ");
			switch (drp_state) {
			case PD_DRP_TOGGLE_ON:
				ccprintf("on\n");
				break;
			case PD_DRP_TOGGLE_OFF:
				ccprintf("off\n");
				break;
			case PD_DRP_FORCE_SINK:
				ccprintf("force sink\n");
				break;
			case PD_DRP_FORCE_SOURCE:
				ccprintf("force source\n");
				break;
			}
		} else {
			if (!strcasecmp(argv[2], "on"))
				pd_set_dual_role(PD_DRP_TOGGLE_ON);
			else if (!strcasecmp(argv[2], "off"))
				pd_set_dual_role(PD_DRP_TOGGLE_OFF);
			else if (!strcasecmp(argv[2], "sink"))
				pd_set_dual_role(PD_DRP_FORCE_SINK);
			else if (!strcasecmp(argv[2], "source"))
				pd_set_dual_role(PD_DRP_FORCE_SOURCE);
			else
				return EC_ERROR_PARAM3;
		}
		return EC_SUCCESS;
	} else
#endif
	if (!strcasecmp(argv[1], "dump")) {
		int level;

		if (argc < 3)
			ccprintf("dump level: %d\n", debug_level);
		else {
			level = strtoi(argv[2], &e, 10);
			if (*e)
				return EC_ERROR_PARAM2;
			debug_level = level;
		}
		return EC_SUCCESS;
	}
#ifdef CONFIG_CMD_PD
	else if (!strcasecmp(argv[1], "enable")) {
		int enable;

		if (argc < 3)
			return EC_ERROR_PARAM_COUNT;

		enable = strtoi(argv[2], &e, 10);
		if (*e)
			return EC_ERROR_PARAM3;
		pd_comm_enable(enable);
		ccprintf("Ports %s\n", enable ? "enabled" : "disabled");
		return EC_SUCCESS;
	}
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

#endif
	/* command: pd <port> <subcmd> [args] */
	port = strtoi(argv[1], &e, 10);
	if (argc < 3)
		return EC_ERROR_PARAM_COUNT;
	if (*e || port >= CONFIG_USB_PD_PORT_COUNT)
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
		pd[port].power_role = PD_ROLE_SOURCE;
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
	} else if (!strncasecmp(argv[2], "hard", 4)) {
		set_state(port, PD_STATE_HARD_RESET_SEND);
		task_wake(PD_PORT_TO_TASK_ID(port));
	} else if (!strncasecmp(argv[2], "info", 4)) {
		int i;
		ccprintf("Hash ");
		for (i = 0; i < PD_RW_HASH_SIZE / 4; i++)
			ccprintf("%08x ", pd[port].dev_rw_hash[i]);
		ccprintf("\nImage %s\n", system_image_copy_t_to_string(
						pd[port].current_image));
	} else if (!strncasecmp(argv[2], "soft", 4)) {
		set_state(port, PD_STATE_SOFT_RESET);
		task_wake(PD_PORT_TO_TASK_ID(port));
	} else if (!strncasecmp(argv[2], "swap", 4)) {
		if (argc < 4)
			return EC_ERROR_PARAM_COUNT;

		if (!strncasecmp(argv[3], "power", 5)) {
			pd_request_power_swap(port);
		} else if (!strncasecmp(argv[3], "data", 4)) {
			pd_request_data_swap(port);
#ifdef CONFIG_USBC_VCONN_SWAP
		} else if (!strncasecmp(argv[3], "vconn", 5)) {
			set_state(port, PD_STATE_VCONN_SWAP_SEND);
			task_wake(PD_PORT_TO_TASK_ID(port));
#endif
		} else {
			return EC_ERROR_PARAM3;
		}
	} else if (!strncasecmp(argv[2], "ping", 4)) {
		int enable;

		if (argc > 3) {
			enable = strtoi(argv[3], &e, 10);
			if (*e)
				return EC_ERROR_PARAM3;
			pd_ping_enable(port, enable);
		}

		ccprintf("Pings %s\n",
			 (pd[port].flags & PD_FLAGS_PING_ENABLED) ?
			 "on" : "off");
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
			pd_send_vdm(port, USB_VID_GOOGLE, VDO_CMD_CURRENT,
				    NULL, 0);
		} else if (!strncasecmp(argv[3], "vers", 4)) {
			pd_send_vdm(port, USB_VID_GOOGLE, VDO_CMD_VERSION,
				    NULL, 0);
		} else {
			return EC_ERROR_PARAM_COUNT;
		}
#if defined(CONFIG_CMD_PD) && defined(CONFIG_CMD_PD_FLASH)
	} else if (!strncasecmp(argv[2], "flash", 4)) {
		return remote_flashing(argc, argv);
#endif
	} else
#endif
	if (!strncasecmp(argv[2], "state", 5)) {
		ccprintf("Port C%d, %s - Role: %s-%s%s Polarity: CC%d "
			 "Flags: 0x%04x, State: %s\n",
			port, pd_comm_enabled ? "Ena" : "Dis",
			pd[port].power_role == PD_ROLE_SOURCE ? "SRC" : "SNK",
			pd[port].data_role == PD_ROLE_DFP ? "DFP" : "UFP",
			(pd[port].flags & PD_FLAGS_VCONN_ON) ? "-VC" : "",
			pd[port].polarity + 1,
			pd[port].flags,
			pd_state_names[pd[port].task_state]);
	} else {
		return EC_ERROR_PARAM1;
	}

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(pd, command_pd,
			"dualrole|dump|enable [0|1]|rwhashtable|\n\t<port> "
			"[tx|bist_rx|bist_tx|charger|clock|dev"
			"|soft|hash|hard|ping|state|swap [power|data]|"
			"vdm [ping | curr | vers]]",
			"USB PD",
			NULL);

#ifdef CONFIG_USBC_SS_MUX
#ifdef CONFIG_CMD_TYPEC
static int command_typec(int argc, char **argv)
{
	const char * const mux_name[] = {"none", "usb", "dp", "dock"};
	char *e;
	int port;
	enum typec_mux mux = TYPEC_MUX_NONE;
	int i;

	if (argc < 2)
		return EC_ERROR_PARAM_COUNT;

	port = strtoi(argv[1], &e, 10);
	if (*e || port >= CONFIG_USB_PD_PORT_COUNT)
		return EC_ERROR_PARAM1;

	if (argc < 3) {
		const char *dp_str, *usb_str;
		ccprintf("Port C%d: polarity:CC%d\n",
			port, pd_get_polarity(port) + 1);
		if (board_get_usb_mux(port, &dp_str, &usb_str))
			ccprintf("Superspeed %s%s%s\n",
				 dp_str ? dp_str : "",
				 dp_str && usb_str ? "+" : "",
				 usb_str ? usb_str : "");
		else
			ccprintf("No Superspeed connection\n");

		return EC_SUCCESS;
	}

	for (i = 0; i < ARRAY_SIZE(mux_name); i++)
		if (!strcasecmp(argv[2], mux_name[i]))
			mux = i;
	board_set_usb_mux(port, mux, mux == TYPEC_MUX_NONE ?
					USB_SWITCH_DISCONNECT :
					USB_SWITCH_CONNECT,
			  pd_get_polarity(port));
	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(typec, command_typec,
			"<port> [none|usb|dp|dock]",
			"Control type-C connector muxing",
			NULL);
#endif /* CONFIG_CMD_TYPEC */
#endif /* CONFIG_USBC_SS_MUX */

#ifdef HAS_TASK_HOSTCMD

static int hc_pd_ports(struct host_cmd_handler_args *args)
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

static int hc_usb_pd_control(struct host_cmd_handler_args *args)
{
	const struct ec_params_usb_pd_control *p = args->params;
	struct ec_response_usb_pd_control_v1 *r_v1 = args->response;
	struct ec_response_usb_pd_control *r = args->response;

	if (p->port >= CONFIG_USB_PD_PORT_COUNT)
		return EC_RES_INVALID_PARAM;

	if (p->role >= USB_PD_CTRL_ROLE_COUNT ||
	    p->mux >= USB_PD_CTRL_MUX_COUNT)
		return EC_RES_INVALID_PARAM;

	if (p->role != USB_PD_CTRL_ROLE_NO_CHANGE)
		pd_set_dual_role(dual_role_map[p->role]);

#ifdef CONFIG_USBC_SS_MUX
	if (p->mux != USB_PD_CTRL_MUX_NO_CHANGE)
		board_set_usb_mux(p->port, typec_mux_map[p->mux],
				  typec_mux_map[p->mux] == TYPEC_MUX_NONE ?
					USB_SWITCH_DISCONNECT :
					USB_SWITCH_CONNECT,
				  pd_get_polarity(p->port));
#endif /* CONFIG_USBC_SS_MUX */

	if (args->version == 0) {
		r->enabled = pd_comm_enabled;
		r->role = pd[p->port].power_role;
		r->polarity = pd[p->port].polarity;
		r->state = pd[p->port].task_state;
		args->response_size = sizeof(*r);
	} else {
		r_v1->enabled = pd_comm_enabled |
				(pd_is_connected(p->port) << 1);
		r_v1->role = pd[p->port].power_role |
			    (pd[p->port].data_role << 1);
		r_v1->polarity = pd[p->port].polarity;
		strzcpy(r_v1->state,
			pd_state_names[pd[p->port].task_state],
			sizeof(r_v1->state));
		args->response_size = sizeof(*r_v1);
	}
	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_USB_PD_CONTROL,
		     hc_usb_pd_control,
		     EC_VER_MASK(0) | EC_VER_MASK(1));

static int hc_remote_flash(struct host_cmd_handler_args *args)
{
	const struct ec_params_usb_pd_fw_update *p = args->params;
	int port = p->port;
	const uint32_t *data = &(p->size) + 1;
	int i, size, rv = EC_RES_SUCCESS;
	timestamp_t timeout;

	if (port >= CONFIG_USB_PD_PORT_COUNT)
		return EC_RES_INVALID_PARAM;

	if (p->size + sizeof(*p) > args->params_size)
		return EC_RES_INVALID_PARAM;

#if defined(CONFIG_BATTERY_PRESENT_CUSTOM) ||	\
	defined(CONFIG_BATTERY_PRESENT_GPIO)
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
		timeout.val = get_time().val + 500*MSEC;
		break;

	case USB_PD_FW_FLASH_WRITE:
		/* Data size must be a multiple of 4 */
		if (!p->size || p->size % 4)
			return EC_RES_INVALID_PARAM;

		size = p->size / 4;
		for (i = 0; i < size; i += VDO_MAX_SIZE - 1) {
			pd_send_vdm(port, USB_VID_GOOGLE, VDO_CMD_FLASH_WRITE,
				    data + i, MIN(size - i, VDO_MAX_SIZE - 1));
			timeout.val = get_time().val + 500*MSEC;

			/* Wait until VDM is done */
			while ((pd[port].vdm_state > 0) &&
			       (get_time().val < timeout.val))
				task_wait_event(10*MSEC);

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
		task_wait_event(50*MSEC);

	if ((pd[port].vdm_state > 0) ||
	    (pd[port].vdm_state == VDM_STATE_ERR_TMOUT))
		rv = EC_RES_TIMEOUT;
	else if (pd[port].vdm_state < 0)
		rv = EC_RES_ERROR;

	return rv;
}
DECLARE_HOST_COMMAND(EC_CMD_USB_PD_FW_UPDATE,
		     hc_remote_flash,
		     EC_VER_MASK(0));

static int hc_remote_rw_hash_entry(struct host_cmd_handler_args *args)
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

static int hc_remote_pd_dev_info(struct host_cmd_handler_args *args)
{
	const uint8_t *port = args->params;
	struct ec_params_usb_pd_rw_hash_entry *r = args->response;

	if (*port >= CONFIG_USB_PD_PORT_COUNT)
		return EC_RES_INVALID_PARAM;

	r->dev_id = pd[*port].dev_id;

	if (r->dev_id) {
		memcpy(r->dev_rw_hash, pd[*port].dev_rw_hash,
		       PD_RW_HASH_SIZE);
	}

	r->current_image = pd[*port].current_image;

	args->response_size = sizeof(*r);
	return EC_RES_SUCCESS;
}

DECLARE_HOST_COMMAND(EC_CMD_USB_PD_DEV_INFO,
		     hc_remote_pd_dev_info,
		     EC_VER_MASK(0));

#ifdef CONFIG_USB_PD_ALT_MODE_DFP
static int hc_remote_pd_set_amode(struct host_cmd_handler_args *args)
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

#endif /* CONFIG_COMMON_RUNTIME */
