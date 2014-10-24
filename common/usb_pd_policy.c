/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include "adc.h"
#include "atomic.h"
#include "common.h"
#include "console.h"
#include "gpio.h"
#include "host_command.h"
#include "registers.h"
#include "task.h"
#include "timer.h"
#include "util.h"
#include "usb_pd.h"
#include "usb_pd_config.h"
#include "version.h"

#define CPRINTS(format, args...) cprints(CC_USBPD, format, ## args)
#define CPRINTF(format, args...) cprintf(CC_USBPD, format, ## args)

#ifdef CONFIG_USB_PD_ALT_MODE

#ifdef CONFIG_USB_PD_ALT_MODE_DFP

static struct pd_policy pe[PD_PORT_COUNT];

#define AMODE_VALID(port) (pe[port].amode.index != -1)

static void pe_init(int port)
{
	memset(&pe[port], 0, sizeof(struct pd_policy));
	pe[port].amode.index = -1;
}

static void dfp_consume_identity(int port, uint32_t *payload)
{
	int ptype = PD_IDH_PTYPE(payload[VDO_I(IDH)]);
	pe_init(port);
	switch (ptype) {
	case IDH_PTYPE_AMA:
		/* TODO(tbroch) do I disable VBUS here if power contract
		 * requested it
		 */
		if (!PD_VDO_AMA_VBUS_REQ(payload[VDO_I(AMA)]))
			pd_power_supply_reset(port);
		break;
		/* TODO(crosbug.com/p/30645) provide vconn support here */
	default:
		break;
	}
}

static int dfp_discover_svids(int port, uint32_t *payload)
{
	payload[0] = VDO(USB_SID_PD, 1, CMD_DISCOVER_SVID);
	return 1;
}

static void dfp_consume_svids(int port, uint32_t *payload)
{
	int i;
	uint32_t *ptr = payload + 1;
	uint16_t svid0, svid1;

	for (i = pe[port].svid_cnt; i < pe[port].svid_cnt + 12; i += 2) {
		if (i == SVID_DISCOVERY_MAX) {
			CPRINTF("ERR: too many svids discovered\n");
			break;
		}

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
	}
	/* TODO(tbroch) need to re-issue discover svids if > 12 */
	if (i && ((i % 12) == 0))
		CPRINTF("TODO: need to re-issue discover svids > 12\n");
}

static int dfp_discover_modes(int port, uint32_t *payload)
{
	uint16_t svid = pe[port].svids[pe[port].svid_idx].svid;
	if (!pe[port].svid_cnt)
		return 0;
	payload[0] = VDO(svid, 1, CMD_DISCOVER_MODES);
	return 1;
}

static int dfp_consume_modes(int port, int cnt, uint32_t *payload)
{
	int idx = pe[port].svid_idx;
	pe[port].svids[idx].mode_cnt = cnt - 1;
	if (pe[port].svids[idx].mode_cnt < 0) {
		CPRINTF("PE ERR: no modes provided for SVID\n");
	} else {
		memcpy(pe[port].svids[pe[port].svid_idx].mode_vdo, &payload[1],
		       sizeof(uint32_t) * pe[port].svids[idx].mode_cnt);
	}

	pe[port].svid_idx++;
	return (pe[port].svid_idx < pe[port].svid_cnt);
}

/* TODO(tbroch) this function likely needs to move up the stack to where system
 * policy decisions are made. */
static int dfp_enter_mode(int port, uint32_t *payload)
{
	int i, j, done;
	struct svdm_amode_data *modep = &pe[port].amode;
	for (i = 0, done = 0; !done && (i < supported_modes_cnt); i++) {
		for (j = 0; j < pe[port].svid_cnt; j++) {
			if (pe[port].svids[j].svid != supported_modes[i].svid)
				continue;
			pe[port].amode.fx = &supported_modes[i];
			pe[port].amode.mode_caps =
				pe[port].svids[j].mode_vdo[0];
			pe[port].amode.index = 0;
			done = 1;
			break;
		}
	}
	if (!AMODE_VALID(port))
		return 0;

	if (modep->fx->enter(port, modep->mode_caps) == -1)
		return 0;

	payload[0] = VDO(modep->fx->svid, 1,
			 CMD_ENTER_MODE |
			 VDO_OPOS((modep->index + 1)));
	return 1;
}

static int dfp_consume_attention(int port, uint32_t *payload)
{
	int svid = PD_VDO_VID(payload[0]);
	int opos = PD_VDO_OPOS(payload[0]);

	if (!AMODE_VALID(port))
		return 0;
	if (svid != pe[port].amode.fx->svid) {
		CPRINTF("PE ERR: svid s:0x%04x != m:0x%04x\n",
			svid, pe[port].amode.fx->svid);
		return 0; /* NAK */
	}
	if (opos != pe[port].amode.index + 1) {
		CPRINTF("PE ERR: opos s:%d != m:%d\n",
			opos, pe[port].amode.index + 1);
		return 0; /* NAK */
	}
	if (!pe[port].amode.fx->attention)
		return 0;
	return pe[port].amode.fx->attention(port, payload);
}

int pd_exit_mode(int port, uint32_t *payload)
{
	struct svdm_amode_data *modep = &pe[port].amode;
	if (!modep->fx)
		return 0;

	modep->fx->exit(port);

	if (payload)
		payload[0] = VDO(modep->fx->svid, 1,
				 CMD_EXIT_MODE | VDO_OPOS((modep->index + 1)));
	modep->index = -1;
	return 1;
}

