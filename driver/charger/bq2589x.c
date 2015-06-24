/* Copyright 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * TI bq25890/bq25892/bq25895 battery charger driver.
 */

#include "config.h"
#include "bq2589x.h"
#include "charger.h"
#include "common.h"
#include "console.h"
#include "hooks.h"
#include "i2c.h"
#include "printf.h"
#include "util.h"

/* Console output macros */
#define CPUTS(outstr) cputs(CC_CHARGER, outstr)
#define CPRINTF(format, args...) cprintf(CC_CHARGER, format, ## args)

/* 5V Boost settings */
#ifndef CONFIG_CHARGER_BQ2589X_BOOST
#define CONFIG_CHARGER_BQ2589X_BOOST BQ2589X_BOOST_DEFAULT
#endif

/* IR compensation settings */
#ifndef CONFIG_CHARGER_BQ2589X_IR_COMP
#define CONFIG_CHARGER_BQ2589X_IR_COMP BQ2589X_IR_COMP_DEFAULT
#endif

/* Charger information */
static const struct charger_info bq2589x_charger_info = {
	.name         = "bq2589x",
	.voltage_max  = 4608,
	.voltage_min  = 3840,
	.voltage_step = 16,
	.current_max  = 5056,
	.current_min  = 0,
	.current_step = 64,
	.input_current_max  = 3250,
	.input_current_min  = 100,
	.input_current_step = 50,
};

static int bq2589x_read(int reg, int *value)
{
	return i2c_read8(I2C_PORT_CHARGER, BQ2589X_ADDR, reg, value);
}

static int bq2589x_write(int reg, int value)
{
	return i2c_write8(I2C_PORT_CHARGER, BQ2589X_ADDR, reg, value);
}

static int bq2589x_watchdog_reset(void)
{
	int rv, val;

	rv = bq2589x_read(BQ2589X_REG_CFG2, &val);
	if (rv)
		return rv;
	val |= BQ2589X_CFG2_WD_RST;
	return bq2589x_write(BQ2589X_REG_CFG2, val);
}

static int bq2589x_set_terminate_current(int current)
{
	int reg_val, rv;
	int val = (current - 64) / 64;

	rv = bq2589x_read(BQ2589X_REG_PRE_CHG_CURR, &reg_val);
	if (rv)
		return rv;
	reg_val = (reg_val & ~0xf) | (val & 0xf);
	return bq2589x_write(BQ2589X_REG_PRE_CHG_CURR, reg_val);
}

int charger_enable_otg_power(int enabled)
{
	int val, rv;

	rv = bq2589x_read(BQ2589X_REG_CFG2, &val);
	if (rv)
		return rv;
	val = (val & ~(BQ2589X_CFG2_CHG_CONFIG | BQ2589X_CFG2_OTG_CONFIG))
	    | (enabled ? BQ2589X_CFG2_OTG_CONFIG : BQ2589X_CFG2_CHG_CONFIG);
	return bq2589x_write(BQ2589X_REG_CFG2, val);
}

int charger_set_input_current(int input_current)
{
	int value, rv;
	const struct charger_info * const info = charger_get_info();

	input_current -= info->input_current_min;
	if (input_current < 0)
		input_current = 0;

	rv = bq2589x_read(BQ2589X_REG_INPUT_CURR, &value);
	if (rv)
		return rv;
	value = value & ~(0x3f);
	value |= (input_current / info->input_current_step) & 0x3f;
	return bq2589x_write(BQ2589X_REG_INPUT_CURR, value);
}

int charger_get_input_current(int *input_current)
{
	int rv, value;
	const struct charger_info * const info = charger_get_info();

	rv = bq2589x_read(BQ2589X_REG_INPUT_CURR, &value);
	if (rv)
		return rv;

	*input_current = (value & 0x3f) * info->input_current_step
			+ info->input_current_min;

	return EC_SUCCESS;
}

int charger_manufacturer_id(int *id)
{
	return EC_ERROR_UNIMPLEMENTED;
}

int charger_device_id(int *id)
{
	int res = bq2589x_read(BQ2589X_REG_ID, id);

	if (res == EC_SUCCESS)
		*id &= BQ2589X_DEVICE_ID_MASK;

	return res;
}

int charger_get_option(int *option)
{
	/* Ignored: does not exist */
	*option = 0;
	return EC_SUCCESS;
}

int charger_set_option(int option)
{
	/* Ignored: does not exist */
	return EC_SUCCESS;
}

const struct charger_info *charger_get_info(void)
{
	return &bq2589x_charger_info;
}

int charger_get_status(int *status)
{
	/* TODO(crosbug.com/p/38603) implement using REG0C value */
	*status = 0;
	return EC_SUCCESS;
}

int charger_set_mode(int mode)
{
	return EC_SUCCESS;
}

int charger_get_current(int *current)
{
	int rv, val;
	const struct charger_info * const info = charger_get_info();

	rv = bq2589x_read(BQ2589X_REG_CHG_CURR, &val);
	if (rv)
		return rv;
	*current = val * info->current_step + info->current_min;
	return EC_SUCCESS;
}

