/* Copyright 2020 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Console commands for USB-PD module.
 */

#include "console.h"
#include "led_onoff_states.h"
#include "usb_dp_alt_mode.h"
#include "usb_pd.h"
#include "usb_pd_tcpm.h"
#include "util.h"

#ifdef CONFIG_USB_PD_ALT_MODE_DFP
#ifdef CONFIG_CMD_USB_PD_PE
static void dump_pe(int port)
{
	int i, j, idh_ptype;
	const union disc_ident_ack *resp;
	enum tcpci_msg_type type;
	/* TODO(b/152417597): Output SOP' discovery results */
	const struct pd_discovery *disc =
		pd_get_am_discovery(port, TCPCI_MSG_SOP);

	static const char *const idh_ptype_names[] = { "UNDEF",	 "Hub",
						       "Periph", "PCable",
						       "ACable", "AMA",
						       "RSV6",	 "RSV7" };
	static const char *const tx_names[] = { "SOP", "SOP'", "SOP''" };

	for (type = TCPCI_MSG_SOP; type < DISCOVERY_TYPE_COUNT; type++) {
		resp = pd_get_identity_response(port, type);
		if (pd_get_identity_discovery(port, type) != PD_DISC_COMPLETE) {
			ccprintf("No %s identity discovered yet.\n",
				 tx_names[type]);
			continue;
		}

		idh_ptype = resp->idh.product_type;
		ccprintf("IDENT %s:\n", tx_names[type]);
		ccprintf("\t[ID Header] %08x :: %s, VID:%04x\n",
			 resp->raw_value[0], idh_ptype_names[idh_ptype],
			 resp->idh.usb_vendor_id);

		ccprintf("\t[Cert Stat] %08x\n", resp->cert.xid);
		for (i = 2; i < ARRAY_SIZE(resp->raw_value); i++) {
			ccprintf("\t");
			if (resp->raw_value[i])
				ccprintf("[%d] %08x ", i, resp->raw_value[i]);
		}
		ccprintf("\n");
	}

	if (pd_get_svid_count(port, TCPCI_MSG_SOP) < 1) {
		ccprintf("No SVIDS discovered yet.\n");
		return;
	}

	/* TODO(b/152418267): Display discovered SVIDs and modes for SOP' */
	for (i = 0; i < pd_get_svid_count(port, TCPCI_MSG_SOP); i++) {
		ccprintf("SVID[%d]: %04x MODES:", i, disc->svids[i].svid);
		for (j = 0; j < disc->svids[j].mode_cnt; j++)
			ccprintf(" [%d] %08x", j + 1,
				 disc->svids[i].mode_vdo[j]);
		ccprintf("\n");
	}
}