static void dump_pe(int port)
{
	int i, j;

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
	}
	if (!AMODE_VALID(port)) {
		ccprintf("No mode chosen yet.\n");
		return;
	}

	ccprintf("MODE[%d]: svid:%04x caps:%08x\n", pe[port].amode.index + 1,
		 pe[port].amode.fx->svid, pe[port].amode.mode_caps);
}

static int command_pe(int argc, char **argv)
{
	int port;
	char *e;
	if (argc < 3)
		return EC_ERROR_PARAM_COUNT;
	/* command: pe <port> <subcmd> <args> */
	port = strtoi(argv[1], &e, 10);
	if (*e || port >= PD_PORT_COUNT)
		return EC_ERROR_PARAM2;
	if (!strncasecmp(argv[2], "dump", 4))
		dump_pe(port);

	return EC_SUCCESS;
}

DECLARE_CONSOLE_COMMAND(pe, command_pe,
			"<port> dump",
			"USB PE",
			NULL);

#endif /* CONFIG_USB_PD_ALT_MODE_DFP */

int pd_svdm(int port, int cnt, uint32_t *payload, uint32_t **rpayload)
{
	int i;
	int cmd = PD_VDO_CMD(payload[0]);
	int cmd_type = PD_VDO_CMDT(payload[0]);
	int (*func)(int port, uint32_t *payload) = NULL;

	int rsize = 1; /* VDM header at a minimum */
	CPRINTF("SVDM/%d [%d] %08x", cnt, cmd, payload[0]);
	for (i = 1; i < cnt; i++)
		CPRINTF(" %08x", payload[i]);
	CPRINTF("\n");

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
			/* This is DFP response */
			func = &dfp_consume_attention;
			break;
#endif
		default:
			CPRINTF("PE ERR: unknown command %d\n", cmd);
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
#ifdef CONFIG_USB_PD_ALT_MODE_DFP
	} else if (cmd_type == CMDT_RSP_ACK) {
		switch (cmd) {
		case CMD_DISCOVER_IDENT:
			dfp_consume_identity(port, payload);
			rsize = dfp_discover_svids(port, payload);
			break;
		case CMD_DISCOVER_SVID:
			dfp_consume_svids(port, payload);
			rsize = dfp_discover_modes(port, payload);
			break;
		case CMD_DISCOVER_MODES:
			if (dfp_consume_modes(port, cnt, payload))
				rsize = dfp_discover_modes(port, payload);
			else
				rsize = dfp_enter_mode(port, payload);
			break;
		case CMD_ENTER_MODE:
			if (AMODE_VALID(port)) {
				rsize = pe[port].amode.fx->status(port,
								  payload);
				payload[0] |=
					VDO_OPOS((pe[port].amode.index + 1));
			} else {
				rsize = 0;
			}
			break;
		case CMD_DP_STATUS:
			/* DP status response & UFP's DP attention have same
			   payload */
			dfp_consume_attention(port, payload);
			if (AMODE_VALID(port))
				rsize = pe[port].amode.fx->config(port,
								  payload);
			else
				rsize = 0;
			break;
		case CMD_DP_CONFIG:
			/* no response after DFPs ack */
			rsize = 0;
			break;
		case CMD_EXIT_MODE:
			/* no response after DFPs ack */
			rsize = 0;
			break;
		case CMD_ATTENTION:
			/* no response after DFPs ack */
			rsize = 0;
			break;
		default:
			CPRINTF("PE ERR: unknown command %d\n", cmd);
			rsize = 0;
		}

		payload[0] |= VDO_CMDT(CMDT_INIT);
	} else if (cmd_type == CMDT_RSP_BUSY) {
		switch (cmd) {
		case CMD_DISCOVER_IDENT:
		case CMD_DISCOVER_SVID:
		case CMD_DISCOVER_MODES:
			/* resend if its discovery */
			payload[0] |= VDO_CMDT(CMDT_INIT);
			rsize = 1;
			break;
		case CMD_ENTER_MODE:
			/* Error */
			CPRINTF("PE ERR: received BUSY for Enter mode\n");
			rsize = 0;
			break;
		case CMD_EXIT_MODE:
			rsize = 0;
			break;
		default:
			rsize = 0;
		}
	} else if (cmd_type == CMDT_RSP_NAK) {
		/* nothing to do */
		rsize = 0;
#endif /* CONFIG_USB_PD_ALT_MODE_DFP */
	} else {
		CPRINTF("PE ERR: unknown cmd type %d\n", cmd);
	}
	CPRINTS("DONE");
	return rsize;
}

#else

int pd_svdm(int port, int cnt, uint32_t *payload, uint32_t **rpayload)
{
	return 0;
}

#endif /* CONFIG_USB_PD_ALT_MODE */

#ifndef CONFIG_USB_PD_CUSTOM_VDM
int pd_vdm(int port, int cnt, uint32_t *payload, uint32_t **rpayload)
{
	return 0;
}
#endif /* !CONFIG_USB_PD_CUSTOM_VDM */

#ifndef CONFIG_USB_PD_ALT_MODE_DFP
int pd_exit_mode(int port, uint32_t *payload)
{
	return 0;
}
#endif /* !CONFIG_USB_PD_ALT_MODE_DFP */
