/* Copyright 2013 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Battery driver for BQ27541/BQ27542/BQ27741/BQ27742.
 */

#include "battery.h"
#include "battery_smart.h"
#include "console.h"
#include "extpower.h"
#include "hooks.h"
#include "i2c.h"
#include "util.h"

#define BQ27541_ADDR_FLAGS 0x55
#define BQ27541_TYPE_ID 0x0541
#define BQ27542_TYPE_ID 0x0542
#define BQ27741_TYPE_ID 0x0741
#define BQ27742_TYPE_ID 0x0742

#define REG_CTRL 0x00
#define REG_AT_RATE 0x02
#define REG_AT_RATE_TIME_TO_EMPTY 0x04
#define REG_TEMPERATURE 0x06
#define REG_VOLTAGE 0x08
#define REG_FLAGS 0x0a
#define REG_NOMINAL_CAPACITY 0x0c
#define REG_FULL_AVAILABLE_CAPACITY 0x0e
#define REG_REMAINING_CAPACITY 0x10
#define REG_FULL_CHARGE_CAPACITY 0x12
#define REG_AVERAGE_CURRENT 0x14
#define REG_TIME_TO_EMPTY 0x16
#define REG_TIME_TO_FULL 0x18
#define REG_STANDBY_CURRENT 0x1a
#define REG_STANDBY_TIME_TO_EMPTY 0x1c
#define REG_MAX_LOAD_CURRENT 0x1e
#define REG_MAX_LOAD_TIME_TO_EMPTY 0x20
#define REG_AVAILABLE_ENERGY 0x22
#define REG_AVERAGE_POEWR 0x24
#define REG_TT_EAT_CONSTANT_POWER 0x26
#define REG_CYCLE_COUNT 0x2a
#define REG_STATE_OF_CHARGE 0x2c
#define REG_DATA_FLASH_BLOCK 0x3f
#define REG_DESIGN_CAPACITY 0x3c
#define REG_MANUFACTURER_INFO 0x52
#define REG_DEVICE_NAME_LENGTH 0x62
#define MAX_DEVICE_NAME_LENGTH 7
#define REG_DEVICE_NAME 0x63
#define REG_PROTECTOR 0x6d

/* Over-charge */
#define BQ27542_FLAG_BATHI BIT(13)
/* Over Temperature in discharge */
#define BQ27542_FLAG_OTD BIT(11)
/* Over Temperature in charge */
#define BQ27542_FLAG_OTC BIT(7)
/* Charge allowed */
#define BQ27542_FLAG_CHG BIT(3)
/* Discharge */
#define BQ27542_FLAG_DSG BIT(0)

static int battery_type_id;
static int fake_state_of_charge = -1;

static int bq27541_read(int offset, int *data)
{
	return i2c_read16(I2C_PORT_BATTERY, BQ27541_ADDR, offset, data);
}

static int bq27541_read8(int offset, int *data)
{
	return i2c_read8(I2C_PORT_BATTERY, BQ27541_ADDR, offset, data);
}

static int bq27541_write(int offset, int data)
{
	return i2c_write16(I2C_PORT_BATTERY, BQ27541_ADDR, offset, data);
}

int bq27541_probe(void)
{
	int rv;

	rv = bq27541_write(REG_CTRL, 0x1);
	rv |= bq27541_read(REG_CTRL, &battery_type_id);
	/* Read twice to get the right value */
	rv |= bq27541_read(REG_CTRL, &battery_type_id);
	if (rv)
		return rv;
	if (battery_type_id == BQ27541_TYPE_ID ||
	    battery_type_id == BQ27542_TYPE_ID ||
	    battery_type_id == BQ27741_TYPE_ID ||
	    battery_type_id == BQ27742_TYPE_ID)
		return EC_SUCCESS;
	return EC_ERROR_UNKNOWN;
}

static void probe_type_id(void)
{
	bq27541_probe();
}
DECLARE_HOOK(HOOK_INIT, probe_type_id, HOOK_PRIO_DEFAULT);

int battery_device_name(char *device_name, int buf_size)
{
	int rv, i, val;
	int len = MIN(7, buf_size - 1);

	if (battery_type_id == BQ27742_TYPE_ID) {
		/* No device name register available */
		strzcpy(device_name, "<BATT>", len);
		return 0;
	}
	/* Battery pack vendor specific */
	if (battery_type_id == BQ27542_TYPE_ID) {
		rv = bq27541_write(REG_DATA_FLASH_BLOCK, 0x1);
		for (i = 0; i < len; ++i) {
			rv |= bq27541_read8(REG_MANUFACTURER_INFO + i, &val);
			device_name[i] = val;
		}
		device_name[i] = '\0';
		return rv;
	}

	rv = bq27541_read8(REG_DEVICE_NAME_LENGTH, &val);
	if (rv)
		return rv;
	len = MIN(len, val);

	for (i = 0; i < len; ++i) {
		rv |= bq27541_read8(REG_DEVICE_NAME + i, &val);
		device_name[i] = val;
	}
	device_name[i] = '\0';

	return rv;
}

