/* Copyright 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "atomic.h"
#include "charge_manager.h"
#include "common.h"
#include "console.h"
#include "ec_commands.h"
#include "flash.h"
#include "gpio.h"
#include "hooks.h"
#include "host_command.h"
#include "mkbp_event.h"
#include "registers.h"
#include "rsa.h"
#include "sha256.h"
#include "system.h"
#include "task.h"
#include "tcpm.h"
#include "timer.h"
#include "util.h"
#include "usb_api.h"
#include "usb_common.h"
#include "usb_pd.h"
#include "usbc_ppc.h"
#include "version.h"

#ifdef CONFIG_COMMON_RUNTIME
#define CPRINTS(format, args...) cprints(CC_USBPD, format, ## args)
#define CPRINTF(format, args...) cprintf(CC_USBPD, format, ## args)
#else
#define CPRINTS(format, args...)
#define CPRINTF(format, args...)
#endif

static int rw_flash_changed = 1;

#ifdef CONFIG_MKBP_EVENT
static int dp_alt_mode_entry_get_next_event(uint8_t *data)
{
	return EC_SUCCESS;
}
DECLARE_EVENT_SOURCE(EC_MKBP_EVENT_DP_ALT_MODE_ENTERED,
		     dp_alt_mode_entry_get_next_event);

void pd_notify_dp_alt_mode_entry(void)
{
	CPRINTS("Notifying AP of DP Alt Mode Entry...");
	mkbp_send_event(EC_MKBP_EVENT_DP_ALT_MODE_ENTERED);
}
#endif /* CONFIG_MKBP_EVENT */

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

__attribute__((weak)) int pd_board_check_request(uint32_t rdo, int pdo_cnt)
{
	int idx = RDO_POS(rdo);

	/* Check for invalid index */
	return (!idx || idx > pdo_cnt) ?
		EC_ERROR_INVAL : EC_SUCCESS;
}

#ifdef CONFIG_USB_PD_DUAL_ROLE
/* Last received source cap */
static uint32_t pd_src_caps[CONFIG_USB_PD_PORT_COUNT][PDO_MAX_OBJECTS];
static uint8_t pd_src_cap_cnt[CONFIG_USB_PD_PORT_COUNT];

/* Cap on the max voltage requested as a sink (in millivolts) */
static unsigned max_request_mv = PD_MAX_VOLTAGE_MV; /* no cap */

const uint32_t * const pd_get_src_caps(int port)
{
	ASSERT(port < CONFIG_USB_PD_PORT_COUNT);

	return pd_src_caps[port];
}

uint8_t pd_get_src_cap_cnt(int port)
{
	ASSERT(port < CONFIG_USB_PD_PORT_COUNT);

	return pd_src_cap_cnt[port];
}

void pd_process_source_cap(int port, int cnt, uint32_t *src_caps)
{
#ifdef CONFIG_CHARGE_MANAGER
	uint32_t ma, mv, pdo;
#endif
	int i;

	pd_src_cap_cnt[port] = cnt;
	for (i = 0; i < cnt; i++)
		pd_src_caps[port][i] = *src_caps++;

#ifdef CONFIG_CHARGE_MANAGER
	/* Get max power info that we could request */
	pd_find_pdo_index(pd_get_src_cap_cnt(port), pd_get_src_caps(port),
						PD_MAX_VOLTAGE_MV, &pdo);
	pd_extract_pdo_power(pdo, &ma, &mv);

	/* Set max. limit, but apply 500mA ceiling */
	charge_manager_set_ceil(port, CEIL_REQUESTOR_PD, PD_MIN_MA);
	pd_set_input_current_limit(port, ma, mv);
#endif
}

void pd_set_max_voltage(unsigned mv)
{
	max_request_mv = mv;
}

unsigned pd_get_max_voltage(void)
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
	return (vid == USB_VID_APPLE && (pid == 0x1012 || pid == 0x1013));
}
#endif /* CONFIG_USB_PD_DUAL_ROLE */

