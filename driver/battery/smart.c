/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Smart battery driver.
 */

#include "battery.h"
#include "battery_smart.h"
#include "host_command.h"
#include "i2c.h"
#include "timer.h"
#include "util.h"

test_mockable int sbc_read(int cmd, int *param)
{
	return i2c_read16(I2C_PORT_CHARGER, CHARGER_ADDR, cmd, param);
}

test_mockable int sbc_write(int cmd, int param)
{
	return i2c_write16(I2C_PORT_CHARGER, CHARGER_ADDR, cmd, param);
}

test_mockable int sb_read(int cmd, int *param)
{
	return i2c_read16(I2C_PORT_BATTERY, BATTERY_ADDR, cmd, param);
}

test_mockable int sb_write(int cmd, int param)
{
	return i2c_write16(I2C_PORT_BATTERY, BATTERY_ADDR, cmd, param);
}

int battery_get_mode(int *mode)
{
	return sb_read(SB_BATTERY_MODE, mode);
}

/**
 * Force battery to mAh mode (instead of 10mW mode) for reporting capacity.
 *
 * @return non-zero if error.
 */

static int battery_force_mah_mode(void)
{
	int val, rv;
	rv = battery_get_mode(&val);
	if (rv)
		return rv;

	if (val & MODE_CAPACITY)
		rv = sb_write(SB_BATTERY_MODE, val & ~MODE_CAPACITY);

	return rv;
}

int battery_state_of_charge_abs(int *percent)
{
	return sb_read(SB_ABSOLUTE_STATE_OF_CHARGE, percent);
}

int battery_remaining_capacity(int *capacity)
{
	int rv = battery_force_mah_mode();
	if (rv)
		return rv;

	return sb_read(SB_REMAINING_CAPACITY, capacity);
}

int battery_full_charge_capacity(int *capacity)
{
	int rv = battery_force_mah_mode();
	if (rv)
		return rv;

	return sb_read(SB_FULL_CHARGE_CAPACITY, capacity);
}

int battery_time_to_empty(int *minutes)
{
	return sb_read(SB_AVERAGE_TIME_TO_EMPTY, minutes);
}

int battery_run_time_to_empty(int *minutes)
{
	return sb_read(SB_RUN_TIME_TO_EMPTY, minutes);
}

int battery_time_to_full(int *minutes)
{
	return sb_read(SB_AVERAGE_TIME_TO_FULL, minutes);
}

/* Read battery status */
int battery_status(int *status)
{
	return sb_read(SB_BATTERY_STATUS, status);
}

/* Battery charge cycle count */
int battery_cycle_count(int *count)
{
	return sb_read(SB_CYCLE_COUNT, count);
}

int battery_design_capacity(int *capacity)
{
	int rv = battery_force_mah_mode();
	if (rv)
		return rv;

	return sb_read(SB_DESIGN_CAPACITY, capacity);
}

/* Designed battery output voltage
 * unit: mV
 */
int battery_design_voltage(int *voltage)
{
	return sb_read(SB_DESIGN_VOLTAGE, voltage);
}

/* Read serial number */
int battery_serial_number(int *serial)
{
	return sb_read(SB_SERIAL_NUMBER, serial);
}

test_mockable int battery_time_at_rate(int rate, int *minutes)
{
	int rv;
	int ok, time;
	int loop, cmd, output_sign;

	if (rate == 0) {
		*minutes = 0;
		return EC_ERROR_INVAL;
	}

	rv = sb_write(SB_AT_RATE, rate);
	if (rv)
		return rv;
	loop = 5;
	while (loop--) {
		rv = sb_read(SB_AT_RATE_OK, &ok);
		if (rv)
			return rv;
		if (ok) {
			if (rate > 0) {
				cmd = SB_AT_RATE_TIME_TO_FULL;
				output_sign = -1;
			} else {
				cmd = SB_AT_RATE_TIME_TO_EMPTY;
				output_sign = 1;
			}
			rv = sb_read(cmd, &time);
			if (rv)
				return rv;

			*minutes = (time == 0xffff) ? 0 : output_sign * time;
			return EC_SUCCESS;
		} else {
			/* wait 10ms for AT_RATE_OK */
			msleep(10);
		}
	}
	return EC_ERROR_TIMEOUT;
}

test_mockable int battery_manufacture_date(int *year, int *month, int *day)
{
	int rv;
	int ymd;

	rv = sb_read(SB_SPECIFICATION_INFO, &ymd);
	if (rv)
		return rv;

	/* battery date format:
	 * ymd = day + month * 32 + (year - 1980) * 256
	 */
	*year  = (ymd >> 8) + 1980;
	*month = (ymd & 0xff) / 32;
	*day   = (ymd & 0xff) % 32;

	return EC_SUCCESS;
}

/* Read manufacturer name */
test_mockable int battery_manufacturer_name(char *dest, int size)
{
	return i2c_read_string(I2C_PORT_BATTERY, BATTERY_ADDR,
			       SB_MANUFACTURER_NAME, dest, size);
}

/* Read device name */
test_mockable int battery_device_name(char *dest, int size)
{
	return i2c_read_string(I2C_PORT_BATTERY, BATTERY_ADDR,
			       SB_DEVICE_NAME, dest, size);
}

/* Read battery type/chemistry */
test_mockable int battery_device_chemistry(char *dest, int size)
{
	return i2c_read_string(I2C_PORT_BATTERY, BATTERY_ADDR,
			       SB_DEVICE_CHEMISTRY, dest, size);
}

