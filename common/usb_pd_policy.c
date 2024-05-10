/* Copyright 2014 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* LCOV_EXCL_START - TCPMv1 is difficult to meaningfully test: b/304349098. */

#include "atomic.h"
#include "builtin/assert.h"
#include "charge_manager.h"
#include "chipset.h"
#include "common.h"
#include "console.h"
#include "cros_version.h"
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
#include "tcpm/tcpm.h"
#include "timer.h"
#include "typec_control.h"
#include "usb_api.h"
#include "usb_common.h"
#include "usb_mux.h"
#include "usb_pd.h"
#include "usb_pd_tcpm.h"
#include "usbc_ppc.h"
#include "util.h"

#ifdef CONFIG_COMMON_RUNTIME
#define CPRINTS(format, args...) cprints(CC_USBPD, format, ##args)
#define CPRINTF(format, args...) cprintf(CC_USBPD, format, ##args)
#else
#define CPRINTS(format, args...)
#define CPRINTF(format, args...)
#endif

/*
 * This file is currently only used for TCPMv1, and would need changes before
 * being used for TCPMv2. One example: PD_FLAGS_* are TCPMv1 only.
 */
#ifndef CONFIG_USB_PD_TCPMV1
#error This file must only be used with TCPMv1
#endif

#ifdef CONFIG_USB_PD_ALT_MODE_DFP
#ifndef PORT_TO_HPD
#define PORT_TO_HPD(port) ((port) ? GPIO_USB_C1_DP_HPD : GPIO_USB_C0_DP_HPD)
#endif /* PORT_TO_HPD */

/* Tracker for which task is waiting on sysjump prep to finish */
static volatile task_id_t sysjump_task_waiting = TASK_ID_INVALID;

/*
 * timestamp of the next possible toggle to ensure the 2-ms spacing
 * between IRQ_HPD.  Since this is used in overridable functions, this
 * has to be global.
 */
uint64_t svdm_hpd_deadline[CONFIG_USB_PD_PORT_MAX_COUNT];

int dp_flags[CONFIG_USB_PD_PORT_MAX_COUNT];

uint32_t dp_status[CONFIG_USB_PD_PORT_MAX_COUNT];

/* Console command multi-function preference set for a PD port. */

__maybe_unused bool dp_port_mf_allow[CONFIG_USB_PD_PORT_MAX_COUNT] = {
	[0 ... CONFIG_USB_PD_PORT_MAX_COUNT - 1] = true
};

__overridable const struct svdm_response svdm_rsp = {
	.identity = NULL,
	.svids = NULL,
	.modes = NULL,
};
#endif /* CONFIG_USB_PD_ALT_MODE_DFP */

static int rw_flash_changed = 1;

__overridable void pd_check_pr_role(int port, enum pd_power_role pr_role,
				    int flags)
{
	/*
	 * If partner is dual-role power and dualrole toggling is on, consider
	 * if a power swap is necessary.
	 */
	if ((flags & PD_FLAGS_PARTNER_DR_POWER) &&
	    pd_get_dual_role(port) == PD_DRP_TOGGLE_ON) {
		/*
		 * If we are a sink and partner is not unconstrained, then
		 * swap to become a source. If we are source and partner is
		 * unconstrained, swap to become a sink.
		 */
		int partner_unconstrained = flags & PD_FLAGS_PARTNER_UNCONSTR;

		if ((!partner_unconstrained && pr_role == PD_ROLE_SINK) ||
		    (partner_unconstrained && pr_role == PD_ROLE_SOURCE))
			pd_request_power_swap(port);
	}
}

__overridable void pd_check_dr_role(int port, enum pd_data_role dr_role,
				    int flags)
{
	/* If UFP, try to switch to DFP */
	if ((flags & PD_FLAGS_PARTNER_DR_DATA) && dr_role == PD_ROLE_UFP)
		pd_request_data_swap(port);
}

/* Last received source cap */
static uint32_t pd_src_caps[CONFIG_USB_PD_PORT_MAX_COUNT][PDO_MAX_OBJECTS];
static uint8_t pd_src_cap_cnt[CONFIG_USB_PD_PORT_MAX_COUNT];

const uint32_t *const pd_get_src_caps(int port)
{
	return pd_src_caps[port];
}

void pd_set_src_caps(int port, int cnt, uint32_t *src_caps)
{
	int i;

	pd_src_cap_cnt[port] = cnt;

	for (i = 0; i < cnt; i++)
		pd_src_caps[port][i] = *src_caps++;
}

uint8_t pd_get_src_cap_cnt(int port)
{
	return pd_src_cap_cnt[port];
}

#ifdef CONFIG_USB_PD_ALT_MODE

#ifdef CONFIG_USB_PD_ALT_MODE_DFP

static struct pd_discovery discovery[CONFIG_USB_PD_PORT_MAX_COUNT]
				    [DISCOVERY_TYPE_COUNT];
