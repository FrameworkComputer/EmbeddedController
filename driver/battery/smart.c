/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Smart battery driver.
 */

#include "battery.h"
#include "battery_smart.h"
#include "console.h"
#include "host_command.h"
#include "i2c.h"
#include "smbus.h"
#include "timer.h"
#include "util.h"

/* Console output macros */
#define CPUTS(outstr) cputs(CC_CHARGER, outstr);
#define CPRINTS(format, args...) cprints(CC_CHARGER, format, ## args)

#define BATTERY_NO_RESPONSE_TIMEOUT	(1000*MSEC)

static int fake_state_of_charge = -1;

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
#ifdef CONFIG_BATTERY_CUT_OFF
	/*
	 * Some batteries would wake up after cut-off if we talk to it.
	 */
	if (battery_is_cut_off())
		return EC_RES_ACCESS_DENIED;
#endif
#ifdef CONFIG_SMBUS
	{
		int rv;
		uint16_t d16 = 0;
		rv = smbus_read_word(I2C_PORT_BATTERY, BATTERY_ADDR, cmd, &d16);
		*param = d16;
		return rv;
	}
#else
	return i2c_read16(I2C_PORT_BATTERY, BATTERY_ADDR, cmd, param);
#endif
}

test_mockable int sb_write(int cmd, int param)
{
#ifdef CONFIG_BATTERY_CUT_OFF
	/*
	 * Some batteries would wake up after cut-off if we talk to it.
	 */
	if (battery_is_cut_off())
		return EC_RES_ACCESS_DENIED;
#endif
#ifdef CONFIG_SMBUS
	return smbus_write_word(I2C_PORT_BATTERY, BATTERY_ADDR, cmd, param);
#else
	return i2c_write16(I2C_PORT_BATTERY, BATTERY_ADDR, cmd, param);
#endif
}

int sb_read_string(int port, int slave_addr, int offset, uint8_t *data,
	int len)
{
#ifdef CONFIG_BATTERY_CUT_OFF
	/*
	 * Some batteries would wake up after cut-off if we talk to it.
	 */
	if (battery_is_cut_off())
		return EC_RES_ACCESS_DENIED;
#endif
#ifdef CONFIG_SMBUS
	return smbus_read_string(port, slave_addr, offset, data, len);
#else
	return i2c_read_string(port, slave_addr, offset, data, len);
#endif
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
	return sb_read_string(I2C_PORT_BATTERY, BATTERY_ADDR,
			       SB_MANUFACTURER_NAME, dest, size);
}

/* Read device name */
test_mockable int battery_device_name(char *dest, int size)
{
	return sb_read_string(I2C_PORT_BATTERY, BATTERY_ADDR,
			       SB_DEVICE_NAME, dest, size);
}

/* Read battery type/chemistry */
test_mockable int battery_device_chemistry(char *dest, int size)
{
	return sb_read_string(I2C_PORT_BATTERY, BATTERY_ADDR,
			       SB_DEVICE_CHEMISTRY, dest, size);
}

