/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Alternate Mode Discovery storage, access, and helpers
 */

#include "builtin/assert.h"
#include "chipset.h"
#include "console.h"
#include "gpio.h"
#include "task.h"
#include "task_id.h"
#include "timer.h"
#include "typec_control.h"
#include "usb_charge.h"
#include "usb_common.h"
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

	if (PD_VDO_SVDM_VERS_MAJOR(payload[0]) &&
	    PD_VDO_SVDM_VERS_MINOR(payload[0]))
		disc->svdm_vers = SVDM_VER_2_1;
	else if (PD_VDO_SVDM_VERS_MAJOR(payload[0]))
		disc->svdm_vers = SVDM_VER_2_0;
	else
		disc->svdm_vers = SVDM_VER_1_0;

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
	const struct svid_mode_data *requested_mode_data =
		pd_get_next_mode(port, type);

	for (svid_idx = 0; svid_idx < disc->svid_cnt; ++svid_idx) {
		uint16_t svid = disc->svids[svid_idx].svid;

		if (svid == response_svid) {
			mode_discovery = &disc->svids[svid_idx];
			break;
		}
	}
	if (!mode_discovery || (requested_mode_data->svid != response_svid)) {
		CPRINTF("C%d: Unexpected mode repsonse for SVID %x, but TCPM "
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

void pd_disable_discovery(int port)
{
	/* Mark identity and SVIDs for the port partner and cable as failed.
	 * With no discovered SVIDs, there are no modes to mark as failed.
	 */
	pd_set_identity_discovery(port, TCPCI_MSG_SOP, PD_DISC_FAIL);
	pd_set_svids_discovery(port, TCPCI_MSG_SOP, PD_DISC_FAIL);
	pd_set_identity_discovery(port, TCPCI_MSG_SOP_PRIME, PD_DISC_FAIL);
	pd_set_svids_discovery(port, TCPCI_MSG_SOP_PRIME, PD_DISC_FAIL);
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
	enum pd_discovery_state svids_disc = pd_get_svids_discovery(port, type);

	/*
	 * If SVIDs discovery is incomplete, modes discovery is trivially
	 * incomplete.
	 */
	if (svids_disc != PD_DISC_COMPLETE)
		return svids_disc;

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
			       USB_R30_SS_U40_GEN3 ||
		       disc->identity.product_t1.p_rev30.ss ==
			       USB_R30_SS_U40_GEN4;
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
