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

const struct charger_info *charger_get_info(void)
{
	return &mock_charger_info;
}


int charger_get_status(int *status)
{
	*status = CHARGER_LEVEL_2;
	if (mock_mode & CHARGE_FLAG_INHIBIT_CHARGE)
		*status |= CHARGER_CHARGE_INHIBITED;

	return EC_SUCCESS;
}


int charger_set_mode(int mode)
{
	if (mode & CHARGE_FLAG_INHIBIT_CHARGE)
		mock_mode |= OPTION_CHARGE_INHIBIT;
	else
		mock_mode &= ~OPTION_CHARGE_INHIBIT;
	return EC_SUCCESS;
}


int charger_get_current(int *current)
{
	*current = mock_current;
	return EC_SUCCESS;
}


int charger_set_current(int current)
{
	const struct charger_info *info = charger_get_info();

	if (current > 0 && current < info->current_min)
		current = info->current_min;
	if (current > info->current_max)
		current = info->current_max;

	if (mock_current != current)
		ccprintf("Charger set current: %d\n", current);
	mock_current = current;
	return EC_SUCCESS;
}

int charger_get_voltage(int *voltage)
{
	*voltage = mock_voltage;
	return EC_SUCCESS;
}


int charger_set_voltage(int voltage)
{
	mock_voltage = voltage;
	ccprintf("Charger set voltage: %d\n", voltage);
	return EC_SUCCESS;
}


int charger_get_option(int *option)
{
	*option = mock_option;
	return EC_SUCCESS;
}


int charger_set_option(int option)
{
	mock_option = option;
	return EC_SUCCESS;
}


int charger_manufacturer_id(int *id)
{
	return EC_SUCCESS;
}


int charger_device_id(int *id)
{
	return EC_SUCCESS;
}


int charger_get_input_current(int *input_current)
{
	*input_current = mock_input_current;
	return EC_SUCCESS;
}


int charger_set_input_current(int current)
{
	const struct charger_info *info = charger_get_info();

	if (current < info->input_current_min)
		current = info->input_current_min;
	if (current > info->input_current_max)
		current = info->input_current_max;

	if (mock_input_current != current)
		ccprintf("Charger set input current: %d\n", current);

	mock_input_current = current;
	return EC_SUCCESS;
}


int charger_post_init(void)
{
	mock_current = mock_input_current = CONFIG_CHARGER_INPUT_CURRENT;
	return EC_SUCCESS;
}