static struct partner_active_modes partner_amodes[CONFIG_USB_PD_PORT_MAX_COUNT]
						 [AMODE_TYPE_COUNT];

void pd_dfp_discovery_init(int port)
{
	memset(&discovery[port], 0, sizeof(struct pd_discovery));
}

void pd_dfp_mode_init(int port)
{
	memset(&partner_amodes[port], 0, sizeof(partner_amodes[0]));
}

static int dfp_discover_svids(uint32_t *payload)
{
	payload[0] = VDO(USB_SID_PD, 1, CMD_DISCOVER_SVID);
	return 1;
}

struct pd_discovery *
pd_get_am_discovery_and_notify_access(int port, enum tcpci_msg_type type)
{
	return (struct pd_discovery *)pd_get_am_discovery(port, type);
}

const struct pd_discovery *pd_get_am_discovery(int port,
					       enum tcpci_msg_type type)
{
	return &discovery[port][type];
}

struct partner_active_modes *
pd_get_partner_active_modes(int port, enum tcpci_msg_type type)
{
	assert(type < AMODE_TYPE_COUNT);
	return &partner_amodes[port][type];
}

/* Note: Enter mode flag is not needed by TCPMv1 */
void pd_set_dfp_enter_mode_flag(int port, bool set)
{
}

/**
 * Return the discover alternate mode payload data
 *
 * @param port    USB-C port number
 * @param payload Pointer to payload data to fill
 * @return 1 if valid SVID present else 0
 */
static int dfp_discover_modes(int port, uint32_t *payload)
{
	const struct pd_discovery *disc =
		pd_get_am_discovery(port, TCPCI_MSG_SOP);
	uint16_t svid = disc->svids[disc->svid_idx].svid;

	if (disc->svid_idx >= disc->svid_cnt)
		return 0;

	payload[0] = VDO(svid, 1, CMD_DISCOVER_MODES);

	return 1;
}

static int process_am_discover_ident_sop(int port, int cnt, uint32_t head,
					 uint32_t *payload,
					 enum tcpci_msg_type *rtype)
{
	pd_dfp_discovery_init(port);
	pd_dfp_mode_init(port);
	dfp_consume_identity(port, TCPCI_MSG_SOP, cnt, payload);

	return dfp_discover_svids(payload);
}

static int process_am_discover_svids(int port, int cnt, uint32_t *payload,
				     enum tcpci_msg_type sop,
				     enum tcpci_msg_type *rtype)
{
	/*
	 * The pd_discovery structure stores SOP and SOP' discovery results
	 * separately, but TCPMv1 depends on one-dimensional storage of SVIDs
	 * and modes. Therefore, always use TCPCI_MSG_SOP in TCPMv1.
	 */
	dfp_consume_svids(port, sop, cnt, payload);

	return dfp_discover_modes(port, payload);
}
#endif /* CONFIG_USB_PD_ALT_MODE_DFP */

int pd_svdm(int port, int cnt, uint32_t *payload, uint32_t **rpayload,
	    uint32_t head, enum tcpci_msg_type *rtype)
{
	int cmd = PD_VDO_CMD(payload[0]);
	int cmd_type = PD_VDO_CMDT(payload[0]);
	int (*func)(int port, uint32_t *payload) = NULL;

	int rsize = 1; /* VDM header at a minimum */

#ifdef CONFIG_USB_PD_ALT_MODE_DFP
	enum tcpci_msg_type sop = PD_HEADER_GET_SOP(head);
#endif

	/* Transmit SOP messages by default */
	*rtype = TCPCI_MSG_SOP;

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
		payload[0] |= VDO_SVDM_VERS_MAJOR(
			pd_get_vdo_ver(port, TCPCI_MSG_SOP));
	} else if (cmd_type == CMDT_RSP_ACK) {
#ifdef CONFIG_USB_PD_ALT_MODE_DFP
		struct svdm_amode_data *modep;

		modep = pd_get_amode_data(port, TCPCI_MSG_SOP,
					  PD_VDO_VID(payload[0]));
#endif
		switch (cmd) {
#ifdef CONFIG_USB_PD_ALT_MODE_DFP
		case CMD_DISCOVER_IDENT:
			/* Received a SOP Discover Ident Message */
			rsize = process_am_discover_ident_sop(port, cnt, head,
							      payload, rtype);
			break;
		case CMD_DISCOVER_SVID:
			rsize = process_am_discover_svids(port, cnt, payload,
							  sop, rtype);
			break;
		case CMD_DISCOVER_MODES:
			dfp_consume_modes(port, sop, cnt, payload);

			rsize = dfp_discover_modes(port, payload);
			/* enter the default mode for DFP */
			if (!rsize) {
				payload[0] = pd_dfp_enter_mode(
					port, TCPCI_MSG_SOP, 0, 0);
				if (payload[0])
					rsize = 1;
			}
			break;
		case CMD_ENTER_MODE:
			if (!modep) {
				rsize = 0;
			} else {
				if (!modep->opos)
					pd_dfp_enter_mode(port, TCPCI_MSG_SOP,
							  0, 0);

				if (modep->opos) {
					rsize = modep->fx->status(port,
								  payload);
					payload[0] |= PD_VDO_OPOS(modep->opos);
				}
			}
			break;
		case CMD_DP_STATUS:
			/*
			 * Note: DP status response & UFP's DP attention have
			 * the same payload
			 */
			dfp_consume_attention(port, payload);

			if (modep && modep->opos) {
				/*
				 * Place the USB Type-C pins that are to be
				 * re-configured to DisplayPort Configuration
				 * into the Safe state. For USB_PD_MUX_DOCK,
				 * the superspeed signals can remain connected.
				 * For USB_PD_MUX_DP_ENABLED, disconnect the
				 * superspeed signals here, before the pins are
				 * re-configured to DisplayPort (in
				 * svdm_dp_post_config, when we receive the
				 * config ack).
				 */
				if (svdm_dp_get_mux_mode(port) ==
				    USB_PD_MUX_DP_ENABLED)
					usb_mux_set_safe_mode(port);
				rsize = modep->fx->config(port, payload);
			} else {
				rsize = 0;
			}
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
		payload[0] |= VDO_SVDM_VERS_MAJOR(
			pd_get_vdo_ver(port, TCPCI_MSG_SOP));
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
#endif /* CONFIG_USB_PD_ALT_MODE_DFP */
	} else {
		CPRINTF("ERR:CMDT:%d\n", cmd);
		/* do not answer */
		rsize = 0;
	}
	return rsize;
}

