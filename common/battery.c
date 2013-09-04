/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Common battery command.
 */

#include "battery.h"
#include "console.h"
#include "timer.h"
#include "util.h"

static const char *get_error_text(int rv)
{
	if (rv == EC_ERROR_UNIMPLEMENTED)
		return "(unsupported)";
	else
		return "(error)";
}

static void print_item_name(const char *name)
{
	ccprintf("  %-11s", name);
}

static int check_print_error(int rv)
{
	if (rv != EC_SUCCESS)
		ccprintf("%s\n", get_error_text(rv));
	return rv == EC_SUCCESS;
}

static int print_battery_info(void)
{
	int value;
	int hour, minute;
	char text[32];
	const char *unit;

	print_item_name("Temp:");
	if (check_print_error(battery_temperature(&value)))
		ccprintf("0x%04x = %.1d K (%.1d C)\n",
			 value, value, value - 2731);

	print_item_name("Manuf:");
	if (check_print_error(battery_manufacturer_name(text, sizeof(text))))
		ccprintf("%s\n", text);

	print_item_name("Device:");
	if (check_print_error(battery_device_name(text, sizeof(text))))
		ccprintf("%s\n", text);

	print_item_name("Chem:");
	if (check_print_error(battery_device_chemistry(text, sizeof(text))))
		ccprintf("%s\n", text);

	print_item_name("Serial:");
	if (check_print_error(battery_serial_number(&value)))
		ccprintf("0x%04x\n", value);

	print_item_name("V:");
	if (check_print_error(battery_voltage(&value)))
		ccprintf("0x%04x = %d mV\n", value, value);

	print_item_name("V-desired:");
	if (check_print_error(battery_desired_voltage(&value)))
		ccprintf("0x%04x = %d mV\n", value, value);

	print_item_name("V-deisgn:");
	if (check_print_error(battery_design_voltage(&value)))
		ccprintf("0x%04x = %d mV\n", value, value);

	print_item_name("I:");
	if (check_print_error(battery_current(&value))) {
		ccprintf("0x%04x = %d mA", value & 0xffff, value);
		if (value > 0)
			ccputs("(CHG)");
		else if (value < 0)
			ccputs("(DISCHG)");
		ccputs("\n");
	}

	print_item_name("I-desired:");
	if (check_print_error(battery_desired_current(&value)))
		ccprintf("0x%04x = %d mA\n", value, value);

	print_item_name("Mode:");
	if (check_print_error(battery_get_battery_mode(&value)))
		ccprintf("0x%04x\n", value);

	battery_is_in_10mw_mode(&value);
	unit = value ? "0 mW" : " mAh";

	print_item_name("Charge:");
	if (check_print_error(battery_state_of_charge(&value)))
		ccprintf("%d %%\n", value);

	print_item_name("Abs:");
	if (check_print_error(battery_state_of_charge_abs(&value)))
		ccprintf("%d %%\n", value);

	print_item_name("Remaining:");
	if (check_print_error(battery_remaining_capacity(&value)))
		ccprintf("%d%s\n", value, unit);

	print_item_name("Cap-full:");
	if (check_print_error(battery_full_charge_capacity(&value)))
		ccprintf("%d%s\n", value, unit);

	print_item_name("  Design:");
	if (check_print_error(battery_design_capacity(&value)))
		ccprintf("%d%s\n", value, unit);

	print_item_name("Time-full:");
	if (check_print_error(battery_time_to_full(&value))) {
		if (value == 65535) {
			hour   = 0;
			minute = 0;
		} else {
			hour   = value / 60;
			minute = value % 60;
		}
		ccprintf("%dh:%d\n", hour, minute);
	}

	print_item_name("  Empty:");
	if (check_print_error(battery_time_to_empty(&value))) {
		if (value == 65535) {
			hour   = 0;
			minute = 0;
		} else {
			hour   = value / 60;
			minute = value % 60;
		}
		ccprintf("%dh:%d\n", hour, minute);
	}

	return 0;
}

static int command_battery(int argc, char **argv)
{
	int repeat = 1;
	int rv = 0;
	int loop;
	int sleep_ms = 0;
	char *e;

	if (argc > 1) {
		repeat = strtoi(argv[1], &e, 0);
		if (*e) {
			ccputs("Invalid repeat count\n");
			return EC_ERROR_INVAL;
		}
	}

	if (argc > 2) {
		sleep_ms = strtoi(argv[2], &e, 0);
		if (*e) {
			ccputs("Invalid sleep ms\n");
			return EC_ERROR_INVAL;
		}
	}

	for (loop = 0; loop < repeat; loop++) {
		rv = print_battery_info();

		if (sleep_ms)
			msleep(sleep_ms);

		if (rv)
			break;
	}

	if (rv)
		ccprintf("Failed - error %d\n", rv);

	return rv ? EC_ERROR_UNKNOWN : EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(battery, command_battery,
			"<repeat_count> <sleep_ms>",
			"Print battery info",
			NULL);
