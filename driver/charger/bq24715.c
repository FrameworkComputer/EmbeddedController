/* Copyright 2013 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * TI bq24715 battery charger driver.
 */

#include "battery_smart.h"
#include "bq24715.h"
#include "charger.h"
#include "common.h"
#include "console.h"
#include "i2c.h"
#include "util.h"

/* Sense resistor configurations and macros */
#define DEFAULT_SENSE_RESISTOR 10
#define R_SNS CONFIG_CHARGER_SENSE_RESISTOR
#define R_AC CONFIG_CHARGER_SENSE_RESISTOR_AC
#define REG_TO_CURRENT(REG, RS) ((REG) * DEFAULT_SENSE_RESISTOR / (RS))
#define CURRENT_TO_REG(CUR, RS) ((CUR) * (RS) / DEFAULT_SENSE_RESISTOR)

/* Note: it is assumed that the sense resistors are 10mOhm. */

static const struct charger_info bq24715_charger_info = {
	.name = "bq24715",
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

static inline enum ec_error_list sbc_read(int chgnum, int cmd, int *param)
{
	return i2c_read16(chg_chips[chgnum].i2c_port,
			  chg_chips[chgnum].i2c_addr_flags, cmd, param);
}

static inline enum ec_error_list sbc_write(int chgnum, int cmd, int param)
{
	return i2c_write16(chg_chips[chgnum].i2c_port,
			   chg_chips[chgnum].i2c_addr_flags, cmd, param);
}

static enum ec_error_list bq24715_set_input_current_limit(int chgnum,
							  int input_current)
{
	return sbc_write(chgnum, BQ24715_INPUT_CURRENT,
			 CURRENT_TO_REG(input_current, R_AC));
}

static enum ec_error_list bq24715_get_input_current_limit(int chgnum,
							  int *input_current)
{
	int rv;
	int reg;

	rv = sbc_read(chgnum, BQ24715_INPUT_CURRENT, &reg);
	if (rv)
		return rv;

	*input_current = REG_TO_CURRENT(reg, R_AC);

	return EC_SUCCESS;
}

static enum ec_error_list bq24715_manufacturer_id(int chgnum, int *id)
{
	return sbc_read(chgnum, BQ24715_MANUFACTURER_ID, id);
}

static enum ec_error_list bq24715_device_id(int chgnum, int *id)
{
	return sbc_read(chgnum, BQ24715_DEVICE_ID, id);
}

static enum ec_error_list bq24715_get_option(int chgnum, int *option)
{
	return sbc_read(chgnum, BQ24715_CHARGE_OPTION, option);
}

static enum ec_error_list bq24715_set_option(int chgnum, int option)
{
	return sbc_write(chgnum, BQ24715_CHARGE_OPTION, option);
}

/* Charger interfaces */

static const struct charger_info *bq24715_get_info(int chgnum)
{
	return &bq24715_charger_info;
}

static enum ec_error_list bq24715_get_status(int chgnum, int *status)
{
	int rv;
	int option;

	rv = bq24715_get_option(chgnum, &option);
	if (rv)
		return rv;

	/* Default status */
	*status = CHARGER_LEVEL_2;

	if ((option & OPT_CHARGE_INHIBIT_MASK) == OPT_CHARGE_DISABLE)
		*status |= CHARGER_CHARGE_INHIBITED;

	return EC_SUCCESS;
}

static enum ec_error_list bq24715_set_mode(int chgnum, int mode)
{
	int rv;
	int option;

	rv = bq24715_get_option(chgnum, &option);
	if (rv)
		return rv;

	option &= ~OPT_CHARGE_INHIBIT_MASK;
	if (mode & CHARGE_FLAG_INHIBIT_CHARGE)
		option |= OPT_CHARGE_DISABLE;
	else
		option |= OPT_CHARGE_ENABLE;
	return bq24715_set_option(chgnum, option);
}

static enum ec_error_list bq24715_get_current(int chgnum, int *current)
{
	int rv;
	int reg;

	rv = sbc_read(chgnum, SB_CHARGING_CURRENT, &reg);
	if (rv)
		return rv;

	*current = REG_TO_CURRENT(reg, R_SNS);
	return EC_SUCCESS;
}

static enum ec_error_list bq24715_set_current(int chgnum, int current)
{
	current = charger_closest_current(current);

	return sbc_write(chgnum, SB_CHARGING_CURRENT,
			 CURRENT_TO_REG(current, R_SNS));
}

/* The voltage setting needs to be cached to work with the current
 * charging infrastructure and state machine. The reason is that
 * the state machine expects to be able to set a 0V charging voltage.
 * The bq24715 does not allow this in the hardware register. Therefore
 * 0V is handled specially to appease the state machine. */
static int cached_voltage;

static enum ec_error_list bq24715_get_voltage(int chgnum, int *voltage)
{
	int ret;

	if (cached_voltage == 0) {
		*voltage = cached_voltage;
		return EC_SUCCESS;
	}

	ret = sbc_read(chgnum, SB_CHARGING_VOLTAGE, &cached_voltage);

	if (ret == EC_SUCCESS)
		*voltage = cached_voltage;

	return ret;
}

static enum ec_error_list bq24715_set_voltage(int chgnum, int voltage)
{
	cached_voltage = voltage;
	return sbc_write(chgnum, SB_CHARGING_VOLTAGE, voltage);
}

/* Charging power state initialization */
static enum ec_error_list bq24715_post_init(int chgnum)
{
	int rv;
	int option;

	rv = bq24715_get_option(chgnum, &option);
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

	rv = bq24715_set_option(chgnum, option);
	if (rv)
		return rv;

	rv = bq24715_set_input_current_limit(
		chgnum, CONFIG_CHARGER_DEFAULT_CURRENT_LIMIT);
	return rv;
}

static enum ec_error_list bq24715_discharge_on_ac(int chgnum, int enable)
{
	int rv;
	int option;

	rv = bq24715_get_option(chgnum, &option);
	if (rv)
		return rv;

	option &= ~OPT_LEARN_MASK;
	if (enable)
		option |= OPT_LEARN_ENABLE;
	else
		option |= OPT_LEARN_DISABLE;
	rv = bq24715_set_option(chgnum, option);

	return rv;
}

const struct charger_drv bq24715_drv = {
	.post_init = &bq24715_post_init,
	.get_info = &bq24715_get_info,
	.get_status = &bq24715_get_status,
	.set_mode = &bq24715_set_mode,
	.get_current = &bq24715_get_current,
	.set_current = &bq24715_set_current,
	.get_voltage = &bq24715_get_voltage,
	.set_voltage = &bq24715_set_voltage,
	.discharge_on_ac = &bq24715_discharge_on_ac,
	.set_input_current_limit = &bq24715_set_input_current_limit,
	.get_input_current_limit = &bq24715_get_input_current_limit,
	.manufacturer_id = &bq24715_manufacturer_id,
	.device_id = &bq24715_device_id,
	.get_option = &bq24715_get_option,
	.set_option = &bq24715_set_option,
};
