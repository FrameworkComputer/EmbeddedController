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

static int command_battery(int argc, char **argv)
{
	int rv;
	int d;
	int hour, minute;
	char text[32];
	const char *unit;

	ccputs("Reading battery...\n");

	rv = battery_temperature(&d);
	if (rv)
		return rv;
	ccprintf("  Temperature:            0x%04x = %d x 0.1K (%d C)\n",
		 d, d, (d-2731)/10);

	ccprintf("  Manufacturer:           %s\n",
		 battery_manufacturer_name(text, sizeof(text)) == EC_SUCCESS ?
		 text : "(error)");

	ccprintf("  Device:                 %s\n",
		 battery_device_name(text, sizeof(text)) == EC_SUCCESS ?
		 text : "(error)");

	ccprintf("  Chemistry:              %s\n",
		 battery_device_chemistry(text, sizeof(text)) == EC_SUCCESS ?
		 text : "(error)");

	battery_serial_number(&d);
	ccprintf("  Serial number:          0x%04x\n", d);

	battery_voltage(&d);
	ccprintf("  Voltage:                0x%04x = %d mV\n", d, d);

	battery_desired_voltage(&d);
	ccprintf("  Desired voltage         0x%04x = %d mV\n", d, d);

	battery_design_voltage(&d);
	ccprintf("  Design output voltage   0x%04x = %d mV\n", d, d);

	battery_current(&d);
	ccprintf("  Current:                0x%04x = %d mA",
		d & 0xffff, d);
	if (d > 0)
		ccputs("(CHG)");
	else if (d < 0)
		ccputs("(DISCHG)");
	ccputs("\n");


	battery_desired_current(&d);
	ccprintf("  Desired current         0x%04x = %d mA\n", d, d);

	battery_get_battery_mode(&d);
	ccprintf("  Battery mode:           0x%04x\n", d);
	unit = (d & MODE_CAPACITY) ? "0 mW" : " mAh";

	battery_state_of_charge(&d);
	ccprintf("  %% of charge:            %d %%\n", d);

	battery_state_of_charge_abs(&d);
	ccprintf("  Abs %% of charge:        %d %%\n", d);

	battery_remaining_capacity(&d);
	ccprintf("  Remaining capacity:     %d%s\n", d, unit);

	battery_full_charge_capacity(&d);
	ccprintf("  Full charge capacity:   %d%s\n", d, unit);

	battery_design_capacity(&d);
	ccprintf("  Design capacity:        %d%s\n", d, unit);

	battery_time_to_empty(&d);
	if (d == 65535) {
		hour   = 0;
		minute = 0;
	} else {
		hour   = d / 60;
		minute = d % 60;
	}
	ccprintf("  Time to empty:          %dh:%d\n", hour, minute);

	battery_time_to_full(&d);
	if (d == 65535) {
		hour   = 0;
		minute = 0;
	} else {
		hour   = d / 60;
		minute = d % 60;
	}
	ccprintf("  Time to full:           %dh:%d\n", hour, minute);

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(battery, command_battery);


static int command_sb(int argc, char **argv)
{
	int rv;
	int cmd, d;
	char *e;

	if (argc < 3)
		goto usage;

	cmd = strtoi(argv[2], &e, 0);
	if (*e) {
		ccputs("Invalid cmd.\n");
		goto usage;
	}

	if (argv[1][0] == 'r') {
		rv = i2c_read16(I2C_PORT_BATTERY, BATTERY_ADDR, cmd, &d);
		if (rv) {
			ccputs("I2C read failed.\n");
			return rv;
		}
		ccprintf("R SBCMD[%04x] 0x%04x (%d)\n", cmd, d, d);
		return EC_SUCCESS;
	} else if (argc >= 4 && argv[1][0] == 'w') {
		d = strtoi(argv[3], &e, 0);
		if (*e) {
			ccputs("Invalid w_word.\n");
			goto usage;
		}
		ccprintf("W SBCMD[%04x] 0x%04x (%d)\n", cmd, d, d);
		rv = i2c_write16(I2C_PORT_BATTERY, BATTERY_ADDR, cmd, d);
		if (rv) {
			ccputs("I2C write failed.\n");
			return rv;
		}
		return EC_SUCCESS;
	}

usage:
	ccputs("Usage:sb <r/w> cmd [uint16_t w_word]\n");
	ccputs("    sb r 0x14 // desired charging current\n");
	ccputs("    sb r 0x15 // desired charging voltage\n");
	ccputs("    sb r 0x3  // battery mode\n");
	ccputs("    sb w 0x3 0xe001 // set battery mode\n");
	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(sb, command_sb);