static struct pd_cable cable[CONFIG_USB_PD_PORT_COUNT];

static uint8_t is_transmit_msg_sop_prime(int port)
{
	if (IS_ENABLED(CONFIG_USB_PD_DECODE_SOP))
		return !!(cable[port].flags & CABLE_FLAGS_SOP_PRIME_ENABLE);

	return 0;
}

uint8_t is_sop_prime_ready(int port, uint8_t data_role, uint32_t pd_flags)
{
	/*
	 * Ref: USB PD 3.0 sec 2.5.4: When an Explicit Contract is in place the
	 * VCONN Source (either the DFP or the UFP) can communicate with the
	 * Cable Plug(s) using SOP’/SOP’’ Packets
	 *
	 * Ref: USB PD 2.0 sec 2.4.4: When an Explicit Contract is in place the
	 * DFP (either the Source or the Sink) can communicate with the
	 * Cable Plug(s) using SOP’/SOP” Packets.
	 * Sec 3.6.11 : Before communicating with a Cable Plug a Port Should
	 * ensure that it is the Vconn Source
	 */
	if (pd_flags & PD_FLAGS_VCONN_ON && (IS_ENABLED(CONFIG_USB_PD_REV30) ||
		data_role == PD_ROLE_DFP))
		return is_transmit_msg_sop_prime(port);

	return 0;
}

void reset_pd_cable(int port)
{
	if (IS_ENABLED(CONFIG_USB_PD_DECODE_SOP))
		memset(&cable[port], 0, sizeof(cable[port]));
}

uint8_t get_usb_pd_mux_cable_type(int port)
{
	return cable[port].type;
}

#ifdef CONFIG_USB_PD_ALT_MODE

#ifdef CONFIG_USB_PD_ALT_MODE_DFP

static struct pd_policy pe[CONFIG_USB_PD_PORT_COUNT];

static int is_vdo_present(int cnt, int index)
{
	return cnt > index;
}

static void enable_transmit_sop_prime(int port)
{
	cable[port].flags |= CABLE_FLAGS_SOP_PRIME_ENABLE;
}

static void disable_transmit_sop_prime(int port)
{
	cable[port].flags &= ~CABLE_FLAGS_SOP_PRIME_ENABLE;
}

void pd_dfp_pe_init(int port)
{
	memset(&pe[port], 0, sizeof(struct pd_policy));
}

