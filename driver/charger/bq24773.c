/* Copyright 2014 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * TI bq24773 battery charger driver.
 */

#include "battery_smart.h"
#include "bq24773.h"
#include "charger.h"
#include "common.h"
#include "console.h"
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
#define R_AC (CONFIG_CHARGER_SENSE_RESISTOR_AC)
#define REG_TO_CURRENT(REG, RS) ((REG) * DEFAULT_SENSE_RESISTOR / (RS))
#define CURRENT_TO_REG(CUR, RS) ((CUR) * (RS) / DEFAULT_SENSE_RESISTOR)
#define REG8_TO_CURRENT(REG, RS) ((REG) * DEFAULT_SENSE_RESISTOR / (RS) * R8)
#define CURRENT_TO_REG8(CUR, RS) ((CUR) * (RS) / DEFAULT_SENSE_RESISTOR / R8)

/* ChargeCurrent Register - 0x14 (mA) */
#define CHARGE_I_OFF 0
#define CHARGE_I_MIN 128
#define CHARGE_I_MAX 8128
#define CHARGE_I_STEP 64

/* MaxChargeVoltage Register - 0x15 (mV) */
#define CHARGE_V_MIN 1024
#define CHARGE_V_MAX 19200
#define CHARGE_V_STEP 16

/* InputCurrent Register - 0x3f (mA) */
#define INPUT_I_MIN 128
#define INPUT_I_MAX 8128
#define INPUT_I_STEP 64

/* Charger parameters */
static const struct charger_info bq2477x_charger_info = {
	.name = CHARGER_NAME,
	.voltage_max = CHARGE_V_MAX,
	.voltage_min = CHARGE_V_MIN,
	.voltage_step = CHARGE_V_STEP,
	.current_max = REG_TO_CURRENT(CHARGE_I_MAX, R_SNS),
	.current_min = REG_TO_CURRENT(CHARGE_I_MIN, R_SNS),
	.current_step = REG_TO_CURRENT(CHARGE_I_STEP, R_SNS),
	.input_current_max = REG_TO_CURRENT(INPUT_I_MAX, R_AC),
	.input_current_min = REG_TO_CURRENT(INPUT_I_MIN, R_AC),
	.input_current_step = REG_TO_CURRENT(INPUT_I_STEP, R_AC),
};

#ifdef CONFIG_CHARGER_BQ24773
static inline enum ec_error_list raw_read8(int chgnum, int offset, int *value)
{
	return i2c_read8(chg_chips[chgnum].i2c_port,
			 chg_chips[chgnum].i2c_addr_flags, offset, value);
}

static inline enum ec_error_list raw_write8(int chgnum, int offset, int value)
{
	return i2c_write8(chg_chips[chgnum].i2c_port,
			  chg_chips[chgnum].i2c_addr_flags, offset, value);
}
#endif

static inline enum ec_error_list raw_read16(int chgnum, int offset, int *value)
{
	return i2c_read16(chg_chips[chgnum].i2c_port,
			  chg_chips[chgnum].i2c_addr_flags, offset, value);
}

static inline enum ec_error_list raw_write16(int chgnum, int offset, int value)
{
	return i2c_write16(chg_chips[chgnum].i2c_port,
			   chg_chips[chgnum].i2c_addr_flags, offset, value);
}

/* chip specific interfaces */

static enum ec_error_list bq2477x_set_input_current_limit(int chgnum,
							  int input_current)
{
#ifdef CONFIG_CHARGER_BQ24770
	return raw_write16(chgnum, REG_INPUT_CURRENT,
			   CURRENT_TO_REG(input_current, R_AC));
#elif defined(CONFIG_CHARGER_BQ24773)
	return raw_write8(chgnum, REG_INPUT_CURRENT,
			  CURRENT_TO_REG8(input_current, R_AC));
#endif
}

