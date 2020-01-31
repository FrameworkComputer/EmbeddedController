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
	struct pd_policy *pe = pd_get_am_policy(port);
	const char * const idh_ptype_names[]  = {
		"UNDEF", "Hub", "Periph", "PCable", "ACable", "AMA",
		"RSV6", "RSV7"};

	if (pe->identity[0] == 0) {
		ccprintf("No identity discovered yet.\n");
		return;
	}

	idh_ptype = PD_IDH_PTYPE(pe->identity[0]);
	ccprintf("IDENT:\n");
	ccprintf("\t[ID Header] %08x :: %s, VID:%04x\n",
				pe->identity[0],
				idh_ptype_names[idh_ptype],
				pd_get_identity_vid(port));

	ccprintf("\t[Cert Stat] %08x\n", pe->identity[1]);
	for (i = 2; i < ARRAY_SIZE(pe->identity); i++) {
		ccprintf("\t");
		if (pe->identity[i])
			ccprintf("[%d] %08x ", i, pe->identity[i]);
	}
	ccprintf("\n");

	if (pe->svid_cnt < 1) {
		ccprintf("No SVIDS discovered yet.\n");
		return;
	}

	for (i = 0; i < pe->svid_cnt; i++) {
		ccprintf("SVID[%d]: %04x MODES:", i, pe->svids[i].svid);
		for (j = 0; j < pe->svids[j].mode_cnt; j++)
			ccprintf(" [%d] %08x", j + 1, pe->svids[i].mode_vdo[j]);
		ccprintf("\n");

		modep = pd_get_amode_data(port, pe->svids[i].svid);
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
#endif /* CONFIG_USB_PD_ALT_MODE_DFP */
