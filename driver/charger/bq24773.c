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
	.voltage_max  = CHARGE_V_MAX,
	.voltage_min  = CHARGE_V_MIN,
	.voltage_step = CHARGE_V_STEP,
	.current_max  = REG_TO_CURRENT(CHARGE_I_MAX, R_SNS),
	.current_min  = REG_TO_CURRENT(CHARGE_I_MIN, R_SNS),
	.current_step = REG_TO_CURRENT(CHARGE_I_STEP, R_SNS),
	.input_current_max  = REG_TO_CURRENT(INPUT_I_MAX, R_AC),
	.input_current_min  = REG_TO_CURRENT(INPUT_I_MIN, R_AC),
	.input_current_step = REG_TO_CURRENT(INPUT_I_STEP, R_AC),
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
	int rv, option;
#ifdef CONFIG_CHARGER_ILIM_PIN_DISABLED
	int option2;
#endif

	rv = charger_get_option(&option);
	if (rv)
		return rv;

	option &= ~OPTION0_LEARN_ENABLE;

	rv = charger_set_option(option);
	if (rv)
		return rv;

#ifndef BOARD_SAMUS
	/* Turn off PROCHOT warning */
	rv = i2c_write8(I2C_PORT_CHARGER, BQ24773_ADDR,
			BQ24773_PROCHOT_OPTION1, 0);
#else
	/* On Samus, use PROCHOT warning to detect charging problems */
	/* Turn on PROCHOT warning */
	rv = i2c_write16(I2C_PORT_CHARGER, BQ24773_ADDR,
			 BQ24773_PROCHOT_OPTION1, 0x8120);
	/* Set PROCHOT ICRIT warning when IADP is >120% of IDPM */
	rv |= i2c_write16(I2C_PORT_CHARGER, BQ24773_ADDR,
			 BQ24773_PROCHOT_OPTION0, 0x1b54);
#endif

	if (rv)
		return rv;

#ifdef CONFIG_CHARGER_ILIM_PIN_DISABLED
	/* Read the external ILIM pin enabled flag. */
	rv = i2c_read16(I2C_PORT_CHARGER, BQ24773_ADDR,
			   BQ24773_CHARGE_OPTION2, &option2);
	if (rv)
		return rv;

	/* Set ILIM pin disabled if it is currently enabled. */
	if (option2 & OPTION2_EN_EXTILIM) {
		option2 &= ~OPTION2_EN_EXTILIM;
		rv = i2c_write16(I2C_PORT_CHARGER, BQ24773_ADDR,
				 BQ24773_CHARGE_OPTION2, option2);
	}
	return rv;
#else
	return EC_SUCCESS;
#endif
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