static enum ec_error_list bq2477x_get_input_current_limit(int chgnum,
							  int *input_current)
{
	int rv;
	int reg;

#ifdef CONFIG_CHARGER_BQ24770
	rv = raw_read16(chgnum, REG_INPUT_CURRENT, &reg);
#elif defined(CONFIG_CHARGER_BQ24773)
	rv = raw_read8(chgnum, REG_INPUT_CURRENT, &reg);
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

static enum ec_error_list bq2477x_manufacturer_id(int chgnum, int *id)
{
#ifdef CONFIG_CHARGER_BQ24770
	return raw_read16(chgnum, REG_MANUFACTURE_ID, id);
#elif defined(CONFIG_CHARGER_BQ24773)
	*id = 0x40; /* TI */
	return EC_SUCCESS;
#endif
}

static enum ec_error_list bq2477x_device_id(int chgnum, int *id)
{
#ifdef CONFIG_CHARGER_BQ24770
	return raw_read16(chgnum, REG_DEVICE_ADDRESS, id);
#elif defined(CONFIG_CHARGER_BQ24773)
	return raw_read8(chgnum, REG_DEVICE_ADDRESS, id);
#endif
}

static enum ec_error_list bq2477x_get_option(int chgnum, int *option)
{
	return raw_read16(chgnum, REG_CHARGE_OPTION0, option);
}

static enum ec_error_list bq2477x_set_option(int chgnum, int option)
{
	return raw_write16(chgnum, REG_CHARGE_OPTION0, option);
}

/* Charger interfaces */

static const struct charger_info *bq2477x_get_info(int chgnum)
{
	return &bq2477x_charger_info;
}

static enum ec_error_list bq2477x_get_status(int chgnum, int *status)
{
	int rv;
	int option;

	rv = bq2477x_get_option(chgnum, &option);
	if (rv)
		return rv;

	/* Default status */
	*status = CHARGER_LEVEL_2;

	if (option & OPTION0_CHARGE_INHIBIT)
		*status |= CHARGER_CHARGE_INHIBITED;

	return EC_SUCCESS;
}

static enum ec_error_list bq2477x_set_mode(int chgnum, int mode)
{
	int rv;
	int option;

	rv = bq2477x_get_option(chgnum, &option);
	if (rv)
		return rv;

	if (mode & CHARGE_FLAG_INHIBIT_CHARGE)
		option |= OPTION0_CHARGE_INHIBIT;
	else
		option &= ~OPTION0_CHARGE_INHIBIT;
	return bq2477x_set_option(chgnum, option);
}

static enum ec_error_list bq2477x_get_current(int chgnum, int *current)
{
	int rv;
	int reg;

	rv = raw_read16(chgnum, REG_CHARGE_CURRENT, &reg);

	if (rv)
		return rv;

	*current = REG_TO_CURRENT(reg, R_SNS);
	return EC_SUCCESS;
}

static enum ec_error_list bq2477x_set_current(int chgnum, int current)
{
	current = charger_closest_current(current);
	return raw_write16(chgnum, REG_CHARGE_CURRENT,
			   CURRENT_TO_REG(current, R_SNS));
}

static enum ec_error_list bq2477x_get_voltage(int chgnum, int *voltage)
{
	return raw_read16(chgnum, REG_MAX_CHARGE_VOLTAGE, voltage);
}

static enum ec_error_list bq2477x_set_voltage(int chgnum, int voltage)
{
	voltage = charger_closest_voltage(voltage);
	return raw_write16(chgnum, REG_MAX_CHARGE_VOLTAGE, voltage);
}

/* Charging power state initialization */
static enum ec_error_list bq2477x_post_init(int chgnum)
{
	int rv, option;
#ifdef CONFIG_CHARGER_ILIM_PIN_DISABLED
	int option2;
#endif

	rv = bq2477x_get_option(chgnum, &option);
	if (rv)
		return rv;

	option &= ~OPTION0_LEARN_ENABLE;
	rv = bq2477x_set_option(chgnum, option);
	if (rv)
		return rv;

	/* Turn off PROCHOT warning */
	rv = raw_read16(chgnum, REG_PROCHOT_OPTION1, &option);
	if (rv)
		return rv;

	option &= ~PROCHOT_OPTION1_SELECTOR_MASK;
	rv = raw_write16(chgnum, REG_PROCHOT_OPTION1, option);

	if (rv)
		return rv;

#ifdef CONFIG_CHARGER_ILIM_PIN_DISABLED
	/* Read the external ILIM pin enabled flag. */
	rv = raw_read16(chgnum, REG_CHARGE_OPTION2, &option2);
	if (rv)
		return rv;

	/* Set ILIM pin disabled if it is currently enabled. */
	if (option2 & OPTION2_EN_EXTILIM) {
		option2 &= ~OPTION2_EN_EXTILIM;
		rv = raw_write16(chgnum, REG_CHARGE_OPTION2, option2);
	}
	return rv;
#else
	return EC_SUCCESS;
#endif
}

static enum ec_error_list bq2477x_discharge_on_ac(int chgnum, int enable)
{
	int rv;
	int option;

	rv = bq2477x_get_option(chgnum, &option);
	if (rv)
		return rv;

	if (enable)
		rv = bq2477x_set_option(chgnum, option | OPTION0_LEARN_ENABLE);
	else
		rv = bq2477x_set_option(chgnum, option & ~OPTION0_LEARN_ENABLE);

	return rv;
}

const struct charger_drv bq2477x_drv = {
	.post_init = &bq2477x_post_init,
	.get_info = &bq2477x_get_info,
	.get_status = &bq2477x_get_status,
	.set_mode = &bq2477x_set_mode,
	.get_current = &bq2477x_get_current,
	.set_current = &bq2477x_set_current,
	.get_voltage = &bq2477x_get_voltage,
	.set_voltage = &bq2477x_set_voltage,
	.discharge_on_ac = &bq2477x_discharge_on_ac,
	.set_input_current_limit = &bq2477x_set_input_current_limit,
	.get_input_current_limit = &bq2477x_get_input_current_limit,
	.manufacturer_id = &bq2477x_manufacturer_id,
	.device_id = &bq2477x_device_id,
	.get_option = &bq2477x_get_option,
	.set_option = &bq2477x_set_option,
};
