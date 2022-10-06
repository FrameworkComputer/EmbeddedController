/* Copyright 2020 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Alternate Mode Downstream Facing Port (DFP) USB-PD module.
 */

#include "builtin/assert.h"
#include "chipset.h"
#include "console.h"
#include "gpio.h"
#include "task.h"
#include "task_id.h"
#include "timer.h"
#include "typec_control.h"
#include "usb_common.h"
#include "usb_charge.h"
#include "usb_dp_alt_mode.h"
#include "usb_mux.h"
#include "usb_pd.h"
#include "usb_pd_tcpm.h"
#include "usb_tbt_alt_mode.h"
#include "usbc_ppc.h"
#include "util.h"

#ifdef CONFIG_COMMON_RUNTIME
#define CPRINTS(format, args...) cprints(CC_USBPD, format, ##args)
#define CPRINTF(format, args...) cprintf(CC_USBPD, format, ##args)
#else
#define CPRINTS(format, args...)
#define CPRINTF(format, args...)
#endif

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
#ifndef CONFIG_ZEPHYR
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
#endif /* CONFIG_ZEPHYR */
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

/* TODO(b/170372521) : Incorporate exit mode specific changes to DPM SM */
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

/*
 * Check if the SVID has been recorded previously. Some peripherals provide
 * duplicated SVID.
 */