int battery_state_of_charge_abs(int *percent)
{
	return bq27541_read(REG_STATE_OF_CHARGE, percent);
}

int battery_remaining_capacity(int *capacity)
{
	return bq27541_read(REG_REMAINING_CAPACITY, capacity);
}

int battery_full_charge_capacity(int *capacity)
{
	return bq27541_read(REG_FULL_CHARGE_CAPACITY, capacity);
}

int battery_time_to_empty(int *minutes)
{
	return bq27541_read(REG_TIME_TO_EMPTY, minutes);
}

int battery_time_to_full(int *minutes)
{
	return bq27541_read(REG_TIME_TO_FULL, minutes);
}

int battery_cycle_count(int *count)
{
	return bq27541_read(REG_CYCLE_COUNT, count);
}

int battery_design_capacity(int *capacity)
{
	return bq27541_read(REG_DESIGN_CAPACITY, capacity);
}

int battery_time_at_rate(int rate, int *minutes)
{
	int rv;

	if (battery_type_id == BQ27542_TYPE_ID)
		return EC_ERROR_UNIMPLEMENTED;

	rv = bq27541_write(REG_AT_RATE, rate);
	if (rv)
		return rv;
	return bq27541_read(REG_AT_RATE_TIME_TO_EMPTY, minutes);
}

int battery_device_chemistry(char *dest, int size)
{
	strzcpy(dest, "<unkn>", size);

	return EC_SUCCESS;
}

int battery_serial_number(int *serial)
{
	*serial = 0x0BAD0BAD;

	return EC_SUCCESS;
}

int battery_manufacture_date(int *year, int *month, int *day)
{
	return EC_ERROR_UNIMPLEMENTED;
}

int battery_design_voltage(int *voltage)
{
	*voltage = battery_get_info()->voltage_normal;

	return EC_SUCCESS;
}

/**
 * Check if battery allows charging.
 *
 * @param allowed	Non-zero if charging allowed; zero if not allowed.
 * @return non-zero if error.
 */
static int battery_charging_allowed(int *allowed)
{
	int rv, val;

	rv = bq27541_read(REG_FLAGS, &val);
	if (rv)
		return rv;
	if (battery_type_id == BQ27541_TYPE_ID ||
	    battery_type_id == BQ27741_TYPE_ID)
		*allowed = (val & 0x100);
	else /* BQ27742_TYPE_ID, BQ27542_TYPE_ID */
		*allowed = (val & 0x8);

	return EC_SUCCESS;
}

int battery_get_mode(int *mode)
{
	return EC_ERROR_UNIMPLEMENTED;
}

int battery_status(int *status)
{
	int rv;
	int flag = 0;

	*status = 0;
	if (battery_type_id == BQ27542_TYPE_ID) {
		rv = bq27541_read(REG_FLAGS, &flag);
		if (rv)
			return rv;

		if (flag & (BQ27542_FLAG_OTC | BQ27542_FLAG_OTD))
			*status |= STATUS_OVERTEMP_ALARM;
		if (flag & BQ27542_FLAG_DSG)
			*status |= STATUS_DISCHARGING;
		if (flag & BQ27542_FLAG_BATHI)
			*status |= STATUS_OVERCHARGED_ALARM;

		return EC_SUCCESS;
	}

	return EC_ERROR_UNIMPLEMENTED;
}

enum battery_present battery_is_present(void)
{
	int v;
	if (bq27541_read(REG_TEMPERATURE, &v))
		return BP_NO;
	return BP_YES;
}

