/* Copyright 2014 The Chromium OS Authors. All rights reserved.
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
static const struct charger_info bq2477x_charger_info = {
	.name         = CHARGER_NAME,
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

/* chip specific interfaces */

int charger_set_input_current(int input_current)
{
#ifdef CONFIG_CHARGER_BQ24770
	return raw_write16(REG_INPUT_CURRENT,
			   CURRENT_TO_REG(input_current, R_AC));
#elif defined(CONFIG_CHARGER_BQ24773)
	return raw_write8(REG_INPUT_CURRENT,
			  CURRENT_TO_REG8(input_current, R_AC));
#endif
}

int charger_get_input_current(int *input_current)
{
	int rv;
	int reg;

#ifdef CONFIG_CHARGER_BQ24770
	rv = raw_read16(REG_INPUT_CURRENT, &reg);
#elif defined(CONFIG_CHARGER_BQ24773)
	rv = raw_read8(REG_INPUT_CURRENT, &reg);
#endif
	if (rv)
		return rv;

#ifdef CONFIG_CHARGER_BQ24770
	*input_current = REG_TO_CURRENT(reg, R_AC);
#elif defined(CONFIG_CHARGER_BQ24773)
	*input_current = REG8_TO_CURRENT(reg, R_AC);
#endif
	return EC_SUCCESS;
}

int charger_manufacturer_id(int *id)
{
#ifdef CONFIG_CHARGER_BQ24770
	return raw_read16(REG_MANUFACTURE_ID, id);
#elif defined(CONFIG_CHARGER_BQ24773)
	*id = 0x40; /* TI */
	return EC_SUCCESS;
#endif
}

int charger_device_id(int *id)
{
#ifdef CONFIG_CHARGER_BQ24770
	return raw_read16(REG_DEVICE_ADDRESS, id);
#elif defined(CONFIG_CHARGER_BQ24773)
	return raw_read8(REG_DEVICE_ADDRESS, id);
#endif
}

int charger_get_option(int *option)
{
	return raw_read16(REG_CHARGE_OPTION0, option);
}

int charger_set_option(int option)
{
	return raw_write16(REG_CHARGE_OPTION0, option);
}

/* Charger interfaces */

const struct charger_info *charger_get_info(void)
{
	return &bq2477x_charger_info;
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

	rv = raw_read16(REG_CHARGE_CURRENT, &reg);

	if (rv)
		return rv;

	*current = REG_TO_CURRENT(reg, R_SNS);
	return EC_SUCCESS;
}

int charger_set_current(int current)
{
	current = charger_closest_current(current);
	return raw_write16(REG_CHARGE_CURRENT, CURRENT_TO_REG(current, R_SNS));
}

int charger_get_voltage(int *voltage)
{
	return raw_read16(REG_MAX_CHARGE_VOLTAGE, voltage);
}

int charger_set_voltage(int voltage)
{
	voltage = charger_closest_voltage(voltage);
	return raw_write16(REG_MAX_CHARGE_VOLTAGE, voltage);
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
	rv = raw_read16(REG_PROCHOT_OPTION1, &option);
	if (rv)
		return rv;

	option &= ~PROCHOT_OPTION1_SELECTOR_MASK;

	rv = raw_write16(REG_PROCHOT_OPTION1, option);
#else
	/* On Samus, use PROCHOT warning to detect charging problems */
	/* Turn on PROCHOT warning */
	rv = raw_write16(REG_PROCHOT_OPTION1, 0x8120);
	/* Set PROCHOT ICRIT warning when IADP is >120% of IDPM */
	rv |= raw_write16(REG_PROCHOT_OPTION0, 0x1b54);
#endif

	if (rv)
		return rv;

#ifdef CONFIG_CHARGER_ILIM_PIN_DISABLED
	/* Read the external ILIM pin enabled flag. */
	rv = raw_read16(REG_CHARGE_OPTION2, &option2);
	if (rv)
		return rv;

	/* Set ILIM pin disabled if it is currently enabled. */
	if (option2 & OPTION2_EN_EXTILIM) {
		option2 &= ~OPTION2_EN_EXTILIM;
		rv = raw_write16(REG_CHARGE_OPTION2, option2);
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