#else

int pd_svdm(int port, int cnt, uint32_t *payload, uint32_t **rpayload,
	    uint32_t head, enum tcpci_msg_type *rtype)
{
	return 0;
}

#endif /* CONFIG_USB_PD_ALT_MODE */

#define FW_RW_END                                                 \
	(CONFIG_EC_WRITABLE_STORAGE_OFF + CONFIG_RW_STORAGE_OFF + \
	 CONFIG_RW_SIZE)

uint8_t *flash_hash_rw(void)
{
	static struct sha256_ctx ctx;

	/* re-calculate RW hash when changed as its time consuming */
	if (rw_flash_changed) {
		rw_flash_changed = 0;
		SHA256_init(&ctx);
		SHA256_update(&ctx,
			      (void *)CONFIG_PROGRAM_MEMORY_BASE +
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
				(system_get_image_copy() != EC_IMAGE_RO));
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
		if (system_get_image_copy() != EC_IMAGE_RO)
			break;
		pd_log_event(PD_EVENT_ACC_RW_ERASE, 0, 0, NULL);
		flash_offset =
			CONFIG_EC_WRITABLE_STORAGE_OFF + CONFIG_RW_STORAGE_OFF;
		crec_flash_physical_erase(CONFIG_EC_WRITABLE_STORAGE_OFF +
						  CONFIG_RW_STORAGE_OFF,
					  CONFIG_RW_SIZE);
		rw_flash_changed = 1;
		break;
	case VDO_CMD_FLASH_WRITE:
		/* do not kill the code under our feet */
		if ((system_get_image_copy() != EC_IMAGE_RO) ||
		    (flash_offset <
		     CONFIG_EC_WRITABLE_STORAGE_OFF + CONFIG_RW_STORAGE_OFF))
			break;
		crec_flash_physical_write(flash_offset, 4 * (cnt - 1),
					  (const char *)(payload + 1));
		flash_offset += 4 * (cnt - 1);
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
				crec_flash_physical_write(offset, 4,
							  (const char *)&zero);
		}
		break;
	default:
		/* Unknown : do not answer */
		return 0;
	}
	return rsize;
}

#ifdef CONFIG_USB_PD_ALT_MODE_DFP
static enum ec_status hc_remote_pd_set_amode(struct host_cmd_handler_args *args)
{
	const struct ec_params_usb_pd_set_mode_request *p = args->params;

	if ((p->port >= board_get_usb_pd_port_count()) || (!p->svid) ||
	    (!p->opos))
		return EC_RES_INVALID_PARAM;

	switch (p->cmd) {
	case PD_EXIT_MODE:
		if (pd_dfp_exit_mode(p->port, TCPCI_MSG_SOP, p->svid, p->opos))
			pd_send_vdm(p->port, p->svid,
				    CMD_EXIT_MODE | VDO_OPOS(p->opos), NULL, 0);
		else {
			CPRINTF("Failed exit mode\n");
			return EC_RES_ERROR;
		}
		break;
	case PD_ENTER_MODE:
		if (pd_dfp_enter_mode(p->port, TCPCI_MSG_SOP, p->svid, p->opos))
			pd_send_vdm(p->port, p->svid,
				    CMD_ENTER_MODE | VDO_OPOS(p->opos), NULL,
				    0);
		break;
	default:
		return EC_RES_INVALID_PARAM;
	}
	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_USB_PD_SET_AMODE, hc_remote_pd_set_amode,
		     EC_VER_MASK(0));

