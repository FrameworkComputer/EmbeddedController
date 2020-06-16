/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Console commands for USB-PD module.
 */

#include "console.h"
#include "usb_pd.h"
#include "util.h"

#ifdef CONFIG_USB_PD_ALT_MODE_DFP
#ifdef CONFIG_CMD_USB_PD_PE
static void dump_pe(int port)
{
	int i, j, idh_ptype;
	struct svdm_amode_data *modep;
	uint32_t mode_caps;
	const union disc_ident_ack *resp;
	enum tcpm_transmit_type type;
	/* TODO(b/152417597): Output SOP' discovery results */
	const struct pd_discovery *disc =
		pd_get_am_discovery(port, TCPC_TX_SOP);

	static const char * const idh_ptype_names[]  = {
		"UNDEF", "Hub", "Periph", "PCable", "ACable", "AMA",
		"RSV6", "RSV7"};
	static const char * const tx_names[] = {"SOP", "SOP'", "SOP''"};

	for (type = TCPC_TX_SOP; type < DISCOVERY_TYPE_COUNT; type++) {
		resp = pd_get_identity_response(port, type);
		if (pd_get_identity_discovery(port, type) != PD_DISC_COMPLETE) {
			ccprintf("No %s identity discovered yet.\n",
								tx_names[type]);
			continue;
		}

		idh_ptype = resp->idh.product_type;
		ccprintf("IDENT %s:\n", tx_names[type]);
		ccprintf("\t[ID Header] %08x :: %s, VID:%04x\n",
			 resp->raw_value[0],
			 idh_ptype_names[idh_ptype],
			 resp->idh.usb_vendor_id);

		ccprintf("\t[Cert Stat] %08x\n", resp->cert.xid);
		for (i = 2; i < ARRAY_SIZE(resp->raw_value); i++) {
			ccprintf("\t");
			if (resp->raw_value[i])
				ccprintf("[%d] %08x ", i, resp->raw_value[i]);
		}
		ccprintf("\n");
	}

	if (pd_get_svid_count(port, TCPC_TX_SOP) < 1) {
		ccprintf("No SVIDS discovered yet.\n");
		return;
	}

	/* TODO(b/152418267): Display discovered SVIDs and modes for SOP' */
	for (i = 0; i < pd_get_svid_count(port, TCPC_TX_SOP); i++) {
		ccprintf("SVID[%d]: %04x MODES:", i, disc->svids[i].svid);
		for (j = 0; j < disc->svids[j].mode_cnt; j++)
			ccprintf(" [%d] %08x", j + 1,
					disc->svids[i].mode_vdo[j]);
		ccprintf("\n");

		modep = pd_get_amode_data(port, TCPC_TX_SOP,
				disc->svids[i].svid);
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
	if (*e || port >= board_get_usb_pd_port_count())
		return EC_ERROR_PARAM2;
	if (!strncasecmp(argv[2], "dump", 4))
		dump_pe(port);

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(pe, command_pe,
			"<port> dump",
			"USB PE");
#endif /* CONFIG_CMD_USB_PD_PE */

#ifdef CONFIG_CMD_USB_PD_CABLE
static const char * const cable_type[] = {
	[IDH_PTYPE_PCABLE] = "Passive",
	[IDH_PTYPE_ACABLE] = "Active",
};

static const char * const cable_curr[] = {
	[USB_VBUS_CUR_3A] = "3A",
	[USB_VBUS_CUR_5A] = "5A",
};

static int command_cable(int argc, char **argv)
{
	int port;
	char *e;
	struct pd_discovery *disc;
	enum idh_ptype ptype;
	int cable_rev;

	if (argc < 2)
		return EC_ERROR_PARAM_COUNT;

	port = strtoi(argv[1], &e, 0);
	if (*e || port >= board_get_usb_pd_port_count())
		return EC_ERROR_PARAM2;

	ptype = get_usb_pd_cable_type(port);

	ccprintf("Cable Type: ");
	if (ptype != IDH_PTYPE_PCABLE &&
		ptype != IDH_PTYPE_ACABLE) {
		ccprintf("Not Emark Cable\n");
		return EC_SUCCESS;
	}
	ccprintf("%s\n", cable_type[ptype]);

	cable_rev = pd_get_vdo_ver(port, TCPC_TX_SOP_PRIME);
	disc = pd_get_am_discovery(port, TCPC_TX_SOP_PRIME);

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
		      ARRAY_SIZE(cable_curr) ? "Invalid" :
		      cable_curr[disc->identity.product_t1.p_rev20.vbus_cur]);
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
		      disc->identity.product_t1.a_rev20.sop_p_p ? "" : "Not");

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
									2 : 1);
			ccprintf("Number of SS lanes supported: %u\n",
				disc->identity.product_t2.a2_rev30.usb_lanes);
		}
	}
	return EC_SUCCESS;
}

DECLARE_CONSOLE_COMMAND(pdcable, command_cable,
			"<port>",
			"Cable Characteristics");
#endif /* CONFIG_CMD_USB_PD_CABLE */

#endif /* CONFIG_USB_PD_ALT_MODE_DFP */