static void dfp_consume_identity(int port, int cnt, uint32_t *payload)
{
	int ptype = PD_IDH_PTYPE(payload[VDO_I(IDH)]);
	size_t identity_size = MIN(sizeof(pe[port].identity),
				   (cnt - 1) * sizeof(uint32_t));
	pd_dfp_pe_init(port);
	memcpy(&pe[port].identity, payload + 1, identity_size);
	switch (ptype) {
	case IDH_PTYPE_AMA:
/* Leave vbus ON if the following macro is false */
#if defined(CONFIG_USB_PD_DUAL_ROLE) && defined(CONFIG_USBC_VCONN_SWAP)
		/* Adapter is requesting vconn, try to supply it */
		if (PD_VDO_AMA_VCONN_REQ(payload[VDO_I(AMA)]))
			pd_try_vconn_src(port);

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

static void dfp_consume_cable_response(int port, int cnt, uint32_t *payload)
{
	if (cable[port].is_identified)
		return;

	if (is_vdo_present(cnt, VDO_INDEX_IDH)) {
		cable[port].type = PD_IDH_PTYPE(payload[VDO_INDEX_IDH]);
		if (is_vdo_present(cnt, VDO_INDEX_PTYPE_CABLE1))
			cable[port].attr.raw_value =
					payload[VDO_INDEX_PTYPE_CABLE1];
	}
	/*
	 * Ref USB PD Spec 3.0  Pg 145. For active cable there are two VDOs.
	 * Hence storing the second VDO.
	 */
	if (IS_ENABLED(CONFIG_USB_PD_REV30) &&
	    is_vdo_present(cnt, VDO_INDEX_PTYPE_CABLE2) &&
	    cable[port].type == IDH_PTYPE_ACABLE) {
		cable[port].rev = PD_REV30;
		cable[port].attr2.raw_value = payload[VDO_INDEX_PTYPE_CABLE2];
	}
	cable[port].is_identified = 1;
}

static int dfp_discover_ident(uint32_t *payload)
{
	payload[0] = VDO(USB_SID_PD, 1, CMD_DISCOVER_IDENT);
	return 1;
}

static int dfp_discover_svids(uint32_t *payload)
{
	payload[0] = VDO(USB_SID_PD, 1, CMD_DISCOVER_SVID);
	return 1;
}

static void dfp_consume_svids(int port, int cnt, uint32_t *payload)
{
	int i;
	uint32_t *ptr = payload + 1;
	int vdo = 1;
	uint16_t svid0, svid1;

	for (i = pe[port].svid_cnt; i < pe[port].svid_cnt + 12; i += 2) {
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
		pe[port].svids[i].svid = svid0;
		pe[port].svid_cnt++;

		svid1 = PD_VDO_SVID_SVID1(*ptr);
		if (!svid1)
			break;
		pe[port].svids[i + 1].svid = svid1;
		pe[port].svid_cnt++;
		ptr++;
		vdo++;
	}
	/* TODO(tbroch) need to re-issue discover svids if > 12 */
	if (i && ((i % 12) == 0))
		CPRINTF("ERR:SVID+12\n");
}

static int dfp_discover_modes(int port, uint32_t *payload)
{
	uint16_t svid = pe[port].svids[pe[port].svid_idx].svid;
	if (pe[port].svid_idx >= pe[port].svid_cnt)
		return 0;
	payload[0] = VDO(svid, 1, CMD_DISCOVER_MODES);
	return 1;
}

static void dfp_consume_modes(int port, int cnt, uint32_t *payload)
{
	int idx = pe[port].svid_idx;
	pe[port].svids[idx].mode_cnt = cnt - 1;
	if (pe[port].svids[idx].mode_cnt < 0) {
		CPRINTF("ERR:NOMODE\n");
	} else {
		memcpy(pe[port].svids[pe[port].svid_idx].mode_vdo, &payload[1],
		       sizeof(uint32_t) * pe[port].svids[idx].mode_cnt);
	}

	pe[port].svid_idx++;
}

static int get_mode_idx(int port, uint16_t svid)
{
	int i;

	for (i = 0; i < PD_AMODE_COUNT; i++) {
		if (pe[port].amodes[i].fx->svid == svid)
			return i;
	}
	return -1;
}

static struct svdm_amode_data *get_modep(int port, uint16_t svid)
{
	int idx = get_mode_idx(port, svid);

	return (idx == -1) ? NULL : &pe[port].amodes[idx];
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
	if (pe[port].amode_idx == PD_AMODE_COUNT) {
		CPRINTF("ERR:NO AMODE SPACE\n");
		return -1;
	}

	/* Allocate ...  if SVID == 0 enter default supported policy */
	for (i = 0; i < supported_modes_cnt; i++) {
		for (j = 0; j < pe[port].svid_cnt; j++) {
			struct svdm_svid_data *svidp = &pe[port].svids[j];
			if ((svidp->svid != supported_modes[i].svid) ||
			    (svid && (svidp->svid != svid)))
				continue;

			modep = &pe[port].amodes[pe[port].amode_idx];
			modep->fx = &supported_modes[i];
			modep->data = &pe[port].svids[j];
			pe[port].amode_idx++;
			return pe[port].amode_idx - 1;
		}
	}
	return -1;
}

/*
 * Enter default mode ( payload[0] == 0 ) or attempt to enter mode via svid &
 * opos
*/
uint32_t pd_dfp_enter_mode(int port, uint16_t svid, int opos)
{
	int mode_idx = allocate_mode(port, svid);
	struct svdm_amode_data *modep;
	uint32_t mode_caps;

	if (mode_idx == -1)
		return 0;
	modep = &pe[port].amodes[mode_idx];

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
			if (pe[port].amodes[idx].fx)
				pe[port].amodes[idx].fx->exit(port);

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
	/* exit the mode */
	modep->opos = 0;
	return 1;
}

uint16_t pd_get_identity_vid(int port)
{
	return PD_IDH_VID(pe[port].identity[0]);
}

uint16_t pd_get_identity_pid(int port)
{
	return PD_PRODUCT_PID(pe[port].identity[2]);
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

	if (pe[port].identity[0] == 0) {
		ccprintf("No identity discovered yet.\n");
		return;
	}
	idh_ptype = PD_IDH_PTYPE(pe[port].identity[0]);
	ccprintf("IDENT:\n");
	ccprintf("\t[ID Header] %08x :: %s, VID:%04x\n", pe[port].identity[0],
		 idh_ptype_names[idh_ptype], pd_get_identity_vid(port));
	ccprintf("\t[Cert Stat] %08x\n", pe[port].identity[1]);
	for (i = 2; i < ARRAY_SIZE(pe[port].identity); i++) {
		ccprintf("\t");
		if (pe[port].identity[i])
			ccprintf("[%d] %08x ", i, pe[port].identity[i]);
	}
	ccprintf("\n");

	if (pe[port].svid_cnt < 1) {
		ccprintf("No SVIDS discovered yet.\n");
		return;
	}

	for (i = 0; i < pe[port].svid_cnt; i++) {
		ccprintf("SVID[%d]: %04x MODES:", i, pe[port].svids[i].svid);
		for (j = 0; j < pe[port].svids[j].mode_cnt; j++)
			ccprintf(" [%d] %08x", j + 1,
				 pe[port].svids[i].mode_vdo[j]);
		ccprintf("\n");
		modep = get_modep(port, pe[port].svids[i].svid);
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
	if (*e || port >= CONFIG_USB_PD_PORT_COUNT)
		return EC_ERROR_PARAM2;
	if (!strncasecmp(argv[2], "dump", 4))
		dump_pe(port);

	return EC_SUCCESS;
}

DECLARE_CONSOLE_COMMAND(pe, command_pe,
			"<port> dump",
			"USB PE");
#endif /* CONFIG_CMD_USB_PD_PE */

#endif /* CONFIG_USB_PD_ALT_MODE_DFP */

int pd_svdm(int port, int cnt, uint32_t *payload, uint32_t **rpayload)
{
	int cmd = PD_VDO_CMD(payload[0]);
	int cmd_type = PD_VDO_CMDT(payload[0]);
	int (*func)(int port, uint32_t *payload) = NULL;

	int rsize = 1; /* VDM header at a minimum */

	payload[0] &= ~VDO_CMDT_MASK;
	*rpayload = payload;

	if (cmd_type == CMDT_INIT) {
		switch (cmd) {
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
			if (svdm_rsp.amode)
				func = svdm_rsp.amode->status;
			break;
		case CMD_DP_CONFIG:
			if (svdm_rsp.amode)
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
			return 0;
#endif
		default:
			CPRINTF("ERR:CMD:%d\n", cmd);
			rsize = 0;
		}
		if (func)
			rsize = func(port, payload);
		else /* not supported : NACK it */
			rsize = 0;
		if (rsize >= 1)
			payload[0] |= VDO_CMDT(CMDT_RSP_ACK);
		else if (!rsize) {
			payload[0] |= VDO_CMDT(CMDT_RSP_NAK);
			rsize = 1;
		} else {
			payload[0] |= VDO_CMDT(CMDT_RSP_BUSY);
			rsize = 1;
		}
		payload[0] |= VDO_SVDM_VERS(pd_get_vdo_ver(port));
	} else if (cmd_type == CMDT_RSP_ACK) {
#ifdef CONFIG_USB_PD_ALT_MODE_DFP
		struct svdm_amode_data *modep;

		modep = get_modep(port, PD_VDO_VID(payload[0]));
#endif
		switch (cmd) {
#ifdef CONFIG_USB_PD_ALT_MODE_DFP
		case CMD_DISCOVER_IDENT:
			/* Received a SOP Prime Discover Ident msg */
			if (is_transmit_msg_sop_prime(port)) {
				/* Store cable type */
				dfp_consume_cable_response(port, cnt, payload);
				disable_transmit_sop_prime(port);
				rsize = dfp_discover_svids(payload);
			/* Received a SOP Discover Ident Message */
			} else if (IS_ENABLED(CONFIG_USB_PD_DECODE_SOP)) {
				dfp_consume_identity(port, cnt, payload);
				/* Send SOP' Discover Ident message */
				if (!cable[port].is_identified) {
					rsize = dfp_discover_ident(payload);
					enable_transmit_sop_prime(port);
				}
			} else {
				dfp_consume_identity(port, cnt, payload);
				rsize = dfp_discover_svids(payload);
			}
#ifdef CONFIG_CHARGE_MANAGER
			if (pd_charge_from_device(pd_get_identity_vid(port),
						  pd_get_identity_pid(port)))
				charge_manager_update_dualrole(port,
							       CAP_DEDICATED);
#endif
			break;
		case CMD_DISCOVER_SVID:
			dfp_consume_svids(port, cnt, payload);
			rsize = dfp_discover_modes(port, payload);
			break;
		case CMD_DISCOVER_MODES:
			dfp_consume_modes(port, cnt, payload);
			rsize = dfp_discover_modes(port, payload);
			/* enter the default mode for DFP */
			if (!rsize) {
				payload[0] = pd_dfp_enter_mode(port, 0, 0);
				if (payload[0])
					rsize = 1;
			}
			break;
		case CMD_ENTER_MODE:
			if (!modep) {
				rsize = 0;
			} else {
				if (!modep->opos)
					pd_dfp_enter_mode(port, 0, 0);

				if (modep->opos) {
					rsize = modep->fx->status(port,
								  payload);
					payload[0] |= PD_VDO_OPOS(modep->opos);
				}
			}
			break;
		case CMD_DP_STATUS:
			/* DP status response & UFP's DP attention have same
			   payload */
			dfp_consume_attention(port, payload);
			if (modep && modep->opos)
				rsize = modep->fx->config(port, payload);
			else
				rsize = 0;
			break;
		case CMD_DP_CONFIG:
			if (modep && modep->opos && modep->fx->post_config)
				modep->fx->post_config(port);
			/* no response after DFPs ack */
			rsize = 0;
			break;
		case CMD_EXIT_MODE:
			/* no response after DFPs ack */
			rsize = 0;
			break;
#endif
		case CMD_ATTENTION:
			/* no response after DFPs ack */
			rsize = 0;
			break;
		default:
			CPRINTF("ERR:CMD:%d\n", cmd);
			rsize = 0;
		}

		payload[0] |= VDO_CMDT(CMDT_INIT);
		payload[0] |= VDO_SVDM_VERS(pd_get_vdo_ver(port));
#ifdef CONFIG_USB_PD_ALT_MODE_DFP
	} else if (cmd_type == CMDT_RSP_BUSY) {
		switch (cmd) {
		case CMD_DISCOVER_IDENT:
		case CMD_DISCOVER_SVID:
		case CMD_DISCOVER_MODES:
			/* resend if its discovery */
			rsize = 1;
			break;
		case CMD_ENTER_MODE:
			/* Error */
			CPRINTF("ERR:ENTBUSY\n");
			rsize = 0;
			break;
		case CMD_EXIT_MODE:
			rsize = 0;
			break;
		default:
			rsize = 0;
		}
	} else if (cmd_type == CMDT_RSP_NAK) {
		rsize = 0;
		/* Send SOP' Discover Ident message, if not already received. */
		if (IS_ENABLED(CONFIG_USB_PD_DECODE_SOP) &&
		    !cable[port].is_identified && (cmd == CMD_DISCOVER_IDENT)) {
			rsize = dfp_discover_ident(payload);
			enable_transmit_sop_prime(port);
		}
#endif /* CONFIG_USB_PD_ALT_MODE_DFP */
	} else {
		CPRINTF("ERR:CMDT:%d\n", cmd);
		/* do not answer */
		rsize = 0;
	}
	return rsize;
}

#else

int pd_svdm(int port, int cnt, uint32_t *payload, uint32_t **rpayload)
{
	return 0;
}

#endif /* CONFIG_USB_PD_ALT_MODE */

#ifdef CONFIG_CMD_USB_PD_CABLE
static const char * const cable_type[] = {
	[IDH_PTYPE_PCABLE] = "Passive",
	[IDH_PTYPE_ACABLE] = "Active",
};

static const char * const cable_curr[] = {
	[CABLE_CURRENT_3A] = "3A",
	[CABLE_CURRENT_5A] = "5A",
};

static const char * const cable_ss_support[] = {
	[USB_SS_U2_ONLY] = "Not supported",
	[USB_SS_U31_GEN1] = "Gen 1",
	[USB_SS_U31_GEN2] = "Gen 1 and Gen 2",
};

static const char * const vbus_max[] = {
	[CABLE_VBUS_20V] = "20V",
	[CABLE_VBUS_30V] = "30V",
	[CABLE_VBUS_40V] = "40V",
	[CABLE_VBUS_50V] = "50V",
};
static const char * const conn_type[] = {
	[CONNECTOR_ATYPE] = "Type A",
	[CONNECTOR_BTYPE] = "Type B",
	[CONNECTOR_CTYPE] = "Type C",
	[CONNECTOR_CAPTIVE] = "Captive",
};

static int command_cable(int argc, char **argv)
{
	int port;
	char *e;

	if (argc < 2)
		return EC_ERROR_PARAM_COUNT;
	port = strtoi(argv[1], &e, 0);
	if (*e || port >= CONFIG_USB_PD_PORT_COUNT)
		return EC_ERROR_PARAM2;

	if (!cable[port].is_identified) {
		ccprintf("Cable not identified.\n");
		return EC_SUCCESS;
	}

	ccprintf("Cable Type: ");
	if (cable[port].type != IDH_PTYPE_PCABLE &&
	    cable[port].type != IDH_PTYPE_ACABLE) {
		ccprintf("Not Emark Cable\n");
		return EC_SUCCESS;
	}
	ccprintf("%s\n", cable_type[cable[port].type]);

	/*
	 * For rev 2.0, rev 3.0 active and passive cables have same bits for
	 * connector type (Bit 19:18) and current handling capability bit 6:5
	 */
	ccprintf("Connector Type: %s\n",
		cable[port].attr.rev20.connector > ARRAY_SIZE(conn_type) ?
		      "Invalid" : conn_type[cable[port].attr.rev20.connector]);

	if (cable[port].attr.rev20.current) {
		ccprintf("Cable Current: %s\n",
		      cable[port].attr.rev20.current > ARRAY_SIZE(cable_curr) ?
		      "Invalid" : cable_curr[cable[port].attr.rev20.current]);
	} else
		ccprintf("Cable Current: Invalid\n");

	/*
	 * For Rev 3.0 passive cables and Rev 2.0 active and passive cables,
	 * USB Superspeed Signaling support have same bits 2:0
	 */
	if (cable[port].type == IDH_PTYPE_PCABLE) {
		ccprintf("USB Superspeed Signaling support: %s\n",
			cable[port].attr.rev20.ss >
				ARRAY_SIZE(cable_ss_support) ? "Invalid" :
				cable_ss_support[cable[port].attr.p_rev30.ss]);
	}

	/*
	 * For Rev 3.0 active cables and Rev 2.0 active and passive cables,
	 * SOP" controller preset have same bit 3
	 */
	if (cable[port].type == IDH_PTYPE_ACABLE) {
		ccprintf("SOP' ' Controller: %s present\n",
			cable[port].attr.rev20.controller ? "" : "Not");
	}

	if (cable[port].rev == PD_REV30) {
		/*
		 * For Rev 3.0 active and passive cables, Max Vbus vtg have
		 * same bits 10:9.
		 */
		ccprintf("Max vbus voltage: %s\n",
			cable[port].attr.p_rev30.vbus_max >
				ARRAY_SIZE(vbus_max) ? "Invaild" :
				vbus_max[cable[port].attr.p_rev30.vbus_max]);

		/* For Rev 3.0 Active cables */
		if (cable[port].type == IDH_PTYPE_ACABLE) {
			ccprintf("SS signaling: USB_SS_GEN%u\n",
					cable[port].attr2.a2_rev30.sss ? 2 : 1);
			ccprintf("Number of SS lanes supported: %u\n",
					cable[port].attr2.a2_rev30.lanes);
		}
	}
	return EC_SUCCESS;
}

DECLARE_CONSOLE_COMMAND(pdcable, command_cable,
			"<port>",
			"Cable Characteristics");
#endif /* CONFIG_CMD_USB_PD_CABLE */

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

#ifdef CONFIG_USB_PD_ALT_MODE_DFP
static enum ec_status hc_remote_pd_discovery(struct host_cmd_handler_args *args)
{
	const uint8_t *port = args->params;
	struct ec_params_usb_pd_discovery_entry *r = args->response;

	if (*port >= CONFIG_USB_PD_PORT_COUNT)
		return EC_RES_INVALID_PARAM;

	r->vid = pd_get_identity_vid(*port);
	r->ptype = PD_IDH_PTYPE(pe[*port].identity[0]);
	/* pid only included if vid is assigned */
	if (r->vid)
		r->pid = PD_PRODUCT_PID(pe[*port].identity[2]);

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

	if (p->port >= CONFIG_USB_PD_PORT_COUNT)
		return EC_RES_INVALID_PARAM;

	/* no more to send */
	if (p->svid_idx >= pe[p->port].svid_cnt) {
		r->svid = 0;
		args->response_size = sizeof(r->svid);
		return EC_RES_SUCCESS;
	}

	r->svid = pe[p->port].svids[p->svid_idx].svid;
	r->opos = 0;
	memcpy(r->vdo, pe[p->port].svids[p->svid_idx].mode_vdo, 24);
	modep = get_modep(p->port, r->svid);

	if (modep)
		r->opos = pd_alt_mode(p->port, r->svid);

	args->response_size = sizeof(*r);
	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_USB_PD_GET_AMODE,
		     hc_remote_pd_get_amode,
		     EC_VER_MASK(0));

#endif

#define FW_RW_END (CONFIG_EC_WRITABLE_STORAGE_OFF + \
		   CONFIG_RW_STORAGE_OFF + CONFIG_RW_SIZE)

uint8_t *flash_hash_rw(void)
{
	static struct sha256_ctx ctx;

	/* re-calculate RW hash when changed as its time consuming */
	if (rw_flash_changed) {
		rw_flash_changed = 0;
		SHA256_init(&ctx);
		SHA256_update(&ctx, (void *)CONFIG_PROGRAM_MEMORY_BASE +
			      CONFIG_RW_MEM_OFF,
			      CONFIG_RW_SIZE - RSANUMBYTES);
		return SHA256_final(&ctx);
	} else {
		return ctx.buf;
	}
}

void pd_get_info(uint32_t *info_data)
{
	void *rw_hash = flash_hash_rw();

	/* copy first 20 bytes of RW hash */
	memcpy(info_data, rw_hash, 5 * sizeof(uint32_t));
	/* copy other info into data msg */
#if defined(CONFIG_USB_PD_HW_DEV_ID_BOARD_MAJOR) && \
	defined(CONFIG_USB_PD_HW_DEV_ID_BOARD_MINOR)
	info_data[5] = VDO_INFO(CONFIG_USB_PD_HW_DEV_ID_BOARD_MAJOR,
				CONFIG_USB_PD_HW_DEV_ID_BOARD_MINOR,
				ver_get_num_commits(system_get_image_copy()),
				(system_get_image_copy() != SYSTEM_IMAGE_RO));
#else
	info_data[5] = 0;
#endif
}

int pd_custom_flash_vdm(int port, int cnt, uint32_t *payload)
{
	static int flash_offset;
	int rsize = 1; /* default is just VDM header returned */

	switch (PD_VDO_CMD(payload[0])) {
	case VDO_CMD_VERSION:
		memcpy(payload + 1, &current_image_data.version, 24);
		rsize = 7;
		break;
	case VDO_CMD_REBOOT:
		/* ensure the power supply is in a safe state */
		pd_power_supply_reset(0);
		system_reset(0);
		break;
	case VDO_CMD_READ_INFO:
		/* copy info into response */
		pd_get_info(payload + 1);
		rsize = 7;
		break;
	case VDO_CMD_FLASH_ERASE:
		/* do not kill the code under our feet */
		if (system_get_image_copy() != SYSTEM_IMAGE_RO)
			break;
		pd_log_event(PD_EVENT_ACC_RW_ERASE, 0, 0, NULL);
		flash_offset = CONFIG_EC_WRITABLE_STORAGE_OFF +
			       CONFIG_RW_STORAGE_OFF;
		flash_physical_erase(CONFIG_EC_WRITABLE_STORAGE_OFF +
				     CONFIG_RW_STORAGE_OFF, CONFIG_RW_SIZE);
		rw_flash_changed = 1;
		break;
	case VDO_CMD_FLASH_WRITE:
		/* do not kill the code under our feet */
		if ((system_get_image_copy() != SYSTEM_IMAGE_RO) ||
		    (flash_offset < CONFIG_EC_WRITABLE_STORAGE_OFF +
				    CONFIG_RW_STORAGE_OFF))
			break;
		flash_physical_write(flash_offset, 4*(cnt - 1),
				     (const char *)(payload+1));
		flash_offset += 4*(cnt - 1);
		rw_flash_changed = 1;
		break;
	case VDO_CMD_ERASE_SIG:
		/* this is not touching the code area */
		{
			uint32_t zero = 0;
			int offset;
			/* zeroes the area containing the RSA signature */
			for (offset = FW_RW_END - RSANUMBYTES;
			     offset < FW_RW_END; offset += 4)
				flash_physical_write(offset, 4,
						     (const char *)&zero);
		}
		break;
	default:
		/* Unknown : do not answer */
		return 0;
	}
	return rsize;
}

#ifdef CONFIG_USB_PD_DISCHARGE
void pd_set_vbus_discharge(int port, int enable)
{
	static struct mutex discharge_lock[CONFIG_USB_PD_PORT_COUNT];

	mutex_lock(&discharge_lock[port]);
	enable &= !board_vbus_source_enabled(port);
#ifdef CONFIG_USB_PD_DISCHARGE_GPIO
	if (!port)
		gpio_set_level(GPIO_USB_C0_DISCHARGE, enable);
#if CONFIG_USB_PD_PORT_COUNT > 1
	else
		gpio_set_level(GPIO_USB_C1_DISCHARGE, enable);
#endif /* CONFIG_USB_PD_PORT_COUNT */
#elif defined(CONFIG_USB_PD_DISCHARGE_TCPC)
	tcpc_discharge_vbus(port, enable);
#elif defined(CONFIG_USB_PD_DISCHARGE_PPC)
	ppc_discharge_vbus(port, enable);
#else
#error "PD discharge implementation not defined"
#endif
	mutex_unlock(&discharge_lock[port]);
}
#endif /* CONFIG_USB_PD_DISCHARGE */
