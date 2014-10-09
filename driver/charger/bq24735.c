/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * TI bq24735 battery charger driver.
 */

#include "battery_smart.h"
#include "bq24735.h"
#include "charger.h"
#include "console.h"
#include "common.h"
#include "util.h"

/* Sense resistor configurations and macros */
#define DEFAULT_SENSE_RESISTOR 10
#define R_SNS CONFIG_CHARGER_SENSE_RESISTOR
#define R_AC  CONFIG_CHARGER_SENSE_RESISTOR_AC
#define REG_TO_CURRENT(REG, RS) ((REG) * DEFAULT_SENSE_RESISTOR / (RS))
#define CURRENT_TO_REG(CUR, RS) ((CUR) * (RS) / DEFAULT_SENSE_RESISTOR)

/* Charger infomation
 * charge voltage bitmask: 0111 1111 1111 0000
 * charge current bitmask: 0001 1111 1000 0000
 * input current bitmask : 0000 0000 1000 0000
 */
static const struct charger_info bq24735_charger_info = {
	.name         = "bq24735",
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

/* bq24735 specific interfaces */

int charger_set_input_current(int input_current)
{
	return sbc_write(BQ24735_INPUT_CURRENT,
			 CURRENT_TO_REG(input_current, R_AC));
}

int charger_get_input_current(int *input_current)
{
	int rv;
	int reg;

	rv = sbc_read(BQ24735_INPUT_CURRENT, &reg);
	if (rv)
		return rv;

	*input_current = REG_TO_CURRENT(reg, R_AC);

	return EC_SUCCESS;
}

int charger_manufacturer_id(int *id)
{
	return sbc_read(BQ24735_MANUFACTURE_ID, id);
}

int charger_device_id(int *id)
{
	return sbc_read(BQ24735_DEVICE_ID, id);
}

int charger_get_option(int *option)
{
	return sbc_read(BQ24735_CHARGE_OPTION, option);
}

int charger_set_option(int option)
{
	return sbc_write(BQ24735_CHARGE_OPTION, option);
}

/* Charger interfaces */

const struct charger_info *charger_get_info(void)
{
	return &bq24735_charger_info;
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
	/*
	 * Note: bq24735 power on reset state is:
	 *	watch dog timer     = 175 sec
	 *	input current limit = ~1/2 maximum setting
	 *	charging voltage    = 0 mV
	 *	charging current    = 0 mA
	 */

	int rv, option;

	rv = charger_get_option(&option);
	if (rv)
		return rv;

	option &= ~OPTION_LEARN_ENABLE;
	rv = charger_set_option(option);
	if (rv)
		return rv;

	/* Set charger input current limit */
	return charger_set_input_current(CONFIG_CHARGER_INPUT_CURRENT);
}

int charger_discharge_on_ac(int enable)
{
	int rv;
	int option;

	rv = charger_get_option(&option);
	if (rv)
		return rv;

	if (enable)
		rv = charger_set_option(option | OPTION_LEARN_ENABLE);
	else
		rv = charger_set_option(option & ~OPTION_LEARN_ENABLE);

	return rv;
}
