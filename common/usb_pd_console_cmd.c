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
	struct pd_discovery *disc = pd_get_am_discovery(port);
	const union disc_ident_ack *resp = pd_get_identity_response(port,
								TCPC_TX_SOP);

	const char * const idh_ptype_names[]  = {
		"UNDEF", "Hub", "Periph", "PCable", "ACable", "AMA",
		"RSV6", "RSV7"};

	if (pd_get_identity_discovery(port, TCPC_TX_SOP) != PD_DISC_COMPLETE) {
		ccprintf("No identity discovered yet.\n");
		return;
	}

	idh_ptype = pd_get_product_type(port);
	ccprintf("IDENT:\n");
	ccprintf("\t[ID Header] %08x :: %s, VID:%04x\n",
				resp->raw_value[0],
				idh_ptype_names[idh_ptype],
				pd_get_identity_vid(port));

	ccprintf("\t[Cert Stat] %08x\n", resp->cert.xid);
	for (i = 2; i < ARRAY_SIZE(resp->raw_value); i++) {
		ccprintf("\t");
		if (resp->raw_value[i])
			ccprintf("[%d] %08x ", i, resp->raw_value[i]);
	}
	ccprintf("\n");

	if (disc->svid_cnt < 1) {
		ccprintf("No SVIDS discovered yet.\n");
		return;
	}

	for (i = 0; i < disc->svid_cnt; i++) {
		ccprintf("SVID[%d]: %04x MODES:", i, disc->svids[i].svid);
		for (j = 0; j < disc->svids[j].mode_cnt; j++)
			ccprintf(" [%d] %08x", j + 1,
						disc->svids[i].mode_vdo[j]);
		ccprintf("\n");

		modep = pd_get_amode_data(port, disc->svids[i].svid);
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
	struct pd_cable *cable;

	if (argc < 2)
		return EC_ERROR_PARAM_COUNT;

	port = strtoi(argv[1], &e, 0);
	if (*e || port >= board_get_usb_pd_port_count())
		return EC_ERROR_PARAM2;

	cable = pd_get_cable_attributes(port);

	if (!cable->is_identified) {
		ccprintf("Cable not identified.\n");
		return EC_SUCCESS;
	}

	ccprintf("Cable Type: ");
	if (cable->type != IDH_PTYPE_PCABLE &&
		cable->type != IDH_PTYPE_ACABLE) {
		ccprintf("Not Emark Cable\n");
		return EC_SUCCESS;
	}
	ccprintf("%s\n", cable_type[cable->type]);

	/* Cable revision */
	ccprintf("Cable Rev: %d.0\n", cable->rev + 1);

	/*
	 * For rev 2.0, rev 3.0 active and passive cables have same bits for
	 * connector type (Bit 19:18) and current handling capability bit 6:5
	 */
	ccprintf("Connector Type: %d\n", cable->attr.p_rev20.connector);

	if (cable->attr.p_rev20.vbus_cur) {
		ccprintf("Cable Current: %s\n",
		   cable->attr.p_rev20.vbus_cur > ARRAY_SIZE(cable_curr) ?
		   "Invalid" : cable_curr[cable->attr.p_rev20.vbus_cur]);
	} else
		ccprintf("Cable Current: Invalid\n");

	/*
	 * For Rev 3.0 passive cables and Rev 2.0 active and passive cables,
	 * USB Superspeed Signaling support have same bits 2:0
	 */
	if (cable->type == IDH_PTYPE_PCABLE)
		ccprintf("USB Superspeed Signaling support: %d\n",
			cable[port].attr.p_rev20.ss);

	/*
	 * For Rev 3.0 active cables and Rev 2.0 active and passive cables,
	 * SOP" controller preset have same bit 3
	 */
	if (cable->type == IDH_PTYPE_ACABLE)
		ccprintf("SOP'' Controller: %s present\n",
			cable->attr.a_rev20.sop_p_p ? "" : "Not");

	if (cable->rev == PD_REV30) {
		/*
		 * For Rev 3.0 active and passive cables, Max Vbus vtg have
		 * same bits 10:9.
		 */
		ccprintf("Max vbus voltage: %d\n",
			20 + 10 * cable->attr.p_rev30.vbus_max);

		/* For Rev 3.0 Active cables */
		if (cable->type == IDH_PTYPE_ACABLE) {
			ccprintf("SS signaling: USB_SS_GEN%u\n",
				cable->attr2.a2_rev30.usb_gen ? 2 : 1);
			ccprintf("Number of SS lanes supported: %u\n",
				cable->attr2.a2_rev30.usb_lanes);
		}
	}
	return EC_SUCCESS;
}

DECLARE_CONSOLE_COMMAND(pdcable, command_cable,
			"<port>",
			"Cable Characteristics");
#endif /* CONFIG_CMD_USB_PD_CABLE */

#endif /* CONFIG_USB_PD_ALT_MODE_DFP */
