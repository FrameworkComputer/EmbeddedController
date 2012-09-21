/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * TI bq24725 battery charger driver.
 */

#include "board.h"
#include "charger.h"
#include "charger_bq24725.h"
#include "console.h"
#include "common.h"
#include "i2c.h"
#include "smart_battery.h"
#include "util.h"

/* Sense resistor configurations and macros */
#define DEFAULT_SENSE_RESISTOR 10
#define R_SNS CONFIG_BQ24725_R_SNS
#define R_AC  CONFIG_BQ24725_R_AC
#define REG_TO_CURRENT(REG, RS) ((REG) * DEFAULT_SENSE_RESISTOR / (RS))
#define CURRENT_TO_REG(CUR, RS) ((CUR) * (RS) / DEFAULT_SENSE_RESISTOR)

/* Charger infomation
 * charge voltage bitmask: 0111 1111 1111 0000
 * charge current bitmask: 0001 1111 1000 0000
 * input current bitmask : 0000 0000 1000 0000
 */
static const struct charger_info bq24725_charger_info = {
	.name         = "bq24725",
	.voltage_max  = 19200,
	.voltage_min  = 1024,
	.voltage_step = 16,
	.current_max  = REG_TO_CURRENT(8128, R_SNS),
	.current_min  = REG_TO_CURRENT(128, R_SNS),
	.current_step = REG_TO_CURRENT(128, R_SNS),
	.input_current_max  = REG_TO_CURRENT(8064, R_AC),
	.input_current_min  = REG_TO_CURRENT(128, R_AC),
	.input_current_step = REG_TO_CURRENT(128, R_AC),
};

/* bq24725 specific interfaces */

static int charger_set_input_current(int input_current)
{
	return sbc_write(BQ24725_INPUT_CURRENT,
		CURRENT_TO_REG(input_current, R_AC));
}

static int charger_get_input_current(int *input_current)
{
	int rv;
	int reg;

	rv = sbc_read(BQ24725_INPUT_CURRENT, &reg);
	if (rv)
		return rv;

	*input_current = REG_TO_CURRENT(reg, R_AC);

	return EC_SUCCESS;
}

static int charger_manufacturer_id(int *id)
{
	return sbc_read(BQ24725_MANUFACTURE_ID, id);
}

static int charger_device_id(int *id)
{
	return sbc_read(BQ24725_DEVICE_ID, id);
}

static int charger_get_option(int *option)
{
	return sbc_read(BQ24725_CHARGE_OPTION, option);
}

static int charger_set_option(int option)
{
	return sbc_write(BQ24725_CHARGE_OPTION, option);
}

/* charger interfaces */
const struct charger_info *charger_get_info(void)
{
	return &bq24725_charger_info;
}

int charger_get_status(int *status)
{
	int rv;
	int option;

	rv = charger_get_option(&option);
	if (rv)
		return rv;

	/* Default status */
	*status = CHARGER_LEVEL_2;

	if (option & OPTION_CHARGE_INHIBIT)
		*status |= CHARGER_CHARGE_INHIBITED;

	return EC_SUCCESS;
}

int charger_set_mode(int mode)
{
	int rv;
	int option;

	rv = charger_get_option(&option);
	if (rv)
		return rv;

	if (mode & CHARGE_FLAG_INHIBIT_CHARGE)
		option |= OPTION_CHARGE_INHIBIT;
	else
		option &= ~OPTION_CHARGE_INHIBIT;
	return charger_set_option(option);
}

int charger_get_current(int *current)
{
	int rv;
	int reg;

	rv = sbc_read(SB_CHARGING_CURRENT, &reg);
	if (rv)
		return rv;

	*current = REG_TO_CURRENT(reg, R_SNS);
	return EC_SUCCESS;
}

int charger_closest_current(int current)
{
	const struct charger_info * const info = charger_get_info();

	/*
	 * If the requested current is non-zero but below our minimum,
	 * return the minimum.  See crosbug.com/p/8662.
	 */
	if (current > 0 && current < info->current_min)
		return info->current_min;

	/* Clip to max */
	if (current > info->current_max)
		return info->current_max;

	/* Otherwise round down to nearest current step */
	return current - (current % info->current_step);
}

int charger_set_current(int current)
{
	current = charger_closest_current(current);

	return sbc_write(SB_CHARGING_CURRENT, CURRENT_TO_REG(current, R_SNS));
}

int charger_get_voltage(int *voltage)
{
	return sbc_read(SB_CHARGING_VOLTAGE, voltage);
}

int charger_set_voltage(int voltage)
{
	return sbc_write(SB_CHARGING_VOLTAGE, voltage);
}

/* Charging power state initialization */
int charger_post_init(void)
{
	/* bq24725 power on reset state:
	 * watch dog timer     = 175 sec
	 * input current limit = ~1/2 maximum setting
	 * charging voltage    = 0 mV
	 * charging current    = 0 mA
	 */

	/* Set charger input current limit */
	return charger_set_input_current(CONFIG_CHARGER_INPUT_CURRENT);
}

/*****************************************************************************/
/* Console commands */

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
