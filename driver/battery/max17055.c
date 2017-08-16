/* Copyright 2017 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Battery driver for MAX17055.
 */

#include "battery.h"
#include "console.h"
#include "extpower.h"
#include "hooks.h"
#include "i2c.h"
#include "timer.h"
#include "util.h"

/* Console output macros */
#define CPRINTS(format, args...) cprints(CC_CHARGER, format, ## args)

#define MAX17055_ADDR               0x6c
#define MAX17055_DEVICE_ID          0x4010

#define REG_STATUS                  0x00
#define REG_AT_RATE                 0x04
#define REG_REMAINING_CAPACITY      0x05
#define REG_STATE_OF_CHARGE         0x06
#define REG_TEMPERATURE             0x08
#define REG_VOLTAGE                 0x09
#define REG_CURRENT                 0x0a
#define REG_AVERAGE_CURRENT         0x0b
#define REG_FULL_CHARGE_CAPACITY    0x10
#define REG_TIME_TO_EMPTY           0x11
#define REG_CONFIG                  0x1D
#define REG_AVERAGE_TEMPERATURE     0x16
#define REG_CYCLE_COUNT             0x17
#define REG_DESIGN_CAPACITY         0x18
#define REG_AVERAGE_VOLTAGE         0x19
#define REG_CHARGE_TERM_CURRENT     0x1e
#define REG_TIME_TO_FULL            0x20
#define REG_DEVICE_NAME             0x21
#define REG_EMPTY_VOLTAGE           0x3a
#define REG_FSTAT                   0x3d
#define REG_DQACC                   0x45
#define REG_DPACC                   0x46
#define REG_STATUS2                 0xb0
#define REG_HIBCFG                  0xba
#define REG_CONFIG2                 0xbb
#define REG_MODELCFG                0xdb

/* Status reg (0x00) flags */
#define STATUS_POR                  0x0002
#define STATUS_BST                  0x0008

/* FStat reg (0x3d) flags */
#define FSTAT_DNR                   0x0001

/* ModelCfg reg (0xdb) flags */
#define MODELCFG_REFRESH            0x8000
#define MODELCFG_VCHG               0x0400

/*
 * Convert the register values to the units that match
 * smart battery protocol.
 */

/* Voltage reg value to mV */
#define VOLTAGE_CONV(REG)       ((REG * 5) >> 6)
/* Current reg value to mA */
#define CURRENT_CONV(REG)       (((REG * 25) >> 4) / BATTERY_MAX17055_RSENSE)
/* Capacity reg value to mAh */
#define CAPACITY_CONV(REG)      (REG * 5 / BATTERY_MAX17055_RSENSE)
/* Time reg value to minute */
#define TIME_CONV(REG)          ((REG * 3) >> 5)
/* Temperature reg value to 0.1K */
#define TEMPERATURE_CONV(REG)   (((REG * 10) >> 8) + 2731)
/* Percentage reg value to 1% */
#define PERCENTAGE_CONV(REG)    (REG >> 8)

static int fake_state_of_charge = -1;

static int max17055_read(int offset, int *data)
{
	return i2c_read16(I2C_PORT_BATTERY, MAX17055_ADDR, offset, data);
}

static int max17055_write(int offset, int data)
{
	return i2c_write16(I2C_PORT_BATTERY, MAX17055_ADDR, offset, data);
}

/* Return 1 if the device id is correct. */
static int max17055_probe(void)
{
	int dev_id;

	if (max17055_read(REG_DEVICE_NAME, &dev_id))
		return 0;
	if (dev_id == MAX17055_DEVICE_ID)
		return 1;
	return 0;
}

int battery_device_name(char *device_name, int buf_size)
{
	return EC_ERROR_UNIMPLEMENTED;
}

int battery_state_of_charge_abs(int *percent)
{
	return EC_ERROR_UNIMPLEMENTED;
}

int battery_remaining_capacity(int *capacity)
{
	int rv;
	int reg;

	rv = max17055_read(REG_REMAINING_CAPACITY, &reg);
	if (!rv)
		*capacity = CAPACITY_CONV(reg);
	return rv;
}

