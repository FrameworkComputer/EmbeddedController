/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Common functions for battery charging.
 */

#include "charger.h"
#include "common.h"
#include "console.h"
#include "host_command.h"
#include "printf.h"
#include "smart_battery.h"
#include "util.h"

/* Console output macros */
#define CPUTS(outstr) cputs(CC_CHARGER, outstr)
#define CPRINTF(format, args...) cprintf(CC_CHARGER, format, ## args)

int charger_closest_voltage(int voltage)
{
	const struct charger_info *info;

	info = charger_get_info();
	return voltage - (voltage % info->voltage_step);
}

static int print_info(void)
{
	int rv;
	int d;
	const struct charger_info *info;

	/* info */
	info = charger_get_info();
	ccprintf("Name  : %s\n", info->name);

	/* option */
	rv = charger_get_option(&d);
	if (rv)
		return rv;
	ccprintf("Option: %016b (0x%04x)\n", d, d);

	/* manufacturer id */
	rv = charger_manufacturer_id(&d);
	if (rv)
		return rv;
	ccprintf("Man id: 0x%04x\n", d);

	/* device id */
	rv = charger_device_id(&d);
	if (rv)
		return rv;
	ccprintf("Dev id: 0x%04x\n", d);

	/* charge voltage limit */
	rv = charger_get_voltage(&d);
	if (rv)
		return rv;
	ccprintf("V_batt: %5d (%4d - %5d, %3d)\n", d,
		 info->voltage_min, info->voltage_max, info->voltage_step);

	/* charge current limit */
	rv = charger_get_current(&d);
	if (rv)
		return rv;
	ccprintf("I_batt: %5d (%4d - %5d, %3d)\n", d,
		 info->current_min, info->current_max, info->current_step);

	/* input current limit */
	rv = charger_get_input_current(&d);
	if (rv)
		return rv;

	ccprintf("I_in  : %5d (%4d - %5d, %3d)\n", d,
		 info->input_current_min, info->input_current_max,
		 info->input_current_step);

	return EC_SUCCESS;
}

static int command_charger(int argc, char **argv)
{
	int d;
	char *e;

	if (argc != 3)
		return print_info();

	if (strcasecmp(argv[1], "input") == 0) {
		d = strtoi(argv[2], &e, 0);
		if (*e)
			return EC_ERROR_PARAM2;
		return charger_set_input_current(d);
	} else if (strcasecmp(argv[1], "current") == 0) {
		d = strtoi(argv[2], &e, 0);
		if (*e)
			return EC_ERROR_PARAM2;
		return charger_set_current(d);
	} else if (strcasecmp(argv[1], "voltage") == 0) {
		d = strtoi(argv[2], &e, 0);
		if (*e)
			return EC_ERROR_PARAM2;
		return charger_set_voltage(d);
	} else
		return EC_ERROR_PARAM1;
}

DECLARE_CONSOLE_COMMAND(charger, command_charger,
			"[input | current | voltage] [newval]",
			"Get or set charger param(s)",
			NULL);