void battery_get_params(struct batt_params *batt)
{
	int v;

	/* Reset battery parameters */
	memset(batt, 0, sizeof(*batt));

	if (sb_read(SB_TEMPERATURE, &batt->temperature))
		batt->flags |= BATT_FLAG_BAD_TEMPERATURE;

	if (sb_read(SB_RELATIVE_STATE_OF_CHARGE, &batt->state_of_charge))
		batt->flags |= BATT_FLAG_BAD_STATE_OF_CHARGE;

	if (sb_read(SB_VOLTAGE, &batt->voltage))
		batt->flags |= BATT_FLAG_BAD_VOLTAGE;

	/* This is a signed 16-bit value. */
	if (sb_read(SB_CURRENT, &v))
		batt->flags |= BATT_FLAG_BAD_CURRENT;
	else
		batt->current = (int16_t)v;

	if (sb_read(SB_CHARGING_VOLTAGE, &batt->desired_voltage))
		batt->flags |= BATT_FLAG_BAD_DESIRED_VOLTAGE;

	if (sb_read(SB_CHARGING_CURRENT, &batt->desired_current))
		batt->flags |= BATT_FLAG_BAD_DESIRED_CURRENT;

	if (battery_remaining_capacity(&batt->remaining_capacity))
		batt->flags |= BATT_FLAG_BAD_REMAINING_CAPACITY;

	if (battery_full_charge_capacity(&batt->full_capacity))
		batt->flags |= BATT_FLAG_BAD_FULL_CAPACITY;

	/* If any of those reads worked, the battery is responsive */
	if ((batt->flags & BATT_FLAG_BAD_ANY) != BATT_FLAG_BAD_ANY)
		batt->flags |= BATT_FLAG_RESPONSIVE;

#if defined(CONFIG_BATTERY_PRESENT_CUSTOM) ||	\
	defined(CONFIG_BATTERY_PRESENT_GPIO)
	/* Hardware can tell us for certain */
	batt->is_present = battery_is_present();
#else
	/* No hardware test, so we only know it's there if it responds */
	if (batt->flags & BATT_FLAG_RESPONSIVE)
		batt->is_present = BP_YES;
	else
		batt->is_present = BP_NOT_SURE;
#endif

	/*
	 * Charging allowed if both desired voltage and current are nonzero
	 * and battery isn't full (and we read them all correctly).
	 */
	if (!(batt->flags & (BATT_FLAG_BAD_DESIRED_VOLTAGE |
			     BATT_FLAG_BAD_DESIRED_CURRENT |
			     BATT_FLAG_BAD_STATE_OF_CHARGE)) &&
	    batt->desired_voltage &&
	    batt->desired_current &&
	    batt->state_of_charge < BATTERY_LEVEL_FULL)
		batt->flags |= BATT_FLAG_WANT_CHARGE;
	else
		/* Force both to zero */
		batt->desired_voltage = batt->desired_current = 0;
}

/*****************************************************************************/
/* Smart battery pass-through
 */
#ifdef CONFIG_I2C_PASSTHROUGH
static int host_command_sb_read_word(struct host_cmd_handler_args *args)
{
	int rv;
	int val;
	const struct ec_params_sb_rd *p = args->params;
	struct ec_response_sb_rd_word *r = args->response;

	if (p->reg > 0x1c)
		return EC_RES_INVALID_PARAM;
	rv = i2c_read16(I2C_PORT_BATTERY, BATTERY_ADDR, p->reg, &val);
	if (rv)
		return EC_RES_ERROR;

	r->value = val;
	args->response_size = sizeof(struct ec_response_sb_rd_word);

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_SB_READ_WORD,
		     host_command_sb_read_word,
		     EC_VER_MASK(0));

static int host_command_sb_write_word(struct host_cmd_handler_args *args)
{
	int rv;
	const struct ec_params_sb_wr_word *p = args->params;

	if (p->reg > 0x1c)
		return EC_RES_INVALID_PARAM;
	rv = i2c_write16(I2C_PORT_BATTERY, BATTERY_ADDR, p->reg, p->value);
	if (rv)
		return EC_RES_ERROR;

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_SB_WRITE_WORD,
		     host_command_sb_write_word,
		     EC_VER_MASK(0));

static int host_command_sb_read_block(struct host_cmd_handler_args *args)
{
	int rv;
	const struct ec_params_sb_rd *p = args->params;
	struct ec_response_sb_rd_block *r = args->response;

	if ((p->reg != SB_MANUFACTURER_NAME) &&
	    (p->reg != SB_DEVICE_NAME) &&
	    (p->reg != SB_DEVICE_CHEMISTRY) &&
	    (p->reg != SB_MANUFACTURER_DATA))
		return EC_RES_INVALID_PARAM;
	rv = i2c_read_string(I2C_PORT_BATTERY, BATTERY_ADDR, p->reg,
			     r->data, 32);
	if (rv)
		return EC_RES_ERROR;

	args->response_size = sizeof(struct ec_response_sb_rd_block);

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_SB_READ_BLOCK,
		     host_command_sb_read_block,
		     EC_VER_MASK(0));

static int host_command_sb_write_block(struct host_cmd_handler_args *args)
{
	/* Not implemented */
	return EC_RES_INVALID_COMMAND;
}
DECLARE_HOST_COMMAND(EC_CMD_SB_WRITE_BLOCK,
		     host_command_sb_write_block,
		     EC_VER_MASK(0));
#endif