void battery_get_params(struct batt_params *batt)
{
	int v;
	const uint32_t flags_to_check =
		BATT_FLAG_BAD_TEMPERATURE | BATT_FLAG_BAD_STATE_OF_CHARGE |
		BATT_FLAG_BAD_VOLTAGE | BATT_FLAG_BAD_CURRENT;

	/* Reset flags */
	batt->flags = 0;

	if (bq27541_read(REG_TEMPERATURE, &batt->temperature))
		batt->flags |= BATT_FLAG_BAD_TEMPERATURE;

	if (bq27541_read8(REG_STATE_OF_CHARGE, &v) && fake_state_of_charge < 0)
		batt->flags |= BATT_FLAG_BAD_STATE_OF_CHARGE;

	batt->state_of_charge =
		fake_state_of_charge >= 0 ? fake_state_of_charge : v;

	if (bq27541_read(REG_VOLTAGE, &batt->voltage))
		batt->flags |= BATT_FLAG_BAD_VOLTAGE;

	v = 0;
	if (bq27541_read(REG_AVERAGE_CURRENT, &v))
		batt->flags |= BATT_FLAG_BAD_CURRENT;
	batt->current = (int16_t)v;

	if (battery_remaining_capacity(&batt->remaining_capacity))
		batt->flags |= BATT_FLAG_BAD_REMAINING_CAPACITY;

	if (battery_full_charge_capacity(&batt->full_capacity))
		batt->flags |= BATT_FLAG_BAD_FULL_CAPACITY;

	/* Default to not desiring voltage and current */
	batt->desired_voltage = batt->desired_current = 0;

	/* If any of those reads worked, the battery is responsive */
	if ((batt->flags & flags_to_check) != flags_to_check) {
		batt->flags |= BATT_FLAG_RESPONSIVE;
		batt->is_present = BP_YES;
	} else {
		/* If all of those reads error, the battery is not present */
		batt->is_present = BP_NO;
	}

	/* update the battery status */
	if (battery_status(&batt->status))
		batt->flags |= BATT_FLAG_BAD_STATUS;

	v = 0;
	if (battery_charging_allowed(&v)) {
		batt->flags |= BATT_FLAG_BAD_ANY;
	} else if (v) {
		batt->flags |= BATT_FLAG_WANT_CHARGE;

		/*
		 * Desired voltage and current are not provided by the battery.
		 * So ask for battery's max voltage and an arbitrarily large
		 * current.
		 */
		batt->desired_voltage = battery_get_info()->voltage_max;
		batt->desired_current = 4096;
	}
}

/* Wait until battery is totally stable */
int battery_wait_for_stable(void)
{
	/* TODO(crosbug.com/p/30426): implement me */
	return EC_SUCCESS;
}

#ifdef CONFIG_BATTERY_REVIVE_DISCONNECT
/*
 * Check if battery is in disconnect state, a state entered by pulling
 * BATT_DISCONN_N low, and clear that state if we have external power plugged
 * and no battery faults are detected. Disconnect state resembles battery
 * shutdown mode, but extra steps must be taken to get the battery out of this
 * mode.
 */
enum battery_disconnect_state battery_get_disconnect_state(void)
{
	int val, rv;
	/*
	 * Take note if we find that the battery isn't in disconnect state,
	 * and always return NOT_DISCONNECTED without probing the battery.
	 * This assumes the battery will not go to disconnect state during
	 * runtime.
	 */
	static int not_disconnected;

	if (not_disconnected)
		return BATTERY_NOT_DISCONNECTED;

	if (extpower_is_present()) {
		/* Check DSG_OFF bit */
		rv = bq27541_read(REG_PROTECTOR, &val);
		if (rv)
			return BATTERY_DISCONNECT_ERROR;
		if (!(val & BIT(6))) {
			not_disconnected = 1;
			return BATTERY_NOT_DISCONNECTED;
		}

		/* DSG_OFF is set. Verify this is not due to a safety fault */
		if (val & 0x3f)
			return BATTERY_DISCONNECT_ERROR;
		rv = bq27541_read(REG_FLAGS, &val);
		if (rv)
			return BATTERY_DISCONNECT_ERROR;
		if (val & 0xfc60)
			return BATTERY_DISCONNECT_ERROR;
		return BATTERY_DISCONNECTED;
	}
	not_disconnected = 1;
	return BATTERY_NOT_DISCONNECTED;
}
#endif /* CONFIG_BATTERY_REVIVE_DISCONNECT */

static int command_battfake(int argc, const char **argv)
{
	char *e;
	int v;

	if (argc == 2) {
		v = strtoi(argv[1], &e, 0);
		if (*e || v < -1 || v > 100)
			return EC_ERROR_PARAM1;

		fake_state_of_charge = v;
	}

	if (fake_state_of_charge >= 0)
		ccprintf("Fake batt %d%%\n", fake_state_of_charge);

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(battfake, command_battfake,
			"percent (-1 = use real level)",
			"Set fake battery level");

#ifdef CONFIG_CMD_PWR_AVG
int battery_get_avg_current(void)
{
	int current = -EC_ERROR_UNKNOWN;

	bq27541_read(REG_AVERAGE_CURRENT, &current);
	return current;
}

int battery_get_avg_voltage(void)
{
	/* BQ27541 does not have this parameter */
	return -EC_ERROR_UNIMPLEMENTED;
}
#endif /* CONFIG_CMD_PWR_AVG */
