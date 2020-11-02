/* Copyright 2017 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* MCHP MEC GPIO module EC UART commands */

#include "common.h"
#include "console.h"
#include "gpio.h"
#include "hooks.h"
#include "registers.h"
#include "system.h"
#include "util.h"
#include "gpio_chip.h"
#include "tfdp_chip.h"

/* Console output macros */
#define CPUTS(outstr) cputs(CC_LPC, outstr)
#define CPRINTS(format, args...) cprints(CC_LPC, format, ## args)



static int cmd_gp_get_config(int argc, char **argv)
{
	char *e;

	int i;
	uint32_t gctrl;
	static const char * const pupd[] = {"None", "Pull Up", "Pull Down", "Repeater"};
	static const char * const pgs[] = {"VTR", "VCC", "Unpowered", "Reserved"};
	static const char * const mux[] = {"GPIO", "Alt 1", "Alt 2", "Alt 3"};
	static const char * const drive[] = {"2mA", "4mA", "8mA", "12mA"};
	static const char * const interrupt[] = {"Low", "High", "", "",
									"Disabled", "", "", "",
									"", "", "", "",
									"", "EdgeRising", "EdgeFalling", "EdgeBoth"};

	/* If a signal is specified, print only that one */
	if (argc == 2) {
		i = strtoi(argv[1], &e, 0);
		if (*e)
			return EC_ERROR_PARAM1;

		if (!gpio_is_implemented(i))
			return EC_ERROR_PARAM1;

		gctrl = MCHP_GPIO_CTRL(i);

		ccprintf(" GPIO[0x%X].Ctrl = 0x%08X\n", i, gctrl);
		ccprintf("            PUPD = %s\n", pupd[gctrl & MCHP_GPIO_CTRL_PUD_MASK]);
		ccprintf("             PGS = %s\n", pgs[(gctrl>>MCHP_GPIO_CTRL_PWR_BITPOS) & MCHP_GPIO_CTRL_PWR_MASK0]);
		ccprintf("  Edge Detection = %s\n", gctrl & 0x80 ? "Enabled" : "Disabled");
		ccprintf("  Interrupt Type = %s\n", interrupt[(gctrl>>4) & 0x0F]);
		ccprintf("   Output Buffer = %s\n", gctrl & 0x100 ? "Open Drain" : "PushPull");
		ccprintf("       Direction = %s\n", gctrl & 0x200 ? "Output" : "Input");
		ccprintf("     Output Ctrl = %s\n", gctrl & 0x400 ? "Pin Control" : "Grouped");
		ccprintf("        Polarity = %s\n", gctrl & MCHP_GPIO_POLARITY_INV ? "Inverted" : "Normal");
		ccprintf("        Alt Mode = %s\n", mux[(gctrl>>MCHP_GPIO_CTRL_ALT_FUNC_BITPOS) & 0x03]);
		ccprintf("   Input Disable = %s\n", gctrl & MCHP_GPIO_CTRL_INPUT_DISABLE_MASK ? "Disabled" : "Normal");
		ccprintf("     Output Data = %s\n", gctrl & MCHP_GPIO_CTRL_OUT_LVL ? "Disabled" : "Normal");
		ccprintf("       Pad Value = %s\n", gctrl & MCHP_GPIO_CTRL_IN_LVL ? "High" : "Low");
		gctrl = MCHP_GPIO_CTRL2(i);
		ccprintf("  Drive Strength = %s\n", drive[(gctrl>>MCHP_GPIO_CTRL2_DRIVE_STRENGTH_BITPOS) & 0x03]);
	} else { /* Otherwise print them all */
		for (i = 0; i < GPIO_COUNT; i++) {
			if (!gpio_is_implemented(i))
				continue;  /* Skip unsupported signals */

			gctrl = MCHP_GPIO_CTRL(i);

			ccprintf(" GPIO[0x%X].Ctrl = 0x%08X\n", i, gctrl);
		}
	}

	/* Flush console to avoid truncating output */
	cflush();

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(gpgetcfg, cmd_gp_get_config,
			"[number]",
			"Read GPIO config");

static int cmd_gp_set_config(int argc, char **argv)
{
	char *e;
	int i;
	uint32_t gctrl;

	/* If a signal is specified, print only that one */
	if (argc > 2) {
		i = strtoi(argv[1], &e, 0);
		if (*e)
			return EC_ERROR_PARAM1;

		if (!gpio_is_implemented(i))
			return EC_ERROR_PARAM1;

		gctrl = (uint32_t)strtoi(argv[2], &e, 0);
		if (*e)
			return EC_ERROR_PARAM2;

		MCHP_GPIO_CTRL(i) = gctrl;
		gctrl = MCHP_GPIO_CTRL(i);
		ccprintf(" GPIO[0x%X].Ctrl = 0x%08X\n", i, gctrl);

	} else {
		ccprintf(" Requires two parameters: GPIO num and new config");
	}
	/* Flush console to avoid truncating output */
	cflush();

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(gpsetcfg, cmd_gp_set_config,
			"gp_num val",
			"Set GPIO config");

