/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Alternate Mode Downstream Facing Port (DFP) USB-PD module.
 */

#include "chipset.h"
#include "console.h"
#include "task.h"
#include "task_id.h"
#include "timer.h"
#include "usb_charge.h"
#include "usb_mux.h"
#include "usb_pd.h"
#include "usbc_ppc.h"
#include "util.h"

#ifdef CONFIG_COMMON_RUNTIME
#define CPRINTS(format, args...) cprints(CC_USBPD, format, ## args)
#define CPRINTF(format, args...) cprintf(CC_USBPD, format, ## args)
#else
#define CPRINTS(format, args...)
#define CPRINTF(format, args...)
#endif

#ifndef PORT_TO_HPD
#define PORT_TO_HPD(port) ((port) ? GPIO_USB_C1_DP_HPD : GPIO_USB_C0_DP_HPD)
#endif /* PORT_TO_HPD */

/*
 * timestamp of the next possible toggle to ensure the 2-ms spacing
 * between IRQ_HPD.  Since this is used in overridable functions, this
 * has to be global.
 */
uint64_t svdm_hpd_deadline[CONFIG_USB_PD_PORT_MAX_COUNT];

int dp_flags[CONFIG_USB_PD_PORT_MAX_COUNT];

uint32_t dp_status[CONFIG_USB_PD_PORT_MAX_COUNT];

__overridable const struct svdm_response svdm_rsp = {
	.identity = NULL,
	.svids = NULL,
	.modes = NULL,
};

static int pd_get_mode_idx(int port, uint16_t svid)
{
	int i;
	struct pd_discovery *disc = pd_get_am_discovery(port);

	for (i = 0; i < PD_AMODE_COUNT; i++) {
		if (disc->amodes[i].fx &&
		    (disc->amodes[i].fx->svid == svid))
			return i;
	}
	return -1;
}

