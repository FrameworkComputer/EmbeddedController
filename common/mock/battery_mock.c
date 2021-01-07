/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "battery.h"
#include "string.h"

/*****************************************************************************
 * Battery functions needed to enable CONFIG_BATTERY
 */
static int battery_soc_value = 100;
int board_get_battery_soc(void)
{
	return battery_soc_value;
}
void set_battery_soc(int new_value)
{
	battery_soc_value = new_value;
}

static int battery_status_value;
int battery_status(int *status)
{
	*status = battery_status_value;
	return EC_SUCCESS;
}
void set_battery_status(int new_value)
{
	battery_status_value = new_value;
}

static int battery_serial_number_value;
int battery_serial_number(int *serial)
{
	*serial = battery_serial_number_value;
	return EC_SUCCESS;
}
void set_battery_serial_number(int new_value)
{
	battery_serial_number_value = new_value;
}

static int battery_design_voltage_value = 5000;
int battery_design_voltage(int *voltage)
{
	*voltage = battery_design_voltage_value;
	return EC_SUCCESS;
}
void set_battery_design_voltage(int new_value)
{
	battery_design_voltage_value = new_value;
}

static int battery_mode_value;
int battery_get_mode(int *mode)
{
	*mode = battery_mode_value;
	return EC_SUCCESS;
}
void set_battery_mode(int new_value)
{
	battery_mode_value = new_value;
}

static int battery_soc_abs_value = 100;
int battery_state_of_charge_abs(int *percent)
{
	*percent = battery_soc_abs_value;
	return EC_SUCCESS;
}
void set_battery_soc_abs(int new_value)
{
	battery_soc_abs_value = new_value;
}

static int battery_remaining_capacity_value = 100;
int battery_remaining_capacity(int *capacity)
{
	*capacity = battery_remaining_capacity_value;
	return EC_SUCCESS;
}
void set_battery_remaining_capacity(int new_value)
{
	battery_remaining_capacity_value = new_value;
}

static int battery_full_charge_capacity_value = 100;
int battery_full_charge_capacity(int *capacity)
{
	*capacity = battery_full_charge_capacity_value;
	return EC_SUCCESS;
}
void set_battery_full_charge_capacity(int new_value)
{
	battery_full_charge_capacity_value = new_value;
}

static int battery_design_capacity_value = 100;
int battery_design_capacity(int *capacity)
{
	*capacity = battery_design_capacity_value;
	return EC_SUCCESS;
}
void set_battery_design_capacity(int new_value)
{
	battery_design_capacity_value = new_value;
}

static int battery_time_to_empty_value = 60;
int battery_time_to_empty(int *minutes)
{
	*minutes = battery_time_to_empty_value;
	return EC_SUCCESS;
}
void set_battery_time_to_empty(int new_value)
{
	battery_time_to_empty_value = new_value;
}

static int battery_run_time_to_empty_value = 60;
int battery_run_time_to_empty(int *minutes)
{
	*minutes = battery_run_time_to_empty_value;
	return EC_SUCCESS;
}
void set_battery_run_time_to_empty(int new_value)
{
	battery_run_time_to_empty_value = new_value;
}

static int battery_time_to_full_value;
int battery_time_to_full(int *minutes)
{
	*minutes = battery_time_to_full_value;
	return EC_SUCCESS;
}
void set_battery_time_to_full(int new_value)
{
	battery_time_to_full_value = new_value;
}

#define MAX_DEVICE_NAME_LENGTH 40
static char battery_device_name_value[MAX_DEVICE_NAME_LENGTH+1] = "?";
int battery_device_name(char *dest, int size)
{
	int i;

	for (i = 0; i < size && i < MAX_DEVICE_NAME_LENGTH; ++i)
		dest[i] = battery_device_name_value[i];
	for (; i < size; ++i)
		dest[i] = '\0';
	return EC_SUCCESS;
}
void set_battery_device_name(char *new_value)
{
	int i;
	int size = strlen(new_value);

	for (i = 0; i < size && i < MAX_DEVICE_NAME_LENGTH; ++i)
		battery_device_name_value[i] = new_value[i];
	for (; i < MAX_DEVICE_NAME_LENGTH+1; ++i)
		battery_device_name_value[i] = '\0';
}

#define MAX_DEVICE_CHEMISTRY_LENGTH 40
static char battery_device_chemistry_value[MAX_DEVICE_CHEMISTRY_LENGTH+1] = "?";
int battery_device_chemistry(char *dest, int size)
{
	int i;

	for (i = 0; i < size && i < MAX_DEVICE_CHEMISTRY_LENGTH; ++i)
		dest[i] = battery_device_chemistry_value[i];
	for (; i < size; ++i)
		dest[i] = '\0';
	return EC_SUCCESS;
}
void set_battery_device_chemistry(char *new_value)
{
	int i;
	int size = strlen(new_value);

	for (i = 0; i < size && i < MAX_DEVICE_CHEMISTRY_LENGTH; ++i)
		battery_device_chemistry_value[i] = new_value[i];
	for (; i < MAX_DEVICE_CHEMISTRY_LENGTH+1; ++i)
		battery_device_chemistry_value[i] = '\0';
}

static int battery_current_value = 3000;
static int battery_desired_current_value = 3000;
static int battery_desired_voltage_value = 5000;
static int battery_is_present_value = BP_YES;
static int battery_temperature_value = 20;
static int battery_voltage_value = 5000;
void battery_get_params(struct batt_params *batt)
{
	struct batt_params batt_new = {0};

	batt_new.temperature = battery_temperature_value;
	batt_new.state_of_charge = battery_soc_value;
	batt_new.voltage = battery_voltage_value;
	batt_new.current = battery_current_value;
	batt_new.desired_voltage = battery_desired_voltage_value;
	batt_new.desired_current = battery_desired_current_value;
	batt_new.remaining_capacity = battery_remaining_capacity_value;
	batt_new.full_capacity = battery_full_charge_capacity_value;
	batt_new.status = battery_status_value;
	batt_new.is_present = battery_is_present_value;

	memcpy(batt, &batt_new, sizeof(*batt));
}