void battery_get_params(struct batt_params *batt)
{
	struct batt_params batt_new = {0};
	int v;

	if (sb_read(SB_TEMPERATURE, &batt_new.temperature))
		batt_new.flags |= BATT_FLAG_BAD_TEMPERATURE;

	if (sb_read(SB_RELATIVE_STATE_OF_CHARGE, &batt_new.state_of_charge)
	    && fake_state_of_charge < 0)
		batt_new.flags |= BATT_FLAG_BAD_STATE_OF_CHARGE;

	/* If soc is faked, override with faked data */
	if (fake_state_of_charge >= 0)
		batt_new.state_of_charge = fake_state_of_charge;

	if (sb_read(SB_VOLTAGE, &batt_new.voltage))
		batt_new.flags |= BATT_FLAG_BAD_VOLTAGE;

	/* This is a signed 16-bit value. */
	if (sb_read(SB_CURRENT, &v))
		batt_new.flags |= BATT_FLAG_BAD_CURRENT;
	else
		batt_new.current = (int16_t)v;

	if (sb_read(SB_CHARGING_VOLTAGE, &batt_new.desired_voltage))
		batt_new.flags |= BATT_FLAG_BAD_DESIRED_VOLTAGE;

	if (sb_read(SB_CHARGING_CURRENT, &batt_new.desired_current))
		batt_new.flags |= BATT_FLAG_BAD_DESIRED_CURRENT;

	if (battery_remaining_capacity(&batt_new.remaining_capacity))
		batt_new.flags |= BATT_FLAG_BAD_REMAINING_CAPACITY;

	if (battery_full_charge_capacity(&batt_new.full_capacity))
		batt_new.flags |= BATT_FLAG_BAD_FULL_CAPACITY;

	if (battery_status(&batt_new.status))
		batt_new.flags |= BATT_FLAG_BAD_STATUS;

	/* If any of those reads worked, the battery is responsive */
	if ((batt_new.flags & BATT_FLAG_BAD_ANY) != BATT_FLAG_BAD_ANY)
		batt_new.flags |= BATT_FLAG_RESPONSIVE;

#if defined(CONFIG_BATTERY_PRESENT_CUSTOM) ||	\
	defined(CONFIG_BATTERY_PRESENT_GPIO)
	/* Hardware can tell us for certain */
	batt_new.is_present = battery_is_present();
#else
	/* No hardware test, so we only know it's there if it responds */
	if (batt_new.flags & BATT_FLAG_RESPONSIVE)
		batt_new.is_present = BP_YES;
	else
		batt_new.is_present = BP_NOT_SURE;
#endif

	/*
	 * Charging allowed if both desired voltage and current are nonzero
	 * and battery isn't full (and we read them all correctly).
	 */
	if (!(batt_new.flags & (BATT_FLAG_BAD_DESIRED_VOLTAGE |
				BATT_FLAG_BAD_DESIRED_CURRENT |
				BATT_FLAG_BAD_STATE_OF_CHARGE)) &&
#ifdef CONFIG_BATTERY_REQUESTS_NIL_WHEN_DEAD
		/*
		 * TODO (crosbug.com/p/29467): remove this workaround
		 * for dead battery that requests no voltage/current
		 */
		((batt_new.desired_voltage &&
			batt_new.desired_current &&
			batt_new.state_of_charge < BATTERY_LEVEL_FULL) ||
		(batt_new.desired_voltage == 0 &&
			batt_new.desired_current == 0 &&
			batt_new.state_of_charge == 0)))
#else
	    batt_new.desired_voltage &&
	    batt_new.desired_current &&
	    batt_new.state_of_charge < BATTERY_LEVEL_FULL)
#endif
		batt_new.flags |= BATT_FLAG_WANT_CHARGE;
	else
		/* Force both to zero */
		batt_new.desired_voltage = batt_new.desired_current = 0;

	/* Update visible battery parameters */
	memcpy(batt, &batt_new, sizeof(*batt));
}

/* Wait until battery is totally stable */
int battery_wait_for_stable(void)
{
	int status;
	uint64_t wait_timeout = get_time().val + BATTERY_NO_RESPONSE_TIMEOUT;

	CPRINTS("Wait for battery stabilized during %d",
			 BATTERY_NO_RESPONSE_TIMEOUT);
	while (get_time().val < wait_timeout) {
		/* Starting pinging battery */
		if (battery_status(&status) == EC_SUCCESS) {
			/* Battery is stable */
			CPRINTS("battery responded with status %x", status);
			return EC_SUCCESS;
		}
		msleep(25); /* clock stretching could hold 25ms */
	}
	CPRINTS("battery not responding");
	return EC_ERROR_NOT_POWERED;
}

#if !defined(CONFIG_CHARGER_V1) && defined(CONFIG_CMD_BATTFAKE)
static int command_battfake(int argc, char **argv)
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
		ccprintf("Fake batt %d%%\n",
			 fake_state_of_charge);

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(battfake, command_battfake,
			"percent (-1 = use real level)",
			"Set fake battery level",
			NULL);
#endif

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
	rv = sb_read(p->reg, &val);
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
	rv = sb_write(p->reg, p->value);
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
	rv = sb_read_string(I2C_PORT_BATTERY, BATTERY_ADDR, p->reg,
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
