/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include "adc.h"
#include "atomic.h"
#include "common.h"
#include "console.h"
#include "flash.h"
#include "gpio.h"
#include "hooks.h"
#include "host_command.h"
#include "registers.h"
#include "rsa.h"
#include "sha256.h"
#include "system.h"
#include "task.h"
#include "timer.h"
#include "util.h"
#include "usb_api.h"
#include "usb_pd.h"
#include "usb_pd_config.h"
#include "version.h"

#ifdef CONFIG_COMMON_RUNTIME
#define CPRINTS(format, args...) cprints(CC_USBPD, format, ## args)
#define CPRINTF(format, args...) cprintf(CC_USBPD, format, ## args)
#else
#define CPRINTS(format, args...)
#define CPRINTF(format, args...)
#endif

static int rw_flash_changed = 1;

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
	memcpy(&pe[port].identity, payload + 1, sizeof(pe[port].identity));
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

int pd_alt_mode(int port)
{
	if (!AMODE_VALID(port))
		/* zero is reserved */
		return 0;

	return pe[port].amode.index + 1;
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
			 CMD_ENTER_MODE | VDO_OPOS(pd_alt_mode(port)));
	return 1;
}

static void dfp_consume_attention(int port, uint32_t *payload)
{
	int svid = PD_VDO_VID(payload[0]);
	int opos = PD_VDO_OPOS(payload[0]);

	if (!AMODE_VALID(port))
		return;
	if (svid != pe[port].amode.fx->svid) {
		CPRINTF("PE ERR: svid s:0x%04x != m:0x%04x\n",
			svid, pe[port].amode.fx->svid);
		return;
	}
	if (opos != pd_alt_mode(port)) {
		CPRINTF("PE ERR: opos s:%d != m:%d\n",
			opos, pd_alt_mode(port));
		return;
	}
	if (pe[port].amode.fx->attention)
		pe[port].amode.fx->attention(port, payload);
}

int pd_exit_mode(int port, uint32_t *payload)
{
	struct svdm_amode_data *modep = &pe[port].amode;
	if (!modep->fx)
		return 0;

	modep->fx->exit(port);

	if (payload)
		payload[0] = VDO(modep->fx->svid, 1,
				 CMD_EXIT_MODE | VDO_OPOS(pd_alt_mode(port)));
	modep->index = -1;
	return 1;
}

static void dump_pe(int port)
{
	const char * const idh_ptype_names[]  = {
		"UNDEF", "Hub", "Periph", "PCable", "ACable", "AMA",
		"RSV6", "RSV7"};

	int i, j, idh_ptype;

	if (pe[port].identity[0] == 0) {
		ccprintf("No identity discovered yet.\n");
		return;
	}
	idh_ptype = PD_IDH_PTYPE(pe[port].identity[0]);
	ccprintf("IDENT:\n");
	ccprintf("\t[ID Header] %08x :: %s, VID:%04x\n", pe[port].identity[0],
		 idh_ptype_names[idh_ptype], PD_IDH_VID(pe[port].identity[0]));
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
	}
	if (!AMODE_VALID(port)) {
		ccprintf("No mode chosen yet.\n");
		return;
	}

	ccprintf("MODE[%d]: svid:%04x caps:%08x\n", pd_alt_mode(port),
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
			/*
			 * attention is only SVDM with no response
			 * (just goodCRC) return zero here.
			 */
			dfp_consume_attention(port, payload);
			return 0;
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
	} else if (cmd_type == CMDT_RSP_ACK) {
		switch (cmd) {
#ifdef CONFIG_USB_PD_ALT_MODE_DFP
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
					VDO_OPOS(pd_alt_mode(port));
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
#endif
		case CMD_ATTENTION:
			/* no response after DFPs ack */
			rsize = 0;
			break;
		default:
			CPRINTF("PE ERR: unknown command %d\n", cmd);
			rsize = 0;
		}

		payload[0] |= VDO_CMDT(CMDT_INIT);
#ifdef CONFIG_USB_PD_ALT_MODE_DFP
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

void pd_usb_billboard_deferred(void)
{
#if defined(CONFIG_USB_PD_ALT_MODE) && !defined(CONFIG_USB_PD_ALT_MODE_DFP) \
	&& !defined(CONFIG_USB_PD_SIMPLE_DFP)

	/* port always zero for these UFPs */
	if (!pd_alt_mode(0))
		usb_connect();

#endif
}
DECLARE_DEFERRED(pd_usb_billboard_deferred);

#ifndef CONFIG_USB_PD_ALT_MODE_DFP
int pd_exit_mode(int port, uint32_t *payload)
{
#ifdef CONFIG_USB_PD_ALT_MODE
	svdm_rsp.exit_mode(port, payload);
#endif
	return 0;
}
#endif /* !CONFIG_USB_PD_ALT_MODE_DFP */

#ifdef CONFIG_USB_PD_ALT_MODE_DFP
static int hc_remote_pd_discovery(struct host_cmd_handler_args *args)
{
	const uint8_t *port = args->params;
	struct ec_params_usb_pd_discovery_entry *r = args->response;

	if (*port >= PD_PORT_COUNT)
		return EC_RES_INVALID_PARAM;

	r->vid = PD_IDH_VID(pe[*port].identity[0]);
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
#endif

#define FW_RW_END (CONFIG_FW_RW_OFF + CONFIG_FW_RW_SIZE)

uint8_t *flash_hash_rw(void)
{
	static struct sha256_ctx ctx;

	/* re-calculate RW hash when changed as its time consuming */
	if (rw_flash_changed) {
		rw_flash_changed = 0;
		SHA256_init(&ctx);
		SHA256_update(&ctx, (void *)CONFIG_FLASH_BASE +
			      CONFIG_FW_RW_OFF,
			      CONFIG_FW_RW_SIZE - RSANUMBYTES);
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
				ver_get_numcommits(),
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
		memcpy(payload + 1, &version_data.version, 24);
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
		flash_offset = CONFIG_FW_RW_OFF;
		flash_physical_erase(CONFIG_FW_RW_OFF, CONFIG_FW_RW_SIZE);
		rw_flash_changed = 1;
		break;
	case VDO_CMD_FLASH_WRITE:
		/* do not kill the code under our feet */
		if ((system_get_image_copy() != SYSTEM_IMAGE_RO) ||
		    (flash_offset < CONFIG_FW_RW_OFF))
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