const uint32_t *pd_get_mode_vdo(int port, uint16_t svid_idx,
				enum tcpci_msg_type type)
{
	const struct pd_discovery *disc = pd_get_am_discovery(port, type);

	return disc->svids[svid_idx].mode_vdo;
}

static enum ec_status hc_remote_pd_get_amode(struct host_cmd_handler_args *args)
{
	struct svdm_amode_data *modep;
	const struct ec_params_usb_pd_get_mode_request *p = args->params;
	struct ec_params_usb_pd_get_mode_response *r = args->response;

	if (p->port >= board_get_usb_pd_port_count())
		return EC_RES_INVALID_PARAM;

	/* no more to send */
	/* TODO(b/148528713): Use TCPMv2's separate storage for SOP'. */
	if (p->svid_idx >= pd_get_svid_count(p->port, TCPCI_MSG_SOP)) {
		r->svid = 0;
		args->response_size = sizeof(r->svid);
		return EC_RES_SUCCESS;
	}

	r->svid = pd_get_svid(p->port, p->svid_idx, TCPCI_MSG_SOP);
	r->opos = 0;
	memcpy(r->vdo, pd_get_mode_vdo(p->port, p->svid_idx, TCPCI_MSG_SOP),
	       sizeof(uint32_t) * VDO_MAX_OBJECTS);
	modep = pd_get_amode_data(p->port, TCPCI_MSG_SOP, r->svid);

	if (modep)
		r->opos = pd_alt_mode(p->port, TCPCI_MSG_SOP, r->svid);

	args->response_size = sizeof(*r);
	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_USB_PD_GET_AMODE, hc_remote_pd_get_amode,
		     EC_VER_MASK(0));

static int pd_get_mode_idx(int port, enum tcpci_msg_type type, uint16_t svid)
{
	int amode_idx;
	struct partner_active_modes *active =
		pd_get_partner_active_modes(port, type);

	for (amode_idx = 0; amode_idx < PD_AMODE_COUNT; amode_idx++) {
		if (active->amodes[amode_idx].fx &&
		    (active->amodes[amode_idx].fx->svid == svid))
			return amode_idx;
	}
	return -1;
}

static int pd_allocate_mode(int port, enum tcpci_msg_type type, uint16_t svid)
{
	int i, j;
	struct svdm_amode_data *modep;
	int mode_idx = pd_get_mode_idx(port, type, svid);
	const struct pd_discovery *disc = pd_get_am_discovery(port, type);
	struct partner_active_modes *active =
		pd_get_partner_active_modes(port, type);
	assert(active);

	if (mode_idx != -1)
		return mode_idx;

	/* There's no space to enter another mode */
	if (active->amode_idx == PD_AMODE_COUNT) {
		CPRINTF("ERR:NO AMODE SPACE\n");
		return -1;
	}

	/* Allocate ...  if SVID == 0 enter default supported policy */
	for (i = 0; i < supported_modes_cnt; i++) {
		for (j = 0; j < disc->svid_cnt; j++) {
			const struct svid_mode_data *svidp = &disc->svids[j];

			/*
			 * Looking for a match between supported_modes and
			 * discovered SVIDs; must also match the passed-in SVID
			 * if that was non-zero. Otherwise, go to the next
			 * discovered SVID.
			 * TODO(b/155890173): Support AP-directed mode entry
			 * where the mode is unknown to the TCPM.
			 */
			if ((svidp->svid != supported_modes[i].svid) ||
			    (svid && (svidp->svid != svid)))
				continue;

			modep = &active->amodes[active->amode_idx];
			modep->fx = &supported_modes[i];
			modep->data = &disc->svids[j];
			active->amode_idx++;
			return active->amode_idx - 1;
		}
	}
	return -1;
}

static int validate_mode_request(struct svdm_amode_data *modep, uint16_t svid,
				 int opos)
{
	if (!modep->fx)
		return 0;

	if (svid != modep->fx->svid) {
		CPRINTF("ERR:svid r:0x%04x != c:0x%04x\n", svid,
			modep->fx->svid);
		return 0;
	}

	if (opos != modep->opos) {
		CPRINTF("ERR:opos r:%d != c:%d\n", opos, modep->opos);
		return 0;
	}

	return 1;
}

void pd_prepare_sysjump(void)
{
	int i;

	/* Exit modes before sysjump so we can cleanly enter again later */
	for (i = 0; i < board_get_usb_pd_port_count(); i++) {
		/*
		 * If the port is not capable of Alternate mode no need to
		 * send the event.
		 */
		if (!pd_alt_mode_capable(i))
			continue;

		sysjump_task_waiting = task_get_current();
		task_set_event(PD_PORT_TO_TASK_ID(i), PD_EVENT_SYSJUMP);
		task_wait_event_mask(TASK_EVENT_SYSJUMP_READY, -1);
		sysjump_task_waiting = TASK_ID_INVALID;
	}
}

