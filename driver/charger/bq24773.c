/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * TI bq24773 battery charger driver.
 */

#include "battery_smart.h"
#include "bq24773.h"
#include "charger.h"
#include "console.h"
#include "common.h"
#include "i2c.h"
#include "util.h"

/*
 * on the I2C version of the charger,
 * some registers are 8-bit only (eg input current)
 * and they are shifted by 6 bits compared to the SMBUS version (bq24770).
 */
#define REG8_SHIFT 6
#define R8 (1 << (REG8_SHIFT))
/* Sense resistor configurations and macros */
#define DEFAULT_SENSE_RESISTOR 10
#define R_SNS CONFIG_CHARGER_SENSE_RESISTOR
#define R_AC  (CONFIG_CHARGER_SENSE_RESISTOR_AC)
#define REG_TO_CURRENT(REG, RS) ((REG) * DEFAULT_SENSE_RESISTOR / (RS))
#define CURRENT_TO_REG(CUR, RS) ((CUR) * (RS) / DEFAULT_SENSE_RESISTOR)
#define REG8_TO_CURRENT(REG, RS) ((REG) * DEFAULT_SENSE_RESISTOR / (RS) * R8)
#define CURRENT_TO_REG8(CUR, RS) ((CUR) * (RS) / DEFAULT_SENSE_RESISTOR / R8)

/* Charger parameters */
static const struct charger_info bq24773_charger_info = {
	.name         = "bq24773",
	.voltage_max  = 19200,
	.voltage_min  = 1024,
	.voltage_step = 16,
	.current_max  = REG_TO_CURRENT(0x1FC0, R_SNS),
	.current_min  = REG_TO_CURRENT(128, R_SNS),
	.current_step = REG_TO_CURRENT(64, R_SNS),
	.input_current_max  = REG8_TO_CURRENT(0x7F, R_AC),
	.input_current_min  = REG8_TO_CURRENT(2, R_AC),
	.input_current_step = REG8_TO_CURRENT(1, R_AC),
};

/* bq24773 specific interfaces */

int charger_set_input_current(int input_current)
{
	return i2c_write8(I2C_PORT_CHARGER, BQ24773_ADDR,
			  BQ24773_INPUT_CURRENT,
			  CURRENT_TO_REG8(input_current, R_AC));
}

int charger_get_input_current(int *input_current)
{
	int rv;
	int reg;

	rv = i2c_read8(I2C_PORT_CHARGER, BQ24773_ADDR,
			BQ24773_INPUT_CURRENT, &reg);
	if (rv)
		return rv;

	*input_current = REG8_TO_CURRENT(reg, R_AC);

	return EC_SUCCESS;
}

int charger_manufacturer_id(int *id)
{
	*id = 0x40; /* TI */
	return EC_SUCCESS;
}

int charger_device_id(int *id)
{
	return i2c_read8(I2C_PORT_CHARGER, BQ24773_ADDR,
			 BQ24773_DEVICE_ADDRESS, id);
}

int charger_get_option(int *option)
{
	return i2c_read16(I2C_PORT_CHARGER, BQ24773_ADDR,
			 BQ24773_CHARGE_OPTION0, option);
}

int charger_set_option(int option)
{
	return i2c_write16(I2C_PORT_CHARGER, BQ24773_ADDR,
			  BQ24773_CHARGE_OPTION0, option);
}

/* Charger interfaces */

const struct charger_info *charger_get_info(void)
{
	return &bq24773_charger_info;
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

	if (option & OPTION0_CHARGE_INHIBIT)
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
		option |= OPTION0_CHARGE_INHIBIT;
	else
		option &= ~OPTION0_CHARGE_INHIBIT;
	return charger_set_option(option);
}

int charger_get_current(int *current)
{
	int rv;
	int reg;

	rv = i2c_read16(I2C_PORT_CHARGER, BQ24773_ADDR,
			BQ24773_CHARGE_CURRENT, &reg);
	if (rv)
		return rv;

	*current = REG_TO_CURRENT(reg, R_SNS);
	return EC_SUCCESS;
}

int charger_set_current(int current)
{
	current = charger_closest_current(current);

	return i2c_write16(I2C_PORT_CHARGER, BQ24773_ADDR,
		BQ24773_CHARGE_CURRENT, CURRENT_TO_REG(current, R_SNS));
}

int charger_get_voltage(int *voltage)
{
	return i2c_read16(I2C_PORT_CHARGER, BQ24773_ADDR,
			  BQ24773_MAX_CHARGE_VOLTAGE, voltage);
}

int charger_set_voltage(int voltage)
{
	return i2c_write16(I2C_PORT_CHARGER, BQ24773_ADDR,
			   BQ24773_MAX_CHARGE_VOLTAGE, voltage);
}

/* Charging power state initialization */
int charger_post_init(void)
{
	int rv;

	/* Set charger input current limit */
	rv = charger_set_input_current(CONFIG_CHARGER_INPUT_CURRENT);
	return rv;
}

int charger_discharge_on_ac(int enable)
{
	int rv;
	int option;

	rv = charger_get_option(&option);
	if (rv)
		return rv;

	if (enable)
		rv = charger_set_option(option | OPTION0_LEARN_ENABLE);
	else
		rv = charger_set_option(option & ~OPTION0_LEARN_ENABLE);

	return rv;
}