int battery_full_charge_capacity(int *capacity)
{
	int rv;
	int reg;

	rv = max17055_read(REG_FULL_CHARGE_CAPACITY, &reg);
	if (!rv)
		*capacity = CAPACITY_CONV(reg);
	return rv;
}

int battery_time_to_empty(int *minutes)
{
	int rv;
	int reg;

	rv = max17055_read(REG_TIME_TO_EMPTY, &reg);
	if (!rv)
		*minutes = TIME_CONV(reg);
	return rv;
}

int battery_time_to_full(int *minutes)
{
	int rv;
	int reg;

	rv = max17055_read(REG_TIME_TO_FULL, &reg);
	if (!rv)
		*minutes = TIME_CONV(reg);
	return rv;
}

int battery_cycle_count(int *count)
{
	return max17055_read(REG_CYCLE_COUNT, count);
}

int battery_design_capacity(int *capacity)
{
	int rv;
	int reg;

	rv = max17055_read(REG_DESIGN_CAPACITY, &reg);
	if (!rv)
		*capacity = CAPACITY_CONV(reg);
	return rv;
}

int battery_time_at_rate(int rate, int *minutes)
{
	return EC_ERROR_UNIMPLEMENTED;
}

int battery_manufacturer_name(char *dest, int size)
{
	strzcpy(dest, "<unkn>", size);

	return EC_SUCCESS;
}

int battery_device_chemistry(char *dest, int size)
{
	strzcpy(dest, "<unkn>", size);

	return EC_SUCCESS;
}

int battery_serial_number(int *serial)
{
	/* TODO(philipchen): Implement this function. */
	*serial = 0xFFFFFFFF;
	return EC_SUCCESS;
}

int battery_design_voltage(int *voltage)
{
	*voltage = battery_get_info()->voltage_normal;

	return EC_SUCCESS;
}

int battery_get_mode(int *mode)
{
	return EC_ERROR_UNIMPLEMENTED;
}

int battery_status(int *status)
{
	return EC_ERROR_UNIMPLEMENTED;
}

enum battery_present battery_is_present(void)
{
	int status = 0;

	if (max17055_read(REG_STATUS, &status))
		return BP_NOT_SURE;
	if (status & STATUS_BST)
		return BP_NO;
	return BP_YES;
}

void battery_get_params(struct batt_params *batt)
{
	int reg = 0;
	const uint32_t flags_to_check = BATT_FLAG_BAD_TEMPERATURE |
					BATT_FLAG_BAD_STATE_OF_CHARGE |
					BATT_FLAG_BAD_VOLTAGE |
					BATT_FLAG_BAD_CURRENT;

	/* Reset flags */
	batt->flags = 0;

	if (max17055_read(REG_TEMPERATURE, &reg))
		batt->flags |= BATT_FLAG_BAD_TEMPERATURE;

	batt->temperature = TEMPERATURE_CONV((int16_t)reg);

	if (max17055_read(REG_STATE_OF_CHARGE, &reg) &&
	    fake_state_of_charge < 0)
		batt->flags |= BATT_FLAG_BAD_STATE_OF_CHARGE;

	batt->state_of_charge = fake_state_of_charge >= 0 ?
				fake_state_of_charge : PERCENTAGE_CONV(reg);

	if (max17055_read(REG_VOLTAGE, &reg))
		batt->flags |= BATT_FLAG_BAD_VOLTAGE;

	batt->voltage = VOLTAGE_CONV(reg);

	if (max17055_read(REG_AVERAGE_CURRENT, &reg))
		batt->flags |= BATT_FLAG_BAD_CURRENT;

	batt->current = CURRENT_CONV((int16_t)reg);

	/* Default to not desiring voltage and current */
	batt->desired_voltage = batt->desired_current = 0;

	/* If any of those reads worked, the battery is responsive */
	if ((batt->flags & flags_to_check) != flags_to_check) {
		batt->flags |= BATT_FLAG_RESPONSIVE;
		batt->is_present = BP_YES;
	} else
		batt->is_present = BP_NOT_SURE;
}

