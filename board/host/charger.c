/* Copyright 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Mock battery charger driver.
 */

#include "battery_smart.h"
#include "charger.h"
#include "console.h"
#include "common.h"
#include "util.h"

static const struct charger_info mock_charger_info = {
	.name         = "MockCharger",
	.voltage_max  = 19200,
	.voltage_min  = 1024,
	.voltage_step = 16,
	.current_max  = 8192,
	.current_min  = 128,
	.current_step = 128,
	.input_current_max  = 8064,
	.input_current_min  = 128,
	.input_current_step = 128,
};

#define OPTION_CHARGE_INHIBIT BIT(0)

static uint32_t mock_option;
static uint32_t mock_mode;
static uint32_t mock_current;
static uint32_t mock_voltage;
static uint32_t mock_input_current;

static const struct charger_info *mock_get_info(int chgnum)
{
	return &mock_charger_info;
}


static enum ec_error_list mock_get_status(int chgnum, int *status)
{
	*status = CHARGER_LEVEL_2;
	if (mock_mode & CHARGE_FLAG_INHIBIT_CHARGE)
		*status |= CHARGER_CHARGE_INHIBITED;

	return EC_SUCCESS;
}


static enum ec_error_list mock_set_mode(int chgnum, int mode)
{
	if (mode & CHARGE_FLAG_INHIBIT_CHARGE)
		mock_mode |= OPTION_CHARGE_INHIBIT;
	else
		mock_mode &= ~OPTION_CHARGE_INHIBIT;
	return EC_SUCCESS;
}


static enum ec_error_list mock_get_current(int chgnum, int *current)
{
	*current = mock_current;
	return EC_SUCCESS;
}


static enum ec_error_list mock_set_current(int chgnum, int current)
{
	const struct charger_info *info = mock_get_info(chgnum);

	if (current > 0 && current < info->current_min)
		current = info->current_min;
	if (current > info->current_max)
		current = info->current_max;

	if (mock_current != current)
		ccprintf("Charger set current: %d\n", current);
	mock_current = current;
	return EC_SUCCESS;
}

static enum ec_error_list mock_get_voltage(int chgnum, int *voltage)
{
	*voltage = mock_voltage;
	return EC_SUCCESS;
}


static enum ec_error_list mock_set_voltage(int chgnum, int voltage)
{
	mock_voltage = voltage;
	ccprintf("Charger set voltage: %d\n", voltage);
	return EC_SUCCESS;
}


static enum ec_error_list mock_get_option(int chgnum, int *option)
{
	*option = mock_option;
	return EC_SUCCESS;
}


static enum ec_error_list mock_set_option(int chgnum, int option)
{
	mock_option = option;
	return EC_SUCCESS;
}


static enum ec_error_list mock_manufacturer_id(int chgnum, int *id)
{
	return EC_SUCCESS;
}


static enum ec_error_list mock_device_id(int chgnum, int *id)
{
	return EC_SUCCESS;
}


static enum ec_error_list mock_get_input_current(int chgnum, int *input_current)
{
	*input_current = mock_input_current;
	return EC_SUCCESS;
}


static enum ec_error_list mock_set_input_current(int chgnum, int current)
{
	const struct charger_info *info = mock_get_info(chgnum);

	if (current < info->input_current_min)
		current = info->input_current_min;
	if (current > info->input_current_max)
		current = info->input_current_max;

	if (mock_input_current != current)
		ccprintf("Charger set input current: %d\n", current);

	mock_input_current = current;
	return EC_SUCCESS;
}


static enum ec_error_list mock_post_init(int chgnum)
{
	mock_current = mock_input_current = CONFIG_CHARGER_INPUT_CURRENT;
	return EC_SUCCESS;
}

const struct charger_drv mock_drv = {
	.post_init = &mock_post_init,
	.get_info = &mock_get_info,
	.get_status = &mock_get_status,
	.set_mode = &mock_set_mode,
	.get_current = &mock_get_current,
	.set_current = &mock_set_current,
	.get_voltage = &mock_get_voltage,
	.set_voltage = &mock_set_voltage,
	.set_input_current = &mock_set_input_current,
	.get_input_current = &mock_get_input_current,
	.manufacturer_id = &mock_manufacturer_id,
	.device_id = &mock_device_id,
	.get_option = &mock_get_option,
	.set_option = &mock_set_option,
};

const struct charger_config_t chg_chips[] = {
	{
		.drv = &mock_drv,
	},
};
