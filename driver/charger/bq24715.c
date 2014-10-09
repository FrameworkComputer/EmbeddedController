/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * TI bq24715 battery charger driver.
 */

#include "battery_smart.h"
#include "bq24715.h"
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

/* Note: it is assumed that the sense resistors are 10mOhm. */

static const struct charger_info bq24725_charger_info = {
	.name         = "bq24715",
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

int charger_set_input_current(int input_current)
{
	return sbc_write(BQ24715_INPUT_CURRENT,
			 CURRENT_TO_REG(input_current, R_AC));
}

int charger_get_input_current(int *input_current)
{
	int rv;
	int reg;

	rv = sbc_read(BQ24715_INPUT_CURRENT, &reg);
	if (rv)
		return rv;

	*input_current = REG_TO_CURRENT(reg, R_AC);

	return EC_SUCCESS;
}

int charger_manufacturer_id(int *id)
{
	return sbc_read(BQ24715_MANUFACTURER_ID, id);
}

int charger_device_id(int *id)
{
	return sbc_read(BQ24715_DEVICE_ID, id);
}

int charger_get_option(int *option)
{
	return sbc_read(BQ24715_CHARGE_OPTION, option);
}

int charger_set_option(int option)
{
	return sbc_write(BQ24715_CHARGE_OPTION, option);
}

/* Charger interfaces */

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

	if ((option & OPT_CHARGE_INHIBIT_MASK) == OPT_CHARGE_DISABLE)
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

	option &= ~OPT_CHARGE_INHIBIT_MASK;
	if (mode & CHARGE_FLAG_INHIBIT_CHARGE)
		option |= OPT_CHARGE_DISABLE;
	else
		option |= OPT_CHARGE_ENABLE;
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

/* The voltage setting needs to be cached to work with the current
 * charging infrastructure and state machine. The reason is that
 * the state machine expects to be able to set a 0V charging voltage.
 * The bq24715 does not allow this in the hardware register. Therefore
 * 0V is handled specially to appease the state machine. */
static int cached_voltage;

int charger_get_voltage(int *voltage)
{
	int ret;

	if (cached_voltage == 0) {
		*voltage = cached_voltage;
		return EC_SUCCESS;
	}

	ret = sbc_read(SB_CHARGING_VOLTAGE, &cached_voltage);

	if (ret == EC_SUCCESS)
		*voltage = cached_voltage;

	return ret;
}

int charger_set_voltage(int voltage)
{
	cached_voltage = voltage;
	return sbc_write(SB_CHARGING_VOLTAGE, voltage);
}

/* Charging power state initialization */
int charger_post_init(void)
{
	int rv;
	int option;

	rv = charger_get_option(&option);
	if (rv)
		return rv;

	/* Don't be noisy */
	option |= OPT_AUDIO_FREQ_40KHZ_LIMIT;

	/* Always monitor adapter current (40X multiplier). */
	option |= OPT_FIX_IOUT_ALWAYS;
	option &= ~OPT_IOUT_MASK;
	option &= ~OPT_LEARN_MASK;

	/* Enable dynamic power management */
	option |= OPT_IDPM_ENABLE;

	rv = charger_set_option(option);
	if (rv)
		return rv;

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

	option &= ~OPT_LEARN_MASK;
	if (enable)
		option |= OPT_LEARN_ENABLE;
	else
		option |= OPT_LEARN_DISABLE;
	rv = charger_set_option(option);

	return rv;
}