/* Wait until battery is totally stable. */
int battery_wait_for_stable(void)
{
	/* TODO(philipchen): Implement this function. */
	return EC_SUCCESS;
}

/* Configured MAX17055 with the battery parameters for optimal performance. */
static int max17055_init_config(void)
{
	int reg;
	int hib_cfg;
	int retries = 50;

	if (max17055_write(REG_DESIGN_CAPACITY, BATTERY_MAX17055_DESIGNCAP) ||
	    max17055_write(REG_DQACC, BATTERY_MAX17055_DESIGNCAP / 32) ||
	    max17055_write(REG_CHARGE_TERM_CURRENT, BATTERY_MAX17055_ICHGTERM)
	    || max17055_write(REG_EMPTY_VOLTAGE, BATTERY_MAX17055_VEMPTY))
		return EC_ERROR_UNKNOWN;

	/* Store the original HibCFG value. */
	if (max17055_read(REG_HIBCFG, &hib_cfg))
		return EC_ERROR_UNKNOWN;

	/* Special sequence to exit hibernate mode. */
	if (max17055_write(0x60, 0x90) ||
	    max17055_write(REG_HIBCFG, 0) ||
	    max17055_write(0x60, 0))
		return EC_ERROR_UNKNOWN;

	/* Choose the model for charge voltage > 4.275V. */
	if (max17055_write(REG_DPACC, (BATTERY_MAX17055_DESIGNCAP / 32) *
			   51200 / BATTERY_MAX17055_DESIGNCAP))
		return EC_ERROR_UNKNOWN;
	if (max17055_write(REG_MODELCFG, (MODELCFG_REFRESH | MODELCFG_VCHG)))
		return EC_ERROR_UNKNOWN;

	/* Delay up to 500 ms until MODELCFG.REFRESH bit == 0. */
	while (--retries) {
		if (max17055_read(REG_MODELCFG, &reg))
			return EC_ERROR_UNKNOWN;
		if (!(MODELCFG_REFRESH & reg))
			break;
		msleep(10);
	}
	if (!retries)
		return EC_ERROR_TIMEOUT;

	/* Restore the original HibCFG value. */
	if (max17055_write(REG_HIBCFG, hib_cfg))
		return EC_ERROR_UNKNOWN;
	return EC_SUCCESS;
}

static void max17055_init(void)
{
	int reg;
	int retries = 80;

	if (!max17055_probe()) {
		CPRINTS("Wrong max17055 id!");
		return;
	}
	if (max17055_read(REG_STATUS, &reg)) {
		CPRINTS("%s: failed to read reg %02x", __func__, REG_STATUS);
		return;
	}

	/* Check for POR */
	if (STATUS_POR & reg) {
		/* Delay up to 800 ms until FSTAT.DNR bit == 0. */
		while (--retries) {
			if (max17055_read(REG_FSTAT, &reg)) {
				CPRINTS("%s: failed to read reg %02x",
					__func__, REG_FSTAT);
				return;
			}
			if (!(FSTAT_DNR & reg))
				break;
			msleep(10);
		}
		if (!retries) {
			CPRINTS("%s: timeout waiting for FSTAT.DNR cleared",
				__func__);
			return;
		}

		if (max17055_init_config()) {
			CPRINTS("max17055 configuration failed!");
			return;
		}
	}

	/* Clear POR bit */
	if (max17055_read(REG_STATUS, &reg)) {
		CPRINTS("%s: failed to read reg %02x", __func__, REG_STATUS);
		return;
	}
	if (max17055_write(REG_STATUS, (reg & ~STATUS_POR))) {
		CPRINTS("%s: failed to write reg %02x", __func__, REG_STATUS);
		return;
	}

	CPRINTS("max17055 configuration succeeded!");
}
DECLARE_HOOK(HOOK_INIT, max17055_init, HOOK_PRIO_DEFAULT);
