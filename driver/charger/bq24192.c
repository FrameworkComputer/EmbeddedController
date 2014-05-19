/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * TI bq24192 battery charger driver.
 */

#include "bq24192.h"
#include "charger.h"
#include "common.h"
#include "console.h"
#include "gpio.h"
#include "hooks.h"
#include "i2c.h"
#include "printf.h"
#include "util.h"

/* Console output macros */
#define CPUTS(outstr) cputs(CC_CHARGER, outstr)
#define CPRINTS(format, args...) cprints(CC_CHARGER, format, ## args)

/* Charger information */
static const struct charger_info bq24192_charger_info = {
	.name         = "bq24192",
	.voltage_max  = 4400,
	.voltage_min  = 3504,
	.voltage_step = 16,
	.current_max  = 4544,
	.current_min  = 512,
	.current_step = 64,
	.input_current_max  = 3000,
	.input_current_min  = 100,
	.input_current_step = -1,
};

static const int input_current_steps[] = {
	100, 150, 500, 900, 1200, 1500, 2000, 3000};

static int bq24192_read(int reg, int *value)
{
	return i2c_read8(I2C_PORT_CHARGER, BQ24192_ADDR, reg, value);
}

static int bq24192_write(int reg, int value)
{
	return i2c_write8(I2C_PORT_CHARGER, BQ24192_ADDR, reg, value);
}

static int bq24192_watchdog_reset(void)
{
	int rv, val;

	rv = bq24192_read(BQ24192_REG_POWER_ON_CFG, &val);
	if (rv)
		return rv;
	val |= (1 << 6);
	return bq24192_write(BQ24192_REG_POWER_ON_CFG, val) ||
	       bq24192_write(BQ24192_REG_POWER_ON_CFG, val);
}

static int bq24192_set_terminate_current(int current)
{
	int reg_val, rv;
	int val = (current - 128) / 128;

	rv = bq24192_read(BQ24192_REG_PRE_CHG_CURRENT, &reg_val);
	if (rv)
		return rv;
	reg_val = (reg_val & ~0xf) | (val & 0xf);
	return bq24192_write(BQ24192_REG_PRE_CHG_CURRENT, reg_val);
}

int charger_enable_otg_power(int enabled)
{
	int val, rv;

	gpio_set_level(GPIO_BCHGR_OTG, enabled);
	rv = bq24192_read(BQ24192_REG_POWER_ON_CFG, &val);
	if (rv)
		return rv;
	val = (val & ~0x30) | (enabled ? 0x20 : 0x10);
	return bq24192_write(BQ24192_REG_POWER_ON_CFG, val);
}

int charger_set_input_current(int input_current)
{
	int i, value, rv;

	for (i = 1; i < ARRAY_SIZE(input_current_steps); ++i)
		if (input_current_steps[i] > input_current) {
			--i;
			break;
		}
	if (i == ARRAY_SIZE(input_current_steps))
		--i;

	rv = bq24192_read(BQ24192_REG_INPUT_CTRL, &value);
	if (rv)
		return rv;
	value = value & ~(0x7);
	value |= (i & 0x7);
	return bq24192_write(BQ24192_REG_INPUT_CTRL, value);
}

int charger_get_input_current(int *input_current)
{
	int rv, value;

	rv = bq24192_read(BQ24192_REG_INPUT_CTRL, &value);
	if (rv)
		return rv;
	*input_current = input_current_steps[value & 0x7];
	return EC_SUCCESS;
}

int charger_manufacturer_id(int *id)
{
	return EC_ERROR_UNIMPLEMENTED;
}

int charger_device_id(int *id)
{
	return bq24192_read(BQ24192_REG_ID, id);
}

int charger_get_option(int *option)
{
	return EC_ERROR_UNIMPLEMENTED;
}

int charger_set_option(int option)
{
	return EC_ERROR_UNIMPLEMENTED;
}

const struct charger_info *charger_get_info(void)
{
	return &bq24192_charger_info;
}

int charger_get_status(int *status)
{
	return EC_ERROR_UNIMPLEMENTED;
}

int charger_set_mode(int mode)
{
	return EC_ERROR_UNIMPLEMENTED;
}

int charger_get_current(int *current)
{
	int rv, val;
	const struct charger_info * const info = charger_get_info();

	rv = bq24192_read(BQ24192_REG_CHG_CURRENT, &val);
	if (rv)
		return rv;
	val = (val >> 2) & 0x3f;
	*current = val * info->current_step + info->current_min;
	return EC_SUCCESS;
}

int charger_set_current(int current)
{
	int rv, val;
	const struct charger_info * const info = charger_get_info();

	current = charger_closest_current(current);
	rv = bq24192_read(BQ24192_REG_CHG_CURRENT, &val);
	if (rv)
		return rv;
	val = val & 0x3;
	val |= ((current - info->current_min) / info->current_step) << 2;
	return bq24192_write(BQ24192_REG_CHG_CURRENT, val);
}

int charger_get_voltage(int *voltage)
{
	int rv, val;
	const struct charger_info * const info = charger_get_info();

	rv = bq24192_read(BQ24192_REG_CHG_VOLTAGE, &val);
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

	rv = bq24192_read(BQ24192_REG_CHG_VOLTAGE, &val);
	if (rv)
		return rv;
	val = val & 0x3;
	val |= ((voltage - info->voltage_min) / info->voltage_step) << 2;
	return bq24192_write(BQ24192_REG_CHG_VOLTAGE, val);
}

/* Charging power state initialization */
int charger_post_init(void)
{
	/* Input current controlled by extpower module. Do nothing here. */
	return EC_SUCCESS;
}


/*****************************************************************************/
/* Hooks */

static void bq24192_init(void)
{
	int val;

	if (charger_device_id(&val) || val != BQ24192_DEVICE_ID) {
		CPRINTF("BQ24192 incorrent ID: 0x%02x", val);
		return;
	}

	/*
	 * Disable I2C watchdog timer.
	 *
	 * TODO(crosbug.com/p/22238): Re-enable watchdog timer and kick it
	 * periodically in charger task.
	 */
	if (bq24192_read(BQ24192_REG_CHG_TERM_TMR, &val))
		return;

	val &= ~0x30;

	if (bq24192_write(BQ24192_REG_CHG_TERM_TMR, val))
		return;

	if (bq24192_set_terminate_current(128))
		return;

	if (bq24192_watchdog_reset())
		return;

	CPRINTF("BQ24192 initialized");
}
DECLARE_HOOK(HOOK_INIT, bq24192_init, HOOK_PRIO_LAST);

/*****************************************************************************/
/* Console commands */

static int command_bq24192(int argc, char **argv)
{
	int i;
	int value;
	int rv;

	ccprintf("REG:");
	for (i = 0; i <= 0xa; ++i)
		ccprintf(" %02x", i);
	ccprintf("\n");

	ccprintf("VAL:");
	for (i = 0; i <= 0xa; ++i) {
		rv = bq24192_read(i, &value);
		if (rv)
			return rv;
		ccprintf(" %02x", value);
	}
	ccprintf("\n");

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(bq24192, command_bq24192,
			NULL, NULL, NULL);