int charger_set_current(int current)
{
	const struct charger_info * const info = charger_get_info();

	current = charger_closest_current(current);

	return bq2589x_write(BQ2589X_REG_CHG_CURR,
			     current / info->current_step);
}

int charger_get_voltage(int *voltage)
{
	int rv, val;
	const struct charger_info * const info = charger_get_info();

	rv = bq2589x_read(BQ2589X_REG_CHG_VOLT, &val);
	if (rv)
		return rv;
	val = (val >> 2) & 0x3f;
	*voltage = val * info->voltage_step + info->voltage_min;
	return EC_SUCCESS;
}

int charger_set_voltage(int voltage)
{
	int rv, val;
	const struct charger_info * const info = charger_get_info();

	voltage = charger_closest_voltage(voltage);

	rv = bq2589x_read(BQ2589X_REG_CHG_VOLT, &val);
	if (rv)
		return rv;
	val = val & 0x3;
	val |= ((voltage - info->voltage_min) / info->voltage_step) << 2;
	return bq2589x_write(BQ2589X_REG_CHG_VOLT, val);
}

int charger_discharge_on_ac(int enable)
{
	return EC_SUCCESS;
}

/* Charging power state initialization */
int charger_post_init(void)
{
#ifdef CONFIG_CHARGER_ILIM_PIN_DISABLED
	int val, rv;
	/* Ignore ILIM pin value */
	rv = bq2589x_read(BQ2589X_REG_INPUT_CURR, &val);
	if (rv)
		return rv;
	val &= ~BQ2589X_INPUT_CURR_EN_ILIM;
	rv = bq2589x_write(BQ2589X_REG_INPUT_CURR, val);
	if (rv)
		return rv;
#endif /* CONFIG_CHARGER_ILIM_PIN_DISABLED */

	/* Input current controlled by extpower module. Do nothing here. */
	return EC_SUCCESS;
}


/*****************************************************************************/
/* Hooks */

static void bq2589x_init(void)
{
	int val;

	if (charger_device_id(&val) || val != BQ2589X_DEVICE_ID) {
		CPRINTF("BQ2589X incorrent ID: 0x%02x\n", val);
		return;
	}

	/*
	 * Disable I2C watchdog timer.
	 *
	 * TODO(crosbug.com/p/38603): Re-enable watchdog timer and kick it
	 * periodically in charger task.
	 */
	if (bq2589x_read(BQ2589X_REG_TIMER, &val))
		return;
	val &= ~0x30;
	if (bq2589x_write(BQ2589X_REG_TIMER, val))
		return;

	if (bq2589x_set_terminate_current(64))
		return;

	if (bq2589x_watchdog_reset())
		return;

	if (bq2589x_write(BQ2589X_REG_IR_COMP, CONFIG_CHARGER_BQ2589X_IR_COMP))
		return;

	if (bq2589x_write(BQ2589X_REG_BOOST_MODE, CONFIG_CHARGER_BQ2589X_BOOST))
		return;

	CPRINTF("BQ2589%c initialized\n",
		BQ2589X_DEVICE_ID == BQ25890_DEVICE_ID ? '0' :
		(BQ2589X_DEVICE_ID == BQ25895_DEVICE_ID ? '5' : '2'));
}
DECLARE_HOOK(HOOK_INIT, bq2589x_init, HOOK_PRIO_LAST);

/*****************************************************************************/
/* Console commands */

static int command_bq2589x(int argc, char **argv)
{
	int i;
	int value;
	int rv;
	int batt_mv, sys_mv, vbus_mv, chg_ma, input_ma;

	/* Trigger one ADC conversion */
	bq2589x_read(BQ2589X_REG_CFG1, &value);
	bq2589x_write(BQ2589X_REG_CFG1, value | BQ2589X_CFG1_CONV_START);
	do {
		bq2589x_read(BQ2589X_REG_CFG1, &value);
	} while (value & BQ2589X_CFG1_CONV_START); /* Wait for End of Conv. */

	bq2589x_read(BQ2589X_REG_ADC_BATT_VOLT, &batt_mv);
	bq2589x_read(BQ2589X_REG_ADC_SYS_VOLT, &sys_mv);
	bq2589x_read(BQ2589X_REG_ADC_VBUS_VOLT, &vbus_mv);
	bq2589x_read(BQ2589X_REG_ADC_CHG_CURR, &chg_ma);
	bq2589x_read(BQ2589X_REG_ADC_INPUT_CURR, &input_ma);
	ccprintf("ADC Batt %dmV Sys %dmV VBUS %dmV Chg %dmA Input %dmA\n",
		2304 + (batt_mv & 0x7f) * 20, 2304 + sys_mv * 20,
		2600 + (vbus_mv & 0x7f) * 100,
		chg_ma * 50, 100 + (input_ma & 0x3f) * 50);

	ccprintf("REG:");
	for (i = 0; i <= 0x14; ++i)
		ccprintf(" %02x", i);
	ccprintf("\n");

	ccprintf("VAL:");
	for (i = 0; i <= 0x14; ++i) {
		rv = bq2589x_read(i, &value);
		if (rv)
			return rv;
		ccprintf(" %02x", value);
	}
	ccprintf("\n");

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(bq2589x, command_bq2589x,
			NULL, NULL, NULL);
