/* Copyright 2018 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * TI bq25703 battery charger driver.
 */

#include "battery_smart.h"
#include "bq25703.h"
#include "charge_ramp.h"
#include "charger.h"
#include "common.h"
#include "console.h"
#include "hooks.h"
#include "i2c.h"
#include "timer.h"

/* Sense resistor configurations and macros */
#define DEFAULT_SENSE_RESISTOR 10

#define INPUT_RESISTOR_RATIO \
	((CONFIG_CHARGER_SENSE_RESISTOR_AC) / DEFAULT_SENSE_RESISTOR)
#define REG_TO_INPUT_CURRENT(REG) ((REG + 1) * 50 / INPUT_RESISTOR_RATIO)
#define INPUT_CURRENT_TO_REG(CUR) (((CUR) * INPUT_RESISTOR_RATIO / 50) - 1)

#define CHARGING_RESISTOR_RATIO \
	((CONFIG_CHARGER_SENSE_RESISTOR) / DEFAULT_SENSE_RESISTOR)
#define REG_TO_CHARGING_CURRENT(REG) ((REG) / CHARGING_RESISTOR_RATIO)
#define CHARGING_CURRENT_TO_REG(CUR) ((CUR) * CHARGING_RESISTOR_RATIO)


/* Console output macros */
#define CPRINTF(format, args...) cprintf(CC_CHARGER, format, ## args)

/* Charger parameters */
static const struct charger_info bq25703_charger_info = {
	.name         = "bq25703",
	.voltage_max  = 19200,
	.voltage_min  = 1024,
	.voltage_step = 16,
	.current_max  = 8128 / CHARGING_RESISTOR_RATIO,
	.current_min  = 64 / CHARGING_RESISTOR_RATIO,
	.current_step = 64 / CHARGING_RESISTOR_RATIO,
	.input_current_max  = 6400 / INPUT_RESISTOR_RATIO,
	.input_current_min  = 50 / INPUT_RESISTOR_RATIO,
	.input_current_step = 50 / INPUT_RESISTOR_RATIO,
};

static inline int raw_read8(int offset, int *value)
{
	return i2c_read8(I2C_PORT_CHARGER, BQ25703_I2C_ADDR1, offset, value);
}

static inline int raw_write8(int offset, int value)
{
	return i2c_write8(I2C_PORT_CHARGER, BQ25703_I2C_ADDR1, offset, value);
}

static inline int raw_read16(int offset, int *value)
{
	return i2c_read16(I2C_PORT_CHARGER, BQ25703_I2C_ADDR1, offset, value);
}

static inline int raw_write16(int offset, int value)
{
	return i2c_write16(I2C_PORT_CHARGER, BQ25703_I2C_ADDR1, offset, value);
}

#ifdef CONFIG_CHARGE_RAMP_HW
static int bq25703_get_low_power_mode(int *mode)
{
	int rv;
	int reg;

	rv = raw_read16(BQ25703_REG_CHARGE_OPTION_0, &reg);
	if (rv)
		return rv;

	*mode = !!(reg & BQ25703_CHARGE_OPTION_0_LOW_POWER_MODE);

	return EC_SUCCESS;
}

static int bq25703_set_low_power_mode(int enable)
{
	int rv;
	int reg;

	rv = raw_read16(BQ25703_REG_CHARGE_OPTION_0, &reg);
	if (rv)
		return rv;

	if (enable)
		reg |= BQ25703_CHARGE_OPTION_0_LOW_POWER_MODE;
	else
		reg &= ~BQ25703_CHARGE_OPTION_0_LOW_POWER_MODE;

	rv = raw_write16(BQ25703_REG_CHARGE_OPTION_0, reg);
	if (rv)
		return rv;

	return EC_SUCCESS;
}
#endif

/* Charger interfaces */

const struct charger_info *charger_get_info(void)
{
	return &bq25703_charger_info;
}

