/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Smart battery driver.
 */

#include "console.h"
#include "smart_battery.h"
#include "timer.h"
#include "util.h"

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

/* Calculate battery time in minutes, under a charging rate
 * rate >  0: charging, negative time to full
 * rate <  0: discharging, positive time to empty
 * rate == 0: invalid input, time = 0
 */
int battery_time_at_rate(int rate, int *minutes)
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
			usleep(10000);
		}
	}
	return EC_ERROR_TIMEOUT;
}

/* Read manufacturer date */
int battery_manufacturer_date(int *year, int *month, int *day)
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

/*****************************************************************************/
/* Console commands */

static int print_battery_info(void)
{
	int value;
	int hour, minute;
	char text[32];
	const char *unit;
	int rv;

	rv = battery_temperature(&value);
	if (rv)
		return rv;

	ccprintf("  Temp:      0x%04x = %.1d K (%.1d C)\n",
		 value, value, value - 2731);

	ccprintf("  Manuf:     %s\n",
		 battery_manufacturer_name(text, sizeof(text)) == EC_SUCCESS ?
		 text : "(error)");

	ccprintf("  Device:    %s\n",
		 battery_device_name(text, sizeof(text)) == EC_SUCCESS ?
		 text : "(error)");

	ccprintf("  Chem:      %s\n",
		 battery_device_chemistry(text, sizeof(text)) == EC_SUCCESS ?
		 text : "(error)");

	battery_serial_number(&value);
	ccprintf("  Serial:    0x%04x\n", value);

	battery_voltage(&value);
	ccprintf("  V:         0x%04x = %d mV\n", value, value);

	battery_desired_voltage(&value);
	ccprintf("  V-desired: 0x%04x = %d mV\n", value, value);

	battery_design_voltage(&value);
	ccprintf("  V-design:  0x%04x = %d mV\n", value, value);

	battery_current(&value);
	ccprintf("  I:         0x%04x = %d mA",
		value & 0xffff, value);
	if (value > 0)
		ccputs("(CHG)");
	else if (value < 0)
		ccputs("(DISCHG)");
	ccputs("\n");


	battery_desired_current(&value);
	ccprintf("  I-desired: 0x%04x = %d mA\n", value, value);

	battery_get_battery_mode(&value);
	ccprintf("  Mode:      0x%04x\n", value);
	unit = (value & MODE_CAPACITY) ? "0 mW" : " mAh";

	battery_state_of_charge(&value);
	ccprintf("  Charge:    %d %%\n", value);

	battery_state_of_charge_abs(&value);
	ccprintf("    Abs:     %d %%\n", value);

	battery_remaining_capacity(&value);
	ccprintf("  Remaining: %d%s\n", value, unit);

	battery_full_charge_capacity(&value);
	ccprintf("  Cap-full:  %d%s\n", value, unit);

	battery_design_capacity(&value);
	ccprintf("    Design:  %d%s\n", value, unit);

	battery_time_to_full(&value);
	if (value == 65535) {
		hour   = 0;
		minute = 0;
	} else {
		hour   = value / 60;
		minute = value % 60;
	}
	ccprintf("  Time-full: %dh:%d\n", hour, minute);

	battery_time_to_empty(&value);
	if (value == 65535) {
		hour   = 0;
		minute = 0;
	} else {
		hour   = value / 60;
		minute = value % 60;
	}
	ccprintf("    Empty:   %dh:%d\n", hour, minute);

	return 0;
}

static int command_battery(int argc, char **argv)
{
	int repeat = 1;
	int rv = 0;
	int loop;
	char *e;

	if (argc > 1) {
		repeat = strtoi(argv[1], &e, 0);
		if (*e) {
			ccputs("Invalid repeat count\n");
			return EC_ERROR_INVAL;
		}
	}

	for (loop = 0; loop < repeat; loop++)
		rv = print_battery_info();

	if (rv)
		ccprintf("Failed - error %d\n", rv);

	return rv ? EC_ERROR_UNKNOWN : EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(battery, command_battery,
			"<repeat_count>",
			"Print battery info",
			NULL);


/* Usage:sb <r/w> cmd [uint16_t w_word]
 *     sb r 0x14 // desired charging current
 *     sb r 0x15 // desired charging voltage
 *     sb r 0x3  // battery mode
 *     sb w 0x3 0xe001 // set battery mode
 */
static int command_sb(int argc, char **argv)
{
	int rv;
	int cmd, d;
	char *e;

	if (argc < 3)
		return EC_ERROR_PARAM_COUNT;

	cmd = strtoi(argv[2], &e, 0);
	if (*e)
		return EC_ERROR_PARAM2;

	if (argv[1][0] == 'r') {
		rv = i2c_read16(I2C_PORT_BATTERY, BATTERY_ADDR, cmd, &d);
		if (rv)
			return rv;

		ccprintf("R SBCMD[%04x] 0x%04x (%d)\n", cmd, d, d);
		return EC_SUCCESS;
	} else if (argc >= 4 && argv[1][0] == 'w') {
		d = strtoi(argv[3], &e, 0);
		if (*e)
			return EC_ERROR_PARAM3;

		ccprintf("W SBCMD[%04x] 0x%04x (%d)\n", cmd, d, d);
		rv = i2c_write16(I2C_PORT_BATTERY, BATTERY_ADDR, cmd, d);
		if (rv)
			return rv;
		return EC_SUCCESS;
	}

	return EC_ERROR_INVAL;


}
DECLARE_CONSOLE_COMMAND(sb, command_sb,
			"[r addr | w addr value]",
			"Read/write smart battery data",
			NULL);

