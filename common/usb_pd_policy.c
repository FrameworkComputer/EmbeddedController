/* Copyright 2014 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "atomic.h"
#include "builtin/assert.h"
#include "charge_manager.h"
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
#include "util.h"
#include "usb_api.h"
#include "usb_common.h"
#include "usb_mux.h"
#include "usb_pd.h"
#include "usb_pd_tcpm.h"
#include "usbc_ppc.h"

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

static struct pd_cable cable[CONFIG_USB_PD_PORT_MAX_COUNT];

enum pd_rev_type get_usb_pd_cable_revision(int port)
{
	return cable[port].rev;
}

bool consume_sop_prime_repeat_msg(int port, uint8_t msg_id)
{
	if (cable[port].last_sop_p_msg_id != msg_id) {
		cable[port].last_sop_p_msg_id = msg_id;
		return false;
	}
	CPRINTF("C%d SOP Prime repeat msg_id %d\n", port, msg_id);
	return true;
}

bool consume_sop_prime_prime_repeat_msg(int port, uint8_t msg_id)
{
	if (cable[port].last_sop_p_p_msg_id != msg_id) {
		cable[port].last_sop_p_p_msg_id = msg_id;
		return false;
	}
	CPRINTF("C%d SOP Prime Prime repeat msg_id %d\n", port, msg_id);
	return true;
}

__maybe_unused static uint8_t is_sop_prime_ready(int port)
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
	return (pd_get_vconn_state(port) &&
		(IS_ENABLED(CONFIG_USB_PD_REV30) ||
		 (pd_get_data_role(port) == PD_ROLE_DFP)));
}

void reset_pd_cable(int port)
{
	memset(&cable[port], 0, sizeof(cable[port]));
	cable[port].last_sop_p_msg_id = INVALID_MSG_ID_COUNTER;
	cable[port].last_sop_p_p_msg_id = INVALID_MSG_ID_COUNTER;
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

static int process_am_discover_ident_sop_prime(int port, int cnt, uint32_t head,
					       uint32_t *payload)
{
	dfp_consume_identity(port, TCPCI_MSG_SOP_PRIME, cnt, payload);
	cable[port].rev = PD_HEADER_REV(head);

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
		payload[0] |=
			VDO_SVDM_VERS(pd_get_vdo_ver(port, TCPCI_MSG_SOP));
	} else if (cmd_type == CMDT_RSP_ACK) {
#ifdef CONFIG_USB_PD_ALT_MODE_DFP
		struct svdm_amode_data *modep;

		modep = pd_get_amode_data(port, TCPCI_MSG_SOP,
					  PD_VDO_VID(payload[0]));
#endif
		switch (cmd) {
#ifdef CONFIG_USB_PD_ALT_MODE_DFP
		case CMD_DISCOVER_IDENT:
			/* Received a SOP' Discover Ident msg */
			if (sop == TCPCI_MSG_SOP_PRIME) {
				rsize = process_am_discover_ident_sop_prime(
					port, cnt, head, payload);
				/* Received a SOP Discover Ident Message */
			} else {
				rsize = process_am_discover_ident_sop(
					port, cnt, head, payload, rtype);
			}
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
		payload[0] |=
			VDO_SVDM_VERS(pd_get_vdo_ver(port, TCPCI_MSG_SOP));
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
		/* Passive cable Nacked for Discover SVID */
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