static int pd_allocate_mode(int port, uint16_t svid)
{
	int i, j;
	struct svdm_amode_data *modep;
	int mode_idx = pd_get_mode_idx(port, svid);
	struct pd_discovery *disc = pd_get_am_discovery(port);

	if (mode_idx != -1)
		return mode_idx;

	/* There's no space to enter another mode */
	if (disc->amode_idx == PD_AMODE_COUNT) {
		CPRINTF("ERR:NO AMODE SPACE\n");
		return -1;
	}

	/* Allocate ...  if SVID == 0 enter default supported policy */
	for (i = 0; i < supported_modes_cnt; i++) {
		for (j = 0; j < disc->svid_cnt; j++) {
			struct svdm_svid_data *svidp = &disc->svids[j];

			if ((svidp->svid != supported_modes[i].svid) ||
			    (svid && (svidp->svid != svid)))
				continue;

			modep = &disc->amodes[disc->amode_idx];
			modep->fx = &supported_modes[i];
			modep->data = &disc->svids[j];
			disc->amode_idx++;
			return disc->amode_idx - 1;
		}
	}
	return -1;
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
				pd_get_amode_data(port, USB_SID_DISPLAYPORT);
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

struct svdm_amode_data *pd_get_amode_data(int port, uint16_t svid)
{
	int idx = pd_get_mode_idx(port, svid);
	struct pd_discovery *disc = pd_get_am_discovery(port);

	return (idx == -1) ? NULL : &disc->amodes[idx];
}

/*
 * Enter default mode ( payload[0] == 0 ) or attempt to enter mode via svid &
 * opos
 */
uint32_t pd_dfp_enter_mode(int port, uint16_t svid, int opos)
{
	int mode_idx = pd_allocate_mode(port, svid);
	struct pd_discovery *disc = pd_get_am_discovery(port);
	struct svdm_amode_data *modep;
	uint32_t mode_caps;

	if (mode_idx == -1)
		return 0;
	modep = &disc->amodes[mode_idx];

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

	pd_set_dfp_enter_mode_flag(port, true);

	/* SVDM to send to UFP for mode entry */
	return VDO(modep->fx->svid, 1, CMD_ENTER_MODE | VDO_OPOS(modep->opos));
}

int pd_dfp_exit_mode(int port, uint16_t svid, int opos)
{
	struct svdm_amode_data *modep;
	struct pd_discovery *disc = pd_get_am_discovery(port);
	int idx;

	/*
	 * Empty svid signals we should reset DFP VDM state by exiting all
	 * entered modes then clearing state.  This occurs when we've
	 * disconnected or for hard reset.
	 */
	if (!svid) {
		for (idx = 0; idx < PD_AMODE_COUNT; idx++)
			if (disc->amodes[idx].fx)
				disc->amodes[idx].fx->exit(port);

		pd_dfp_discovery_init(port);
		return 0;
	}

	/*
	 * TODO(crosbug.com/p/33946) : below needs revisited to allow multiple
	 * mode exit.  Additionally it should honor OPOS == 7 as DFP's request
	 * to exit all modes.  We currently don't have any UFPs that support
	 * multiple modes on one SVID.
	 */
	modep = pd_get_amode_data(port, svid);
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
	struct svdm_amode_data *modep = pd_get_amode_data(port, svid);

	if (!modep || !validate_mode_request(modep, svid, opos))
		return;

	if (modep->fx->attention)
		modep->fx->attention(port, payload);
}

void dfp_consume_identity(int port, int cnt, uint32_t *payload)
{
	int ptype = PD_IDH_PTYPE(payload[VDO_I(IDH)]);
	struct pd_discovery *disc = pd_get_am_discovery(port);
	size_t identity_size = MIN(sizeof(disc->identity),
				   (cnt - 1) * sizeof(uint32_t));
	pd_dfp_discovery_init(port);
	memcpy(disc->identity, payload + 1, identity_size);

	switch (ptype) {
	case IDH_PTYPE_AMA:
		/* Leave vbus ON if the following macro is false */
		if (IS_ENABLED(CONFIG_USB_PD_DUAL_ROLE) &&
			IS_ENABLED(CONFIG_USBC_VCONN_SWAP)) {
			/* Adapter is requesting vconn, try to supply it */
			if (PD_VDO_AMA_VCONN_REQ(payload[VDO_I(AMA)]))
				pd_try_vconn_src(port);

			/* Only disable vbus if vconn was requested */
			if (PD_VDO_AMA_VCONN_REQ(payload[VDO_I(AMA)]) &&
				!PD_VDO_AMA_VBUS_REQ(payload[VDO_I(AMA)]))
				pd_power_supply_reset(port);
		}
		break;
	default:
		break;
	}
}

void dfp_consume_svids(int port, int cnt, uint32_t *payload)
{
	int i;
	uint32_t *ptr = payload + 1;
	int vdo = 1;
	uint16_t svid0, svid1;
	struct pd_discovery *disc = pd_get_am_discovery(port);

	for (i = disc->svid_cnt; i < disc->svid_cnt + 12; i += 2) {
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
		disc->svids[i].svid = svid0;
		disc->svid_cnt++;

		svid1 = PD_VDO_SVID_SVID1(*ptr);
		if (!svid1)
			break;
		disc->svids[i + 1].svid = svid1;
		disc->svid_cnt++;
		ptr++;
		vdo++;
	}
	/* TODO(tbroch) need to re-issue discover svids if > 12 */
	if (i && ((i % 12) == 0))
		CPRINTF("ERR:SVID+12\n");
}

void dfp_consume_modes(int port, int cnt, uint32_t *payload)
{
	struct pd_discovery *disc = pd_get_am_discovery(port);
	int idx = disc->svid_idx;

	disc->svids[idx].mode_cnt = cnt - 1;

	if (disc->svids[idx].mode_cnt < 0) {
		CPRINTF("ERR:NOMODE\n");
	} else {
		memcpy(disc->svids[disc->svid_idx].mode_vdo, &payload[1],
		       sizeof(uint32_t) * disc->svids[idx].mode_cnt);
	}

	disc->svid_idx++;
}

int dfp_discover_modes(int port, uint32_t *payload)
{
	struct pd_discovery *disc = pd_get_am_discovery(port);
	uint16_t svid = disc->svids[disc->svid_idx].svid;

	if (disc->svid_idx >= disc->svid_cnt)
		return 0;

	payload[0] = VDO(svid, 1, CMD_DISCOVER_MODES);

	return 1;
}

int pd_alt_mode(int port, uint16_t svid)
{
	struct svdm_amode_data *modep = pd_get_amode_data(port, svid);

	return (modep) ? modep->opos : -1;
}

uint16_t pd_get_identity_vid(int port)
{
	struct pd_discovery *disc = pd_get_am_discovery(port);

	return PD_IDH_VID(disc->identity[0]);
}

uint16_t pd_get_identity_pid(int port)
{
	struct pd_discovery *disc = pd_get_am_discovery(port);

	return PD_PRODUCT_PID(disc->identity[2]);
}

uint8_t pd_get_product_type(int port)
{
	struct pd_discovery *disc = pd_get_am_discovery(port);

	return PD_IDH_PTYPE(disc->identity[0]);
}

int pd_get_svid_count(int port)
{
	struct pd_discovery *disc = pd_get_am_discovery(port);

	return disc->svid_cnt;
}

uint16_t pd_get_svid(int port, uint16_t svid_idx)
{
	struct pd_discovery *disc = pd_get_am_discovery(port);

	return disc->svids[svid_idx].svid;
}

uint32_t *pd_get_mode_vdo(int port, uint16_t svid_idx)
{
	struct pd_discovery *disc = pd_get_am_discovery(port);

	return disc->svids[svid_idx].mode_vdo;
}

void notify_sysjump_ready(volatile const task_id_t * const sysjump_task_waiting)
{
	/*
	 * If event was set from pd_prepare_sysjump, wake the
	 * task waiting on us to complete.
	 */
	if (*sysjump_task_waiting != TASK_ID_INVALID)
		task_set_event(*sysjump_task_waiting,
						TASK_EVENT_SYSJUMP_READY, 0);
}

/*
 * Before entering into alternate mode, state of the USB-C MUX
 * needs to be in safe mode.
 * Ref: USB Type-C Cable and Connector Specification
 * Section E.2.2 Alternate Mode Electrical Requirements
 */
void usb_mux_set_safe_mode(int port)
{
	usb_mux_set(port, IS_ENABLED(CONFIG_USB_MUX_VIRTUAL) ?
		USB_PD_MUX_SAFE_MODE : USB_PD_MUX_NONE,
		USB_SWITCH_CONNECT, pd_get_polarity(port));

	/* Isolate the SBU lines. */
	if (IS_ENABLED(CONFIG_USBC_PPC_SBU))
		ppc_set_sbu(port, 0);
}

bool is_vdo_present(int cnt, int index)
{
	return cnt > index;
}

/*
 * ############################################################################
 *
 * Cable communication functions
 *
 * ############################################################################
 */
enum idh_ptype get_usb_pd_cable_type(int port)
{
	struct pd_cable *cable = pd_get_cable_attributes(port);

	return cable->type;
}

void dfp_consume_cable_response(int port, int cnt, uint32_t *payload,
				uint16_t head)
{
	struct pd_cable *cable = pd_get_cable_attributes(port);

	/* Get cable rev */
	cable->rev = PD_HEADER_REV(head);

	if (is_vdo_present(cnt, VDO_INDEX_IDH)) {
		cable->type = PD_IDH_PTYPE(payload[VDO_INDEX_IDH]);

		if (is_vdo_present(cnt, VDO_INDEX_PTYPE_CABLE1))
			cable->attr.raw_value =
					payload[VDO_INDEX_PTYPE_CABLE1];

		/*
		 * Ref USB PD Spec 3.0  Pg 145. For active cable there are two
		 * VDOs. Hence storing the second VDO.
		 */
		if (is_vdo_present(cnt, VDO_INDEX_PTYPE_CABLE2))
			cable->attr2.raw_value =
					payload[VDO_INDEX_PTYPE_CABLE2];

		cable->is_identified = 1;
		cable->discovery = PD_DISC_COMPLETE;
	}
}

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
	 * could cause a wake up.)
	 */
	if (chipset_in_state(CHIPSET_STATE_ANY_OFF))
		return -1;

	/* Only enter mode if device is DFP_D capable */
	if (mode_caps & MODE_DP_SNK) {
		svdm_safe_dp_mode(port);

		if (IS_ENABLED(CONFIG_MKBP_EVENT) &&
		    chipset_in_state(CHIPSET_STATE_ANY_SUSPEND))
			/*
			 * Wake the system up since we're entering DP AltMode.
			 */
			pd_notify_dp_alt_mode_entry();

		return 0;
	}

	return -1;
}