int charger_post_init(void)
{
	/*
	 * Note: bq25703 power on reset state is:
	 *	watch dog timer     = 175 sec
	 *	input current limit = ~1/2 maximum setting
	 *	charging voltage    = 0 mV
	 *	charging current    = 0 mA
	 *	discharge on AC     = disabled
	 */

	/* Set charger input current limit */
	return charger_set_input_current(CONFIG_CHARGER_INPUT_CURRENT);
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

	if (option & BQ25703_CHARGE_OPTION_0_CHRG_INHIBIT)
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

	if (mode & CHARGER_CHARGE_INHIBITED)
		option |= BQ25703_CHARGE_OPTION_0_CHRG_INHIBIT;
	else
		option &= ~BQ25703_CHARGE_OPTION_0_CHRG_INHIBIT;

	return charger_set_option(option);
}

int charger_enable_otg_power(int enabled)
{
	/* This is controlled with the EN_OTG pin. Support not added yet. */
	return EC_ERROR_UNIMPLEMENTED;
}

int charger_set_otg_current_voltage(int output_current, int output_voltage)
{
	/* Add when needed. */
	return EC_ERROR_UNIMPLEMENTED;
}

int charger_is_sourcing_otg_power(int port)
{
	/* Add when needed. */
	return EC_ERROR_UNIMPLEMENTED;
}

int charger_get_current(int *current)
{
	int rv, reg;

	rv = raw_read16(BQ25703_REG_CHARGE_CURRENT, &reg);
	if (!rv)
		*current = REG_TO_CHARGING_CURRENT(reg);

	return rv;
}

int charger_set_current(int current)
{
	return raw_write16(BQ25703_REG_CHARGE_CURRENT,
		CHARGING_CURRENT_TO_REG(current));
}

/* Get/set charge voltage limit in mV */
int charger_get_voltage(int *voltage)
{
	return raw_read16(BQ25703_REG_MAX_CHARGE_VOLTAGE, voltage);
}
int charger_set_voltage(int voltage)
{
	return raw_write16(BQ25703_REG_MAX_CHARGE_VOLTAGE, voltage);
}

/* Discharge battery when on AC power. */
int charger_discharge_on_ac(int enable)
{
	int rv, option;

	rv = charger_get_option(&option);
	if (rv)
		return rv;

	if (enable)
		option |= BQ25703_CHARGE_OPTION_0_EN_LEARN;
	else
		option &= ~BQ25703_CHARGE_OPTION_0_EN_LEARN;

	return charger_set_option(option);
}

int charger_set_input_current(int input_current)
{
	return raw_write8(BQ25703_REG_IIN_HOST,
		INPUT_CURRENT_TO_REG(input_current));
}

int charger_get_input_current(int *input_current)
{
	int rv, reg;

	/*
	 * IIN_DPM register reflects the actual input current limit programmed
	 * in the register, either from host or from ICO. After ICO, the
	 * current limit used by DPM regulation may differ from the IIN_HOST
	 * register settings.
	 */
	rv = raw_read8(BQ25703_REG_IIN_DPM, &reg);
	if (!rv)
		*input_current = REG_TO_INPUT_CURRENT(reg);

	return rv;
}

int charger_manufacturer_id(int *id)
{
	return raw_read8(BQ25703_REG_MANUFACTURER_ID, id);
}
int charger_device_id(int *id)
{
	return raw_read8(BQ25703_REG_DEVICE_ADDRESS, id);
}

int charger_get_option(int *option)
{
	/* There are 4 option registers, but we only need the first for now. */
	return raw_read16(BQ25703_REG_CHARGE_OPTION_0, option);
}

int charger_set_option(int option)
{
	/* There are 4 option registers, but we only need the first for now. */
	return raw_write16(BQ25703_REG_CHARGE_OPTION_0, option);
}

#ifdef CONFIG_CHARGE_RAMP_HW