static bool is_svid_duplicated(const struct pd_discovery *disc, uint16_t svid)
{
	int i;

	for (i = 0; i < disc->svid_cnt; ++i)
		if (disc->svids[i].svid == svid) {
			CPRINTF("ERR:SVIDDUP\n");
			return true;
		}

	return false;
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

void dfp_consume_identity(int port, enum tcpci_msg_type type, int cnt,
			  uint32_t *payload)
{
	int ptype;
	struct pd_discovery *disc;
	size_t identity_size;

	if (type == TCPCI_MSG_SOP_PRIME &&
	    !IS_ENABLED(CONFIG_USB_PD_DECODE_SOP)) {
		CPRINTF("ERR:Unexpected cable response\n");
		return;
	}

	ptype = PD_IDH_PTYPE(payload[VDO_I(IDH)]);
	disc = pd_get_am_discovery_and_notify_access(port, type);
	identity_size =
		MIN(sizeof(union disc_ident_ack), (cnt - 1) * sizeof(uint32_t));

	/* Note: only store VDOs, not the VDM header */
	memcpy(disc->identity.raw_value, payload + 1, identity_size);
	disc->identity_cnt = identity_size / sizeof(uint32_t);

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
	pd_set_identity_discovery(port, type, PD_DISC_COMPLETE);
}

void dfp_consume_svids(int port, enum tcpci_msg_type type, int cnt,
		       uint32_t *payload)
{
	int i;
	uint32_t *ptr = payload + 1;
	int vdo = 1;
	uint16_t svid0, svid1;
	struct pd_discovery *disc =
		pd_get_am_discovery_and_notify_access(port, type);

	for (i = disc->svid_cnt; i < disc->svid_cnt + 12; i += 2) {
		if (i >= SVID_DISCOVERY_MAX) {
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

		if (!is_svid_duplicated(disc, svid0))
			disc->svids[disc->svid_cnt++].svid = svid0;

		svid1 = PD_VDO_SVID_SVID1(*ptr);
		if (!svid1)
			break;

		if (!is_svid_duplicated(disc, svid1))
			disc->svids[disc->svid_cnt++].svid = svid1;

		ptr++;
		vdo++;
	}
	/* TODO(tbroch) need to re-issue discover svids if > 12 */
	if (i && ((i % 12) == 0))
		CPRINTF("ERR:SVID+12\n");

	pd_set_svids_discovery(port, type, PD_DISC_COMPLETE);
}

void dfp_consume_modes(int port, enum tcpci_msg_type type, int cnt,
		       uint32_t *payload)
{
	int svid_idx;
	struct svid_mode_data *mode_discovery = NULL;
	struct pd_discovery *disc =
		pd_get_am_discovery_and_notify_access(port, type);
	uint16_t response_svid = (uint16_t)PD_VDO_VID(payload[0]);

	for (svid_idx = 0; svid_idx < disc->svid_cnt; ++svid_idx) {
		uint16_t svid = disc->svids[svid_idx].svid;

		if (svid == response_svid) {
			mode_discovery = &disc->svids[svid_idx];
			break;
		}
	}
	if (!mode_discovery) {
		const struct svid_mode_data *requested_mode_data =
			pd_get_next_mode(port, type);
		CPRINTF("C%d: Mode response for undiscovered SVID %x, but TCPM "
			"requested SVID %x\n",
			port, response_svid, requested_mode_data->svid);
		/*
		 * Although SVIDs discovery seemed like it succeeded before, the
		 * partner is now responding with undiscovered SVIDs. Discovery
		 * cannot reasonably continue under these circumstances.
		 */
		pd_set_modes_discovery(port, type, requested_mode_data->svid,
				       PD_DISC_FAIL);
		return;
	}

	mode_discovery->mode_cnt = cnt - 1;
	if (mode_discovery->mode_cnt < 1) {
		CPRINTF("ERR:NOMODE\n");
		pd_set_modes_discovery(port, type, mode_discovery->svid,
				       PD_DISC_FAIL);
		return;
	}

	memcpy(mode_discovery->mode_vdo, &payload[1],
	       sizeof(uint32_t) * mode_discovery->mode_cnt);
	disc->svid_idx++;
	pd_set_modes_discovery(port, type, mode_discovery->svid,
			       PD_DISC_COMPLETE);
}

int pd_alt_mode(int port, enum tcpci_msg_type type, uint16_t svid)
{
	struct svdm_amode_data *modep = pd_get_amode_data(port, type, svid);

	return (modep) ? modep->opos : -1;
}

void pd_set_identity_discovery(int port, enum tcpci_msg_type type,
			       enum pd_discovery_state disc)
{
	struct pd_discovery *pd =
		pd_get_am_discovery_and_notify_access(port, type);

	pd->identity_discovery = disc;
}

enum pd_discovery_state pd_get_identity_discovery(int port,
						  enum tcpci_msg_type type)
{
	const struct pd_discovery *disc = pd_get_am_discovery(port, type);

	return disc->identity_discovery;
}

const union disc_ident_ack *pd_get_identity_response(int port,
						     enum tcpci_msg_type type)
{
	if (type >= DISCOVERY_TYPE_COUNT)
		return NULL;

	return &pd_get_am_discovery(port, type)->identity;
}

uint16_t pd_get_identity_vid(int port)
{
	const union disc_ident_ack *resp =
		pd_get_identity_response(port, TCPCI_MSG_SOP);

	return resp->idh.usb_vendor_id;
}

uint16_t pd_get_identity_pid(int port)
{
	const union disc_ident_ack *resp =
		pd_get_identity_response(port, TCPCI_MSG_SOP);

	return resp->product.product_id;
}

uint8_t pd_get_product_type(int port)
{
	const union disc_ident_ack *resp =
		pd_get_identity_response(port, TCPCI_MSG_SOP);

	return resp->idh.product_type;
}

void pd_set_svids_discovery(int port, enum tcpci_msg_type type,
			    enum pd_discovery_state disc)
{
	struct pd_discovery *pd =
		pd_get_am_discovery_and_notify_access(port, type);

	pd->svids_discovery = disc;
}

enum pd_discovery_state pd_get_svids_discovery(int port,
					       enum tcpci_msg_type type)
{
	const struct pd_discovery *disc = pd_get_am_discovery(port, type);

	return disc->svids_discovery;
}

int pd_get_svid_count(int port, enum tcpci_msg_type type)
{
	const struct pd_discovery *disc = pd_get_am_discovery(port, type);

	return disc->svid_cnt;
}

uint16_t pd_get_svid(int port, uint16_t svid_idx, enum tcpci_msg_type type)
{
	const struct pd_discovery *disc = pd_get_am_discovery(port, type);

	return disc->svids[svid_idx].svid;
}

void pd_set_modes_discovery(int port, enum tcpci_msg_type type, uint16_t svid,
			    enum pd_discovery_state disc)
{
	struct pd_discovery *pd =
		pd_get_am_discovery_and_notify_access(port, type);
	int svid_idx;

	for (svid_idx = 0; svid_idx < pd->svid_cnt; ++svid_idx) {
		struct svid_mode_data *mode_data = &pd->svids[svid_idx];

		if (mode_data->svid != svid)
			continue;

		mode_data->discovery = disc;
		return;
	}
}

enum pd_discovery_state pd_get_modes_discovery(int port,
					       enum tcpci_msg_type type)
{
	const struct svid_mode_data *mode_data = pd_get_next_mode(port, type);

	/*
	 * If there are no SVIDs for which to discover modes, mode discovery is
	 * trivially complete.
	 */
	if (!mode_data)
		return PD_DISC_COMPLETE;

	return mode_data->discovery;
}

int pd_get_mode_vdo_for_svid(int port, enum tcpci_msg_type type, uint16_t svid,
			     uint32_t *vdo_out)
{
	int idx;
	const struct pd_discovery *disc;

	if (type >= DISCOVERY_TYPE_COUNT)
		return 0;

	disc = pd_get_am_discovery(port, type);

	for (idx = 0; idx < disc->svid_cnt; ++idx) {
		if (pd_get_svid(port, idx, type) == svid) {
			memcpy(vdo_out, disc->svids[idx].mode_vdo,
			       sizeof(uint32_t) * disc->svids[idx].mode_cnt);
			return disc->svids[idx].mode_cnt;
		}
	}
	return 0;
}

const struct svid_mode_data *pd_get_next_mode(int port,
					      enum tcpci_msg_type type)
{
	const struct pd_discovery *disc = pd_get_am_discovery(port, type);
	const struct svid_mode_data *failed_mode_data = NULL;
	bool svid_good_discovery = false;
	int svid_idx;

	/* Walk through all of the discovery mode entries */
	for (svid_idx = 0; svid_idx < disc->svid_cnt; ++svid_idx) {
		const struct svid_mode_data *mode_data = &disc->svids[svid_idx];

		/* Discovery is needed, so send this one back now */
		if (mode_data->discovery == PD_DISC_NEEDED)
			return mode_data;

		/* Discovery already succeeded, save that it was seen */
		if (mode_data->discovery == PD_DISC_COMPLETE)
			svid_good_discovery = true;
		/* Discovery already failed, save first failure */
		else if (!failed_mode_data)
			failed_mode_data = mode_data;
	}

	/* If no good entries were located, then return last failed */
	if (!svid_good_discovery)
		return failed_mode_data;

	/*
	 * Mode discovery has been attempted for every discovered SVID (if
	 * any exist)
	 */
	return NULL;
}

const uint32_t *pd_get_mode_vdo(int port, uint16_t svid_idx,
				enum tcpci_msg_type type)
{
	const struct pd_discovery *disc = pd_get_am_discovery(port, type);

	return disc->svids[svid_idx].mode_vdo;
}

bool pd_is_mode_discovered_for_svid(int port, enum tcpci_msg_type type,
				    uint16_t svid)
{
	const struct pd_discovery *disc = pd_get_am_discovery(port, type);
	const struct svid_mode_data *mode_data;

	for (mode_data = disc->svids; mode_data < disc->svids + disc->svid_cnt;
	     ++mode_data) {
		if (mode_data->svid == svid &&
		    mode_data->discovery == PD_DISC_COMPLETE)
			return true;
	}

	return false;
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

static inline bool is_pd_rev3(int port, enum tcpci_msg_type type)
{
	return pd_get_rev(port, type) == PD_REV30;
}

/*
 * ############################################################################
 *
 * (Charge Through) Vconn Powered Device functions
 *
 * ############################################################################
 */
bool is_vpd_ct_supported(int port)
{
	const struct pd_discovery *disc =
		pd_get_am_discovery(port, TCPCI_MSG_SOP_PRIME);
	union vpd_vdo vpd = disc->identity.product_t1.vpd;

	return vpd.ct_support;
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
	const struct pd_discovery *disc =
		pd_get_am_discovery(port, TCPCI_MSG_SOP_PRIME);

	return disc->identity.idh.product_type;
}

bool is_usb2_cable_support(int port)
{
	const struct pd_discovery *disc =
		pd_get_am_discovery(port, TCPCI_MSG_SOP_PRIME);

	return disc->identity.idh.product_type == IDH_PTYPE_PCABLE ||
	       pd_get_vdo_ver(port, TCPCI_MSG_SOP_PRIME) < VDM_VER20 ||
	       disc->identity.product_t2.a2_rev30.usb_20_support ==
		       USB2_SUPPORTED;
}

bool is_cable_speed_gen2_capable(int port)
{
	const struct pd_discovery *disc =
		pd_get_am_discovery(port, TCPCI_MSG_SOP_PRIME);

	switch (pd_get_rev(port, TCPCI_MSG_SOP_PRIME)) {
	case PD_REV20:
		return disc->identity.product_t1.p_rev20.ss ==
		       USB_R20_SS_U31_GEN1_GEN2;

	case PD_REV30:
		return disc->identity.product_t1.p_rev30.ss ==
			       USB_R30_SS_U32_U40_GEN2 ||
		       disc->identity.product_t1.p_rev30.ss ==
			       USB_R30_SS_U40_GEN3;
	default:
		return false;
	}
}

bool is_active_cable_element_retimer(int port)
{
	const struct pd_discovery *disc =
		pd_get_am_discovery(port, TCPCI_MSG_SOP_PRIME);

	/* Ref: USB PD Spec 2.0 Table 6-29 Active Cable VDO
	 * Revision 2 Active cables do not have Active element support.
	 */
	return is_pd_rev3(port, TCPCI_MSG_SOP_PRIME) &&
	       disc->identity.idh.product_type == IDH_PTYPE_ACABLE &&
	       disc->identity.product_t2.a2_rev30.active_elem == ACTIVE_RETIMER;
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
			usleep(svdm_hpd_deadline[port] - now);

		/* generate IRQ_HPD pulse */
		svdm_set_hpd_gpio(port, 0);
		usleep(HPD_DSTREAM_DEBOUNCE_IRQ);
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

#ifdef CONFIG_USB_PD_TCPMV1
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
#endif /* CONFIG_USB_PD_TCPMV1 */

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

/*
 * TODO: b:169262276: For TCPMv2, move alternate mode specific entry, exit and
 * configuration to Device Policy Manager.
 */
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
#ifdef CONFIG_USB_PD_TCPMV1
	{
		.svid = USB_VID_GOOGLE,
		.enter = &svdm_enter_gfu_mode,
		.status = &svdm_gfu_status,
		.config = &svdm_gfu_config,
		.attention = &svdm_gfu_attention,
		.exit = &svdm_exit_gfu_mode,
	},
#endif /* CONFIG_USB_PD_TCPMV1 */
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

#ifdef CONFIG_CMD_MFALLOW
static int command_mfallow(int argc, const char **argv)
{
	char *e;
	int port;

	if (argc < 3)
		return EC_ERROR_PARAM_COUNT;

	port = strtoi(argv[1], &e, 10);
	if (*e || port >= board_get_usb_pd_port_count())
		return EC_ERROR_PARAM2;

	if (!strcasecmp(argv[2], "true"))
		dp_port_mf_allow[port] = true;
	else if (!strcasecmp(argv[2], "false"))
		dp_port_mf_allow[port] = false;
	else
		return EC_ERROR_PARAM1;

	ccprintf("Port: %d multi function allowed is %s ", port, argv[2]);
	return EC_SUCCESS;
}

DECLARE_CONSOLE_COMMAND(mfallow, command_mfallow, "port [true | false]",
			"Controls Multifunction choice during DP Altmode.");
#endif