#ifdef CONFIG_USB_PD_DP_MODE
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
	struct svdm_amode_data *modep =
		pd_get_amode_data(port, TCPCI_MSG_SOP, USB_SID_DISPLAYPORT);
	uint32_t mode_caps;
	uint32_t pin_caps;
	int mf_pref;

	/*
	 * Default dp_port_mf_allow is true, we allow mf operation
	 * if UFP_D supports it.
	 */

	if (IS_ENABLED(CONFIG_CMD_MFALLOW))
		mf_pref = PD_VDO_DPSTS_MF_PREF(dp_status[port]) &&
			  dp_port_mf_allow[port];
	else
		mf_pref = PD_VDO_DPSTS_MF_PREF(dp_status[port]);

	if (!modep)
		return 0;

	mode_caps = modep->data->mode_vdo[modep->opos - 1];

	/* TODO(crosbug.com/p/39656) revisit with DFP that can be a sink */
	pin_caps = PD_DP_PIN_CAPS(mode_caps);

	/* if don't want multi-function then ignore those pin configs */
	if (!mf_pref)
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
#endif /* CONFIG_USB_PD_DP_MODE */

struct svdm_amode_data *pd_get_amode_data(int port, enum tcpci_msg_type type,
					  uint16_t svid)
{
	int idx = pd_get_mode_idx(port, type, svid);
	struct partner_active_modes *active =
		pd_get_partner_active_modes(port, type);
	assert(active);

	return (idx == -1) ? NULL : &active->amodes[idx];
}

/*
 * Enter default mode ( payload[0] == 0 ) or attempt to enter mode via svid &
 * opos
 */
uint32_t pd_dfp_enter_mode(int port, enum tcpci_msg_type type, uint16_t svid,
			   int opos)
{
	int mode_idx = pd_allocate_mode(port, type, svid);
	struct svdm_amode_data *modep;
	uint32_t mode_caps;

	if (mode_idx == -1)
		return 0;
	modep = &pd_get_partner_active_modes(port, type)->amodes[mode_idx];

	if (!opos) {
		/* choose the lowest as default */
		modep->opos = 1;
	} else if (opos <= modep->data->mode_cnt) {
		modep->opos = opos;
	} else {
		CPRINTS("C%d: Invalid opos %d for SVID %x", port, opos, svid);
		return 0;
	}

	mode_caps = modep->data->mode_vdo[modep->opos - 1];
	if (modep->fx->enter(port, mode_caps) == -1)
		return 0;

	/*
	 * Strictly speaking, this should only happen when the request
	 * has been ACKed.
	 * For TCPMV1, still set modal flag pre-emptively. For TCPMv2, the modal
	 * flag is set when the ENTER command is ACK'd for each alt mode that is
	 * supported.
	 */
	if (IS_ENABLED(CONFIG_USB_PD_TCPMV1))
		pd_set_dfp_enter_mode_flag(port, true);

	/* SVDM to send to UFP for mode entry */
	return VDO(modep->fx->svid, 1, CMD_ENTER_MODE | VDO_OPOS(modep->opos));
}

int pd_dfp_exit_mode(int port, enum tcpci_msg_type type, uint16_t svid,
		     int opos)
{
	struct svdm_amode_data *modep;
	struct partner_active_modes *active =
		pd_get_partner_active_modes(port, type);
	int idx;

	/*
	 * Empty svid signals we should reset DFP VDM state by exiting all
	 * entered modes then clearing state.  This occurs when we've
	 * disconnected or for hard reset.
	 */
	if (!svid) {
		for (idx = 0; idx < PD_AMODE_COUNT; idx++)
			if (active->amodes[idx].fx)
				active->amodes[idx].fx->exit(port);

		pd_dfp_mode_init(port);
		return 0;
	}

	/*
	 * TODO(crosbug.com/p/33946) : below needs revisited to allow multiple
	 * mode exit.  Additionally it should honor OPOS == 7 as DFP's request
	 * to exit all modes.  We currently don't have any UFPs that support
	 * multiple modes on one SVID.
	 */
	modep = pd_get_amode_data(port, type, svid);
	if (!modep || !validate_mode_request(modep, svid, opos))
		return 0;

	/* call DFPs exit function */
	modep->fx->exit(port);

	pd_set_dfp_enter_mode_flag(port, false);

	/* exit the mode */
	modep->opos = 0;
	return 1;
}

void dfp_consume_attention(int port, uint32_t *payload)
{
	uint16_t svid = PD_VDO_VID(payload[0]);
	int opos = PD_VDO_OPOS(payload[0]);
	struct svdm_amode_data *modep =
		pd_get_amode_data(port, TCPCI_MSG_SOP, svid);

	if (!modep || !validate_mode_request(modep, svid, opos))
		return;

	if (modep->fx->attention)
		modep->fx->attention(port, payload);
}