__overridable int svdm_dp_status(int port, uint32_t *payload)
{
	int opos = pd_alt_mode(port, USB_SID_DISPLAYPORT);

	payload[0] = VDO(USB_SID_DISPLAYPORT, 1,
			 CMD_DP_STATUS | VDO_OPOS(opos));
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

__overridable int svdm_dp_config(int port, uint32_t *payload)
{
	int opos = pd_alt_mode(port, USB_SID_DISPLAYPORT);
	int mf_pref = PD_VDO_DPSTS_MF_PREF(dp_status[port]);
	uint8_t pin_mode = get_dp_pin_mode(port);
	mux_state_t mux_mode;

	if (!pin_mode)
		return 0;

	/*
	 * Multi-function operation is only allowed if that pin config is
	 * supported.
	 */
	mux_mode = ((pin_mode & MODE_DP_PIN_MF_MASK) && mf_pref) ?
		USB_PD_MUX_DOCK : USB_PD_MUX_DP_ENABLED;
	CPRINTS("pin_mode: %x, mf: %d, mux: %d", pin_mode, mf_pref, mux_mode);

	/* Connect the SBU and USB lines to the connector. */
	if (IS_ENABLED(CONFIG_USBC_PPC_SBU))
		ppc_set_sbu(port, 1);
	usb_mux_set(port, mux_mode, USB_SWITCH_CONNECT, pd_get_polarity(port));

	payload[0] = VDO(USB_SID_DISPLAYPORT, 1,
			 CMD_DP_CONFIG | VDO_OPOS(opos));
	payload[1] = VDO_DP_CFG(pin_mode,      /* pin mode */
				1,	       /* DPv1.3 signaling */
				2);	       /* UFP connected */
	return 2;
};

__overridable void svdm_dp_post_config(int port)
{
	dp_flags[port] |= DP_FLAGS_DP_ON;
	if (!(dp_flags[port] & DP_FLAGS_HPD_HI_PENDING))
		return;

#ifdef CONFIG_USB_PD_DP_HPD_GPIO
	gpio_set_level(PORT_TO_HPD(port), 1);

	/* set the minimum time delay (2ms) for the next HPD IRQ */
	svdm_hpd_deadline[port] = get_time().val + HPD_USTREAM_DEBOUNCE_LVL;
#endif /* CONFIG_USB_PD_DP_HPD_GPIO */

	usb_mux_hpd_update(port, 1, 0);

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
	enum gpio_signal hpd = PORT_TO_HPD(port);
	int cur_lvl = gpio_get_level(hpd);
#endif /* CONFIG_USB_PD_DP_HPD_GPIO */

	dp_status[port] = payload[1];

	if (chipset_in_state(CHIPSET_STATE_ANY_SUSPEND) &&
	    (irq || lvl))
		/*
		 * Wake up the AP.  IRQ or level high indicates a DP sink is now
		 * present.
		 */
		if (IS_ENABLED(CONFIG_MKBP_EVENT))
			pd_notify_dp_alt_mode_entry();

	/* Its initial DP status message prior to config */
	if (!(dp_flags[port] & DP_FLAGS_DP_ON)) {
		if (lvl)
			dp_flags[port] |= DP_FLAGS_HPD_HI_PENDING;
		return 1;
	}

#ifdef CONFIG_USB_PD_DP_HPD_GPIO
	if (irq & !lvl) {
		/*
		 * IRQ can only be generated when the level is high, because
		 * the IRQ is signaled by a short low pulse from the high level.
		 */
		CPRINTF("ERR:HPD:IRQ&LOW\n");
		return 0; /* nak */
	}

	if (irq & cur_lvl) {
		uint64_t now = get_time().val;
		/* wait for the minimum spacing between IRQ_HPD if needed */
		if (now < svdm_hpd_deadline[port])
			usleep(svdm_hpd_deadline[port] - now);

		/* generate IRQ_HPD pulse */
		gpio_set_level(hpd, 0);
		usleep(HPD_DSTREAM_DEBOUNCE_IRQ);
		gpio_set_level(hpd, 1);
	} else {
		gpio_set_level(hpd, lvl);
	}

	/* set the minimum time delay (2ms) for the next HPD IRQ */
	svdm_hpd_deadline[port] = get_time().val + HPD_USTREAM_DEBOUNCE_LVL;
#endif /* CONFIG_USB_PD_DP_HPD_GPIO */

	usb_mux_hpd_update(port, lvl, irq);

#ifdef USB_PD_PORT_TCPC_MST
	if (port == USB_PD_PORT_TCPC_MST)
		baseboard_mst_enable_control(port, lvl);
#endif

	/* ack */
	return 1;
}

__overridable void svdm_exit_dp_mode(int port)
{
	svdm_safe_dp_mode(port);
#ifdef CONFIG_USB_PD_DP_HPD_GPIO
	gpio_set_level(PORT_TO_HPD(port), 0);
#endif /* CONFIG_USB_PD_DP_HPD_GPIO */
	usb_mux_hpd_update(port, 0, 0);
#ifdef USB_PD_PORT_TCPC_MST
	if (port == USB_PD_PORT_TCPC_MST)
		baseboard_mst_enable_control(port, 0);
#endif
}

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

#ifdef CONFIG_USB_PD_TBT_COMPAT_MODE
__overridable int svdm_tbt_compat_enter_mode(int port, uint32_t mode_caps)
{
	return 0;
}

__overridable void svdm_tbt_compat_exit_mode(int port)
{
}

__overridable int svdm_tbt_compat_status(int port, uint32_t *payload)
{
	return 0;
}

__overridable int svdm_tbt_compat_config(int port, uint32_t *payload)
{
	return 0;
}

__overridable int svdm_tbt_compat_attention(int port, uint32_t *payload)
{
	return 0;
}
#endif /* CONFIG_USB_PD_TBT_COMPAT_MODE */

const struct svdm_amode_fx supported_modes[] = {
	{
		.svid = USB_SID_DISPLAYPORT,
		.enter = &svdm_enter_dp_mode,
		.status = &svdm_dp_status,
		.config = &svdm_dp_config,
		.post_config = &svdm_dp_post_config,
		.attention = &svdm_dp_attention,
		.exit = &svdm_exit_dp_mode,
	},

	{
		.svid = USB_VID_GOOGLE,
		.enter = &svdm_enter_gfu_mode,
		.status = &svdm_gfu_status,
		.config = &svdm_gfu_config,
		.attention = &svdm_gfu_attention,
		.exit = &svdm_exit_gfu_mode,
	},
#ifdef CONFIG_USB_PD_TBT_COMPAT_MODE
	{
		.svid = USB_VID_INTEL,
		.enter = &svdm_tbt_compat_enter_mode,
		.status = &svdm_tbt_compat_status,
		.config = &svdm_tbt_compat_config,
		.attention = &svdm_tbt_compat_attention,
		.exit = &svdm_tbt_compat_exit_mode,
	},
#endif /* CONFIG_USB_PD_TBT_COMPAT_MODE */
};
const int supported_modes_cnt = ARRAY_SIZE(supported_modes);