static void bq25703_chg_ramp_handle(void)
{
	int ramp_curr;

	/*
	 * Once the charge ramp is stable write back the stable ramp
	 * current to input current register.
	 */
	if (chg_ramp_is_stable()) {
		ramp_curr = chg_ramp_get_current_limit();
		if (ramp_curr && !charger_set_input_current(ramp_curr))
			CPRINTF("stable ramp current=%d\n", ramp_curr);
	}
}
DECLARE_DEFERRED(bq25703_chg_ramp_handle);

int charger_set_hw_ramp(int enable)
{
	int option3_reg, option2_reg, rv;

	rv = raw_read16(BQ25703_REG_CHARGE_OPTION_3, &option3_reg);
	if (rv)
		return rv;
	rv = raw_read16(BQ25703_REG_CHARGE_OPTION_2, &option2_reg);
	if (rv)
		return rv;

	if (enable) {
		/* Set InputVoltage register to BC1.2 minimum ramp voltage */
		rv = raw_write16(BQ25703_REG_INPUT_VOLTAGE,
			BQ25703_BC12_MIN_VOLTAGE_MV);
		if (rv)
			return rv;

		/*  Enable ICO algorithm */
		option3_reg |= BQ25703_CHARGE_OPTION_3_EN_ICO_MODE;

		/* 0b: Input current limit is set by BQ25703_REG_IIN_HOST */
		option2_reg &= ~BQ25703_CHARGE_OPTION_2_EN_EXTILIM;

		/* Charge ramp may take up to 2s to settle down */
		hook_call_deferred(&bq25703_chg_ramp_handle_data, (4 * SECOND));
	} else {
		/*  Disable ICO algorithm */
		option3_reg &= ~BQ25703_CHARGE_OPTION_3_EN_ICO_MODE;

		/*
		 * 1b: Input current limit is set by the lower value of
		 * ILIM_HIZ pin and BQ25703_REG_IIN_HOST
		 */
		option2_reg |= BQ25703_CHARGE_OPTION_2_EN_EXTILIM;
	}

	rv = raw_write16(BQ25703_REG_CHARGE_OPTION_2, option2_reg);
	if (rv)
		return rv;
	return raw_write16(BQ25703_REG_CHARGE_OPTION_3, option3_reg);
}

int chg_ramp_is_stable(void)
{
	int reg;

	if (raw_read16(BQ25703_REG_CHARGER_STATUS, &reg))
		return 0;

	return reg & BQ25703_CHARGE_STATUS_ICO_DONE;
}

int chg_ramp_get_current_limit(void)
{
	int reg;
	int mode;
	int tries_left = 8;

	/* Save current mode to restore same state after ADC read */
	if (bq25703_get_low_power_mode(&mode))
		goto error;

	/* Exit low power mode so ADC conversion takes typical time */
	if (bq25703_set_low_power_mode(0))
		goto error;

	/* Turn on the ADC for one reading */
	reg = BQ25703_ADC_OPTION_ADC_START | BQ25703_ADC_OPTION_EN_ADC_IIN;
	if (raw_write16(BQ25703_REG_ADC_OPTION, reg))
		goto error;

	/*
	 * Wait until the ADC operation completes. The spec says typical
	 * conversion time is 10 msec. If low power mode isn't exited first,
	 * then the conversion time jumps to ~60 msec.
	 */
	do {
		msleep(2);
		raw_read16(BQ25703_REG_ADC_OPTION, &reg);
	} while (--tries_left && (reg & BQ25703_ADC_OPTION_ADC_START));

	/* ADC reading attempt complete, go back to low power mode */
	if (bq25703_set_low_power_mode(mode))
		goto error;

	/* Could not complete read */
	if (reg & BQ25703_ADC_OPTION_ADC_START)
		goto error;

	/* Read ADC value */
	if (raw_read8(BQ25703_REG_ADC_IIN, &reg))
		goto error;

	/* LSB => 50mA */
	return reg * BQ25703_ADC_IIN_STEP_MA;

error:
	CPRINTF("Could not read input current limit ADC!\n");
	return 0;
}
#endif /* CONFIG_CHARGE_RAMP_HW */