int pd_alt_mode(int port, enum tcpci_msg_type type, uint16_t svid)
{
	struct svdm_amode_data *modep = pd_get_amode_data(port, type, svid);

	return (modep) ? modep->opos : -1;
}

void notify_sysjump_ready(void)
{
	/*
	 * If event was set from pd_prepare_sysjump, wake the
	 * task waiting on us to complete.
	 */
	if (sysjump_task_waiting != TASK_ID_INVALID)
		task_set_event(sysjump_task_waiting, TASK_EVENT_SYSJUMP_READY);
}

#ifdef CONFIG_USB_PD_DP_MODE
__overridable void svdm_safe_dp_mode(int port)
{
	/* make DP interface safe until configure */
	dp_flags[port] = 0;
	dp_status[port] = 0;

	usb_mux_set_safe_mode(port);
}

__overridable int svdm_enter_dp_mode(int port, uint32_t mode_caps)
{
	/*
	 * Don't enter the mode if the SoC is off.
	 *
	 * There's no need to enter the mode while the SoC is off; we'll
	 * actually enter the mode on the chipset resume hook.  Entering DP Alt
	 * Mode twice will confuse some monitors and require and unplug/replug
	 * to get them to work again.  The DP Alt Mode on USB-C spec says that
	 * if we don't need to maintain HPD connectivity info in a low power
	 * mode, then we shall exit DP Alt Mode.  (This is why we don't enter
	 * when the SoC is off as opposed to suspend where adding a display
	 * could cause a wake up.)  When in S5->S3 transition state, we
	 * should treat it as a SoC off state.
	 */
#ifdef CONFIG_AP_POWER_CONTROL
	if (!chipset_in_state(CHIPSET_STATE_ANY_SUSPEND | CHIPSET_STATE_ON))
		return -1;
#endif

		/*
		 * TCPMv2: Enable logging of CCD line state CCD_MODE_ODL.
		 * DisplayPort Alternate mode requires that the SBU lines are
		 * used for AUX communication. However, in Chromebooks SBU
		 * signals are repurposed as USB2 signals for CCD. This
		 * functionality is accomplished by override fets whose state is
		 * controlled by CCD_MODE_ODL.
		 *
		 * This condition helps in debugging unexpected AUX timeout
		 * issues by indicating the state of the CCD override fets.
		 */
#ifdef GPIO_CCD_MODE_ODL
	if (!gpio_get_level(GPIO_CCD_MODE_ODL))
		CPRINTS("WARNING: Tried to EnterMode DP with [CCD on AUX/SBU]");
#endif

	/* Only enter mode if device is DFP_D capable */
	if (mode_caps & MODE_DP_SNK) {
		svdm_safe_dp_mode(port);

		if (IS_ENABLED(CONFIG_MKBP_EVENT) &&
		    chipset_in_state(CHIPSET_STATE_ANY_SUSPEND))
			/*
			 * Wake the system up since we're entering DP AltMode.
			 */
			pd_notify_dp_alt_mode_entry(port);

		return 0;
	}

	return -1;
}

__overridable int svdm_dp_status(int port, uint32_t *payload)
{
	int opos = pd_alt_mode(port, TCPCI_MSG_SOP, USB_SID_DISPLAYPORT);

	payload[0] =
		VDO(USB_SID_DISPLAYPORT, 1, CMD_DP_STATUS | VDO_OPOS(opos));
	payload[1] = VDO_DP_STATUS(0, /* HPD IRQ  ... not applicable */
				   0, /* HPD level ... not applicable */
				   0, /* exit DP? ... no */
				   0, /* usb mode? ... no */
				   0, /* multi-function ... no */
				   (!!(dp_flags[port] & DP_FLAGS_DP_ON)),
				   0, /* power low? ... no */
				   (!!DP_FLAGS_DP_ON));
	return 2;
};

__overridable uint8_t get_dp_pin_mode(int port)
{
	return pd_dfp_dp_get_pin_mode(port, dp_status[port]);
}

mux_state_t svdm_dp_get_mux_mode(int port)
{
	int pin_mode = get_dp_pin_mode(port);
	/* Default dp_port_mf_allow is true */
	int mf_pref;

	if (IS_ENABLED(CONFIG_CMD_MFALLOW))
		mf_pref = PD_VDO_DPSTS_MF_PREF(dp_status[port]) &&
			  dp_port_mf_allow[port];
	else
		mf_pref = PD_VDO_DPSTS_MF_PREF(dp_status[port]);

	/*
	 * Multi-function operation is only allowed if that pin config is
	 * supported.
	 */
	if ((pin_mode & MODE_DP_PIN_MF_MASK) && mf_pref)
		return USB_PD_MUX_DOCK;
	else
		return USB_PD_MUX_DP_ENABLED;
}

