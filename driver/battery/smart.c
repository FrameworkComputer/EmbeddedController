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

test_mockable int sbc_read(int cmd, int *param)
{
	return i2c_read16(I2C_PORT_CHARGER, CHARGER_ADDR, cmd, param);
}

test_mockable int sbc_write(int cmd, int param)
{
	return i2c_write16(I2C_PORT_CHARGER, CHARGER_ADDR, cmd, param);
}

int sb_read(int cmd, int *param)
{
	return i2c_read16(I2C_PORT_BATTERY, BATTERY_ADDR, cmd, param);
}

int sb_write(int cmd, int param)
{
	return i2c_write16(I2C_PORT_BATTERY, BATTERY_ADDR, cmd, param);
}

int battery_get_mode(int *mode)
{
	return sb_read(SB_BATTERY_MODE, mode);
}

int battery_set_mode(int mode)
{
	return sb_write(SB_BATTERY_MODE, mode);
}

int battery_is_in_10mw_mode(int *ret)
{
	int val;
	int rv = battery_get_mode(&val);
	if (rv)
		return rv;
	*ret = val & MODE_CAPACITY;
	return EC_SUCCESS;
}

int battery_set_10mw_mode(int enabled)
{
	int val, rv;
	rv = battery_get_mode(&val);
	if (rv)
		return rv;
	if (enabled)
		val |= MODE_CAPACITY;
	else
		val &= ~MODE_CAPACITY;
	return battery_set_mode(val);
}

int battery_temperature(int *deci_kelvin)
{
	return sb_read(SB_TEMPERATURE, deci_kelvin);
}

int battery_voltage(int *voltage)
{
	return sb_read(SB_VOLTAGE, voltage);
}

int battery_state_of_charge(int *percent)
{
	return sb_read(SB_RELATIVE_STATE_OF_CHARGE, percent);
}

int battery_state_of_charge_abs(int *percent)
{
	return sb_read(SB_ABSOLUTE_STATE_OF_CHARGE, percent);
}

int battery_remaining_capacity(int *capacity)
{
	return sb_read(SB_REMAINING_CAPACITY, capacity);
}

int battery_full_charge_capacity(int *capacity)
{
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

int battery_desired_current(int *current)
{
	return sb_read(SB_CHARGING_CURRENT, current);
}

int battery_desired_voltage(int *voltage)
{
	return sb_read(SB_CHARGING_VOLTAGE, voltage);
}

int battery_charging_allowed(int *allowed)
{
	int v, c, rv;

	/*
	 * TODO(crosbug.com/p/23811): This re-reads the battery current and
	 * voltage, which is silly because charge_state.c just read them.
	 */
	rv = battery_desired_voltage(&v) | battery_desired_current(&c);
	if (rv)
		return rv;
	*allowed = (v != 0) && (c != 0);
	return EC_SUCCESS;
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

/* Designed battery capacity
 * unit: mAh or 10mW depends on battery mode
 */
int battery_design_capacity(int *capacity)
{
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

/* Read battery discharging current
 * unit: mA
 * negative value: charging
 */
int battery_current(int *current)
{
	int rv, d;

	rv = sb_read(SB_CURRENT, &d);
	if (rv)
		return rv;

	*current = (int16_t)d;
	return EC_SUCCESS;
}


int battery_average_current(int *current)
{
	int rv, d;

	rv = sb_read(SB_AVERAGE_CURRENT, &d);
	if (rv)
		return rv;

	*current = (int16_t)d;
	return EC_SUCCESS;
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
