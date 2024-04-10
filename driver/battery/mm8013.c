/* Copyright 2019 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Battery driver for MM8013.
 */

#include "battery.h"
#include "battery_smart.h"
#include "console.h"
#include "i2c.h"
#include "mm8013.h"
#include "timer.h"
#include "util.h"

#define BATTERY_PACK_INFO_LENGTH 8

/* MM8013 requires a 100us wait time after a read operation. */
#define I2C_WAIT_TIME 100

static int mm8013_read16(int offset, int *data)
{
	int rv;

	*data = 0;
	rv = i2c_read16(I2C_PORT_BATTERY, MM8013_ADDR_FLAGS, offset, data);
	crec_usleep(I2C_WAIT_TIME);
	if (rv)
		return rv;
	return EC_SUCCESS;
}

static int mm8013_read_block(int offset, uint8_t *data, int len)
{
	int rv;

	rv = i2c_read_block(I2C_PORT_BATTERY, MM8013_ADDR_FLAGS, offset, data,
			    len);
	crec_usleep(I2C_WAIT_TIME);
	if (rv)
		return rv;
	return EC_SUCCESS;
}

static int battery_flag(int *flag)
{
	return mm8013_read16(REG_FLAGS, flag);
}

static int battery_current(int *current)
{
	int16_t tmp;
	int rv;

	rv = mm8013_read_block(REG_AVERAGE_CURRENT, (uint8_t *)&tmp,
			       sizeof(int16_t));
	if (rv)
		return rv;
	*current = tmp;

	return EC_SUCCESS;
}

int battery_device_name(char *device_name, int buf_size)
{
	int rv;
	char out_buf[BATTERY_PACK_INFO_LENGTH + 1];

	rv = mm8013_read_block(REG_PRODUCT_INFORMATION, (uint8_t *)out_buf,
			       BATTERY_PACK_INFO_LENGTH);
	if (rv)
		return rv;

	out_buf[BATTERY_PACK_INFO_LENGTH] = '\0';
	strzcpy(device_name, out_buf, buf_size);

	return EC_SUCCESS;
}

int battery_state_of_charge_abs(int *percent)
{
	return mm8013_read16(REG_STATE_OF_CHARGE, percent);
}

int battery_remaining_capacity(int *capacity)
{
	return mm8013_read16(REG_REMAINING_CAPACITY, capacity);
}

int battery_full_charge_capacity(int *capacity)
{
	return mm8013_read16(REG_FULL_CHARGE_CAPACITY, capacity);
}

int battery_time_to_empty(int *minutes)
{
	return mm8013_read16(REG_AVERAGE_TIME_TO_EMPTY, minutes);
}

int battery_time_to_full(int *minutes)
{
	return mm8013_read16(REG_AVERAGE_TIME_TO_FULL, minutes);
}

int battery_cycle_count(int *count)
{
	return mm8013_read16(REG_CYCLE_COUNT, count);
}

int battery_design_capacity(int *capacity)
{
	return mm8013_read16(REG_DESIGN_CAPACITY, capacity);
}

int battery_time_at_rate(int rate, int *minutes)
{
	return EC_ERROR_UNIMPLEMENTED;
}

int battery_device_chemistry(char *dest, int size)
{
	strzcpy(dest, "<unkn>", size);

	return EC_SUCCESS;
}

int battery_serial_number(int *serial)
{
	*serial = 0xFFFFFFFF;
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

int battery_get_mode(int *mode)
{
	return EC_ERROR_UNIMPLEMENTED;
}

int battery_status(int *status)
{
	int rv;
	int flag = 0;

	*status = 0;

	rv = battery_flag(&flag);
	if (rv)
		return rv;

	if (flag & (MM8013_FLAG_OTC | MM8013_FLAG_OTD))
		*status |= STATUS_OVERTEMP_ALARM;
	if (flag & MM8013_FLAG_FC)
		*status |= STATUS_FULLY_CHARGED;
	if (flag & MM8013_FLAG_DSG)
		*status |= STATUS_DISCHARGING;
	if (flag & MM8013_FLAG_BATHI)
		*status |= STATUS_OVERCHARGED_ALARM;

	return EC_SUCCESS;
}

enum battery_present battery_is_present(void)
{
	int temp;

	if (mm8013_read16(REG_TEMPERATURE, &temp))
		return BP_NO;
	return BP_YES;
}

void battery_get_params(struct batt_params *batt)
{
	struct batt_params batt_new = { 0 };
	int flag = 0;

	/*
	 * Assuming the battery is responsive as long as
	 * mm8013 finds battery is present.
	 */
	batt_new.is_present = battery_is_present();

	if (batt_new.is_present == BP_YES)
		batt_new.flags |= BATT_FLAG_RESPONSIVE;
	else if (batt_new.is_present == BP_NO)
		/* Battery is not present, gauge won't report useful info. */
		goto batt_out;

	if (mm8013_read16(REG_TEMPERATURE, &batt_new.temperature))
		batt_new.flags |= BATT_FLAG_BAD_TEMPERATURE;

	if (mm8013_read16(REG_STATE_OF_CHARGE, &batt_new.state_of_charge))
		batt_new.flags |= BATT_FLAG_BAD_STATE_OF_CHARGE;

	if (mm8013_read16(REG_VOLTAGE, &batt_new.voltage))
		batt_new.flags |= BATT_FLAG_BAD_VOLTAGE;

	if (battery_current(&batt_new.current))
		batt_new.flags |= BATT_FLAG_BAD_CURRENT;

	batt_new.desired_voltage = battery_get_info()->voltage_max;
	batt_new.desired_current = BATTERY_DESIRED_CHARGING_CURRENT;

	if (battery_remaining_capacity(&batt_new.remaining_capacity))
		batt_new.flags |= BATT_FLAG_BAD_REMAINING_CAPACITY;

	if (battery_full_charge_capacity(&batt_new.full_capacity))
		batt_new.flags |= BATT_FLAG_BAD_FULL_CAPACITY;

	if (battery_status(&batt_new.status))
		batt_new.flags |= BATT_FLAG_BAD_STATUS;

	if (!battery_flag(&flag) && (flag & MM8013_FLAG_CHG))
		batt_new.flags |= BATT_FLAG_WANT_CHARGE;

batt_out:
	/* Update visible battery parameters */
	memcpy(batt, &batt_new, sizeof(*batt));
}

#ifdef CONFIG_CMD_PWR_AVG
int battery_get_avg_current(void)
{
	/* TODO(crbug.com/752320) implement this */
	return -EC_ERROR_UNIMPLEMENTED;
}

int battery_get_avg_voltage(void)
{
	/* TODO(crbug.com/752320) implement this */
	return -EC_ERROR_UNIMPLEMENTED;
}
#endif /* CONFIG_CMD_PWR_AVG */

/* Wait until battery is totally stable. */
int battery_wait_for_stable(void)
{
	/* TODO(phoenixshen): Implement this function. */
	return EC_SUCCESS;
}