/* Note: Assumes that pins have already been set in safe state if necessary */
__overridable int svdm_dp_config(int port, uint32_t *payload)
{
	int opos = pd_alt_mode(port, TCPCI_MSG_SOP, USB_SID_DISPLAYPORT);
	uint8_t pin_mode = get_dp_pin_mode(port);
	mux_state_t mux_mode = svdm_dp_get_mux_mode(port);
	/* Default dp_port_mf_allow is true */
	int mf_pref;

	if (IS_ENABLED(CONFIG_CMD_MFALLOW))
		mf_pref = PD_VDO_DPSTS_MF_PREF(dp_status[port]) &&
			  dp_port_mf_allow[port];
	else
		mf_pref = PD_VDO_DPSTS_MF_PREF(dp_status[port]);

	if (!pin_mode)
		return 0;

	CPRINTS("pin_mode: %x, mf: %d, mux: %d", pin_mode, mf_pref, mux_mode);

	payload[0] =
		VDO(USB_SID_DISPLAYPORT, 1, CMD_DP_CONFIG | VDO_OPOS(opos));
	payload[1] = VDO_DP_CFG(pin_mode, /* pin mode */
				1, /* DPv1.3 signaling */
				2); /* UFP connected */
	return 2;
};

#if defined(CONFIG_USB_PD_DP_HPD_GPIO) && \
	!defined(CONFIG_USB_PD_DP_HPD_GPIO_CUSTOM)
void svdm_set_hpd_gpio(int port, int en)
{
	gpio_set_level(PORT_TO_HPD(port), en);
}

int svdm_get_hpd_gpio(int port)
{
	return gpio_get_level(PORT_TO_HPD(port));
}
#endif

__overridable void svdm_dp_post_config(int port)
{
	mux_state_t mux_mode = svdm_dp_get_mux_mode(port);
	/* Connect the SBU and USB lines to the connector. */
	typec_set_sbu(port, true);

	usb_mux_set(port, mux_mode, USB_SWITCH_CONNECT,
		    polarity_rm_dts(pd_get_polarity(port)));

	dp_flags[port] |= DP_FLAGS_DP_ON;
	if (!(dp_flags[port] & DP_FLAGS_HPD_HI_PENDING))
		return;

#ifdef CONFIG_USB_PD_DP_HPD_GPIO
	svdm_set_hpd_gpio(port, 1);

	/* set the minimum time delay (2ms) for the next HPD IRQ */
	svdm_hpd_deadline[port] = get_time().val + HPD_USTREAM_DEBOUNCE_LVL;
#endif /* CONFIG_USB_PD_DP_HPD_GPIO */

	usb_mux_hpd_update(port,
			   USB_PD_MUX_HPD_LVL | USB_PD_MUX_HPD_IRQ_DEASSERTED);

#ifdef USB_PD_PORT_TCPC_MST
	if (port == USB_PD_PORT_TCPC_MST)
		baseboard_mst_enable_control(port, 1);
#endif
}

__overridable int svdm_dp_attention(int port, uint32_t *payload)
{
	int lvl = PD_VDO_DPSTS_HPD_LVL(payload[1]);
	int irq = PD_VDO_DPSTS_HPD_IRQ(payload[1]);
#ifdef CONFIG_USB_PD_DP_HPD_GPIO
	int cur_lvl = svdm_get_hpd_gpio(port);
#endif /* CONFIG_USB_PD_DP_HPD_GPIO */
	mux_state_t mux_state;

	dp_status[port] = payload[1];

	if (chipset_in_state(CHIPSET_STATE_ANY_SUSPEND) && (irq || lvl))
		/*
		 * Wake up the AP.  IRQ or level high indicates a DP sink is now
		 * present.
		 */
		if (IS_ENABLED(CONFIG_MKBP_EVENT))
			pd_notify_dp_alt_mode_entry(port);

	/* Its initial DP status message prior to config */
	if (!(dp_flags[port] & DP_FLAGS_DP_ON)) {
		if (lvl)
			dp_flags[port] |= DP_FLAGS_HPD_HI_PENDING;
		return 1;
	}

#ifdef CONFIG_USB_PD_DP_HPD_GPIO
	if (irq && !lvl) {
		/*
		 * IRQ can only be generated when the level is high, because
		 * the IRQ is signaled by a short low pulse from the high level.
		 */
		CPRINTF("ERR:HPD:IRQ&LOW\n");
		return 0; /* nak */
	}

	if (irq && cur_lvl) {
		uint64_t now = get_time().val;
		/* wait for the minimum spacing between IRQ_HPD if needed */
		if (now < svdm_hpd_deadline[port])
			crec_usleep(svdm_hpd_deadline[port] - now);

		/* generate IRQ_HPD pulse */
		svdm_set_hpd_gpio(port, 0);
		crec_usleep(HPD_DSTREAM_DEBOUNCE_IRQ);
		svdm_set_hpd_gpio(port, 1);
	} else {
		svdm_set_hpd_gpio(port, lvl);
	}

	/* set the minimum time delay (2ms) for the next HPD IRQ */
	svdm_hpd_deadline[port] = get_time().val + HPD_USTREAM_DEBOUNCE_LVL;
#endif /* CONFIG_USB_PD_DP_HPD_GPIO */

	mux_state = (lvl ? USB_PD_MUX_HPD_LVL : USB_PD_MUX_HPD_LVL_DEASSERTED) |
		    (irq ? USB_PD_MUX_HPD_IRQ : USB_PD_MUX_HPD_IRQ_DEASSERTED);
	usb_mux_hpd_update(port, mux_state);

#ifdef USB_PD_PORT_TCPC_MST
	if (port == USB_PD_PORT_TCPC_MST)
		baseboard_mst_enable_control(port, lvl);
#endif

	/* ack */
	return 1;
}

