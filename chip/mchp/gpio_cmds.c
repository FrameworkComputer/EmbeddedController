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

	/* If a signal is specified, print only that one */
	if (argc == 2) {
		i = strtoi(argv[1], &e, 0);
		if (*e)
			return EC_ERROR_PARAM1;

		if (!gpio_is_implemented(i))
			return EC_ERROR_PARAM1;

		gctrl = MCHP_GPIO_CTRL(i);

		ccprintf(" GPIO[0x%X].Ctrl = 0x%08X\n", i, gctrl);

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