static int command_pe(int argc, const char **argv)
{
	int port;
	char *e;

	if (argc < 3)
		return EC_ERROR_PARAM_COUNT;

	/* command: pe <port> <subcmd> <args> */
	port = strtoi(argv[1], &e, 10);
	if (*e || port >= board_get_usb_pd_port_count())
		return EC_ERROR_PARAM2;
	if (!strncasecmp(argv[2], "dump", 4))
		dump_pe(port);

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(pe, command_pe, "<port> dump", "USB PE");
#endif /* CONFIG_CMD_USB_PD_PE */

#ifdef CONFIG_CMD_USB_PD_CABLE
static const char *const cable_type[] = {
	[IDH_PTYPE_PCABLE] = "Passive",
	[IDH_PTYPE_ACABLE] = "Active",
};

static const char *const cable_curr[] = {
	[USB_VBUS_CUR_3A] = "3A",
	[USB_VBUS_CUR_5A] = "5A",
};

static const char *const dp21_cable_type[] = {
	[DP21_PASSIVE_CABLE] = "Passive",
	[DP21_ACTIVE_RETIMER_CABLE] = "Active-Retimer",
	[DP21_ACTIVE_REDRIVER_CABLE] = "Active-Redriver",
	[DP21_OPTICAL_CABLE] = "Optical",
};

static const char *const dp21_cable_speed[] = {
	[DP_HBR3] = "HBR3",
	[DP_UHBR10] = "UHBR10",
	[DP_UHBR20] = "UHBR20",
};

static int command_cable(int argc, const char **argv)
{
	int port;
	char *e;
	const struct pd_discovery *disc;
	enum idh_ptype ptype;
	int cable_rev;
	union tbt_mode_resp_cable cable_tbt_mode_resp;
	union dp_mode_resp_cable cable_dp_mode_resp;
	uint8_t dp_bit_rate;
	enum dp21_cable_type active_comp;

	if (argc < 2)
		return EC_ERROR_PARAM_COUNT;

	port = strtoi(argv[1], &e, 0);
	if (*e || port >= board_get_usb_pd_port_count())
		return EC_ERROR_PARAM2;

	ptype = get_usb_pd_cable_type(port);

	ccprintf("Cable Type: ");
	if (ptype != IDH_PTYPE_PCABLE && ptype != IDH_PTYPE_ACABLE) {
		ccprintf("Not Emark Cable\n");
		return EC_SUCCESS;
	}
	ccprintf("%s\n", cable_type[ptype]);

	cable_rev = pd_get_rev(port, TCPCI_MSG_SOP_PRIME);
	disc = pd_get_am_discovery(port, TCPCI_MSG_SOP_PRIME);
	cable_tbt_mode_resp.raw_value =
		IS_ENABLED(CONFIG_USB_PD_TBT_COMPAT_MODE) ?
			pd_get_tbt_mode_vdo(port, TCPCI_MSG_SOP_PRIME) :
			0;
	cable_dp_mode_resp.raw_value =
		IS_ENABLED(CONFIG_USB_PD_DP21_MODE) ?
			dp_get_mode_vdo(port, TCPCI_MSG_SOP_PRIME) :
			0;

	/* Cable revision */
	ccprintf("Cable Rev: %d.0\n", cable_rev + 1);

	/*
	 * For rev 2.0, rev 3.0 active and passive cables have same bits for
	 * connector type (Bit 19:18) and current handling capability bit 6:5
	 */
	ccprintf("Connector Type: %d\n",
		 disc->identity.product_t1.p_rev20.connector);

	if (disc->identity.product_t1.p_rev20.vbus_cur) {
		ccprintf("Cable Current: %s\n",
			 disc->identity.product_t1.p_rev20.vbus_cur >
					 ARRAY_SIZE(cable_curr) ?
				 "Invalid" :
				 cable_curr[disc->identity.product_t1.p_rev20
						    .vbus_cur]);
	} else
		ccprintf("Cable Current: Invalid\n");

	/*
	 * For Rev 3.0 passive cables and Rev 2.0 active and passive cables,
	 * USB Superspeed Signaling support have same bits 2:0
	 */
	if (ptype == IDH_PTYPE_PCABLE)
		ccprintf("USB Superspeed Signaling support: %d\n",
			 disc->identity.product_t1.p_rev20.ss);

	/*
	 * For Rev 3.0 active cables and Rev 2.0 active and passive cables,
	 * SOP" controller preset have same bit 3
	 */
	if (ptype == IDH_PTYPE_ACABLE)
		ccprintf("SOP'' Controller: %s present\n",
			 disc->identity.product_t1.a_rev20.sop_p_p ? "" :
								     "Not");

	if (cable_rev == PD_REV30) {
		/*
		 * For Rev 3.0 active and passive cables, Max Vbus vtg have
		 * same bits 10:9.
		 */
		ccprintf("Max vbus voltage: %d\n",
			 20 + 10 * disc->identity.product_t1.p_rev30.vbus_max);

		/* For Rev 3.0 Active cables */
		if (ptype == IDH_PTYPE_ACABLE) {
			ccprintf("SS signaling: USB_SS_GEN%u\n",
				 disc->identity.product_t2.a2_rev30.usb_gen ?
					 2 :
					 1);
			ccprintf("Number of SS lanes supported: %u\n",
				 disc->identity.product_t2.a2_rev30.usb_lanes);
		}
	}

	if (IS_ENABLED(CONFIG_USB_PD_DP21_MODE) &&
	    cable_dp_mode_resp.raw_value) {
		enum dpam_version dp_ver =
			dp_resolve_dpam_version(port, TCPCI_MSG_SOP_PRIME);
		if (dp_ver == DPAM_VERSION_21) {
			ccprintf("DPAM Version : %s\n",
				 (dp_ver ? "2.1 or higher" : "2.0 or earlier"));
			dp_bit_rate = dp_get_cable_bit_rate(port);
			ccprintf("DP Cable bitrate : %s\n",
				 ((dp_bit_rate <= DP_UHBR20) ?
					  dp21_cable_speed[dp_bit_rate] :
					  "Invalid"));
			ccprintf("DP UHBR13.5 Support : %s\n",
				 (cable_dp_mode_resp.uhbr13_5_support ?
					  "True" :
					  "False"));
			active_comp = cable_dp_mode_resp.active_comp;
			ccprintf("DP Cable Type : %s\n",
				 dp21_cable_type[active_comp]);
		}
	}

	if (!cable_tbt_mode_resp.raw_value)
		return EC_SUCCESS;

	ccprintf("Rounded support: %s\n",
		 cable_tbt_mode_resp.tbt_rounded ==
				 TBT_GEN3_GEN4_ROUNDED_NON_ROUNDED ?
			 "Yes" :
			 "No");

	ccprintf("Optical cable: %s\n",
		 cable_tbt_mode_resp.tbt_cable == TBT_CABLE_OPTICAL ? "Yes" :
								      "No");

	ccprintf("Retimer support: %s\n",
		 cable_tbt_mode_resp.retimer_type == USB_RETIMER ? "Yes" :
								   "No");

	ccprintf("Link training: %s-directional\n",
		 cable_tbt_mode_resp.lsrx_comm == BIDIR_LSRX_COMM ? "Bi" :
								    "Uni");

	ccprintf("Thunderbolt cable type: %s\n",
		 cable_tbt_mode_resp.tbt_active_passive == TBT_CABLE_ACTIVE ?
			 "Active" :
			 "Passive");

	return EC_SUCCESS;
}

DECLARE_CONSOLE_COMMAND(pdcable, command_cable, "<port>",
			"Cable Characteristics");
#endif /* CONFIG_CMD_USB_PD_CABLE */
#endif /* CONFIG_USB_PD_ALT_MODE_DFP */