__overridable void svdm_exit_dp_mode(int port)
{
	dp_flags[port] = 0;
	dp_status[port] = 0;
#ifdef CONFIG_USB_PD_DP_HPD_GPIO
	svdm_set_hpd_gpio(port, 0);
#endif /* CONFIG_USB_PD_DP_HPD_GPIO */
	usb_mux_hpd_update(port, USB_PD_MUX_HPD_LVL_DEASSERTED |
					 USB_PD_MUX_HPD_IRQ_DEASSERTED);
#ifdef USB_PD_PORT_TCPC_MST
	if (port == USB_PD_PORT_TCPC_MST)
		baseboard_mst_enable_control(port, 0);
#endif
}
#endif /* CONFIG_USB_PD_DP_MODE */

__overridable int svdm_enter_gfu_mode(int port, uint32_t mode_caps)
{
	/* Always enter GFU mode */
	return 0;
}

__overridable void svdm_exit_gfu_mode(int port)
{
}

__overridable int svdm_gfu_status(int port, uint32_t *payload)
{
	/*
	 * This is called after enter mode is successful, send unstructured
	 * VDM to read info.
	 */
	pd_send_vdm(port, USB_VID_GOOGLE, VDO_CMD_READ_INFO, NULL, 0);
	return 0;
}

__overridable int svdm_gfu_config(int port, uint32_t *payload)
{
	return 0;
}

__overridable int svdm_gfu_attention(int port, uint32_t *payload)
{
	return 0;
}

const struct svdm_amode_fx supported_modes[] = {
#ifdef CONFIG_USB_PD_DP_MODE
	{
		.svid = USB_SID_DISPLAYPORT,
		.enter = &svdm_enter_dp_mode,
		.status = &svdm_dp_status,
		.config = &svdm_dp_config,
		.post_config = &svdm_dp_post_config,
		.attention = &svdm_dp_attention,
		.exit = &svdm_exit_dp_mode,
	},
#endif /* CONFIG_USB_PD_DP_MODE */
	{
		.svid = USB_VID_GOOGLE,
		.enter = &svdm_enter_gfu_mode,
		.status = &svdm_gfu_status,
		.config = &svdm_gfu_config,
		.attention = &svdm_gfu_attention,
		.exit = &svdm_exit_gfu_mode,
	},
};
const int supported_modes_cnt = ARRAY_SIZE(supported_modes);

#if defined(CONFIG_CMD_MFALLOW)
static int command_mfallow(int argc, const char **argv)
{
	char *e;
	int port;

	if (argc < 3)
		return EC_ERROR_PARAM_COUNT;

	port = strtoi(argv[1], &e, 10);
	if (*e || port >= board_get_usb_pd_port_count())
		return EC_ERROR_PARAM1;

	if (!strcasecmp(argv[2], "true"))
		dp_port_mf_allow[port] = true;
	else if (!strcasecmp(argv[2], "false"))
		dp_port_mf_allow[port] = false;
	else
		return EC_ERROR_PARAM2;

	ccprintf("Port: %d multi function allowed is %s ", port, argv[2]);
	return EC_SUCCESS;
}

DECLARE_CONSOLE_COMMAND(mfallow, command_mfallow, "port [true | false]",
			"Controls Multifunction choice during DP Altmode.");
#endif /* CONFIG_CMD_MFALLOW */

#ifdef CONFIG_COMMON_RUNTIME
static enum ec_status hc_remote_pd_dev_info(struct host_cmd_handler_args *args)
{
	const struct ec_params_usb_pd_info_request *p = args->params;
	struct ec_params_usb_pd_rw_hash_entry *r = args->response;
	uint16_t dev_id;
	uint32_t current_image;

	if (p->port >= board_get_usb_pd_port_count())
		return EC_RES_INVALID_PARAM;

	pd_dev_get_rw_hash(p->port, &dev_id, r->dev_rw_hash, &current_image);

	r->dev_id = dev_id;
	r->current_image = current_image;

	args->response_size = sizeof(*r);

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_USB_PD_DEV_INFO, hc_remote_pd_dev_info,
		     EC_VER_MASK(0));
#endif /* CONFIG_COMMON_RUNTIME */

#endif /* CONFIG_USB_PD_ALT_MODE_DFP */

/* LCOV_EXCL_STOP */
