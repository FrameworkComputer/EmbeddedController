/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Common battery command.
 */

#include "battery.h"
#include "charge_state.h"
#include "common.h"
#include "console.h"
#include "extpower.h"
#include "gpio.h"
#include "hooks.h"
#include "host_command.h"
#include "timer.h"
#include "util.h"
#include "watchdog.h"

#define CPRINTF(format, args...) cprintf(CC_CHARGER, format, ## args)
#define CPRINTS(format, args...) cprints(CC_CHARGER, format, ## args)

#ifdef CONFIG_BATTERY_CUT_OFF

#ifndef CONFIG_BATTERY_CUTOFF_DELAY_US
#define CONFIG_BATTERY_CUTOFF_DELAY_US (1 * SECOND)
#endif

static enum battery_cutoff_states battery_cutoff_state =
	BATTERY_CUTOFF_STATE_NORMAL;

#endif

#ifdef CONFIG_BATTERY_PRESENT_GPIO
#ifdef CONFIG_BATTERY_PRESENT_CUSTOM
#error "Don't define both CONFIG_BATTERY_PRESENT_CUSTOM and" \
	"CONFIG_BATTERY_PRESENT_GPIO"
#endif
/**
 * Physical detection of battery.
 */
enum battery_present battery_is_present(void)
{
	/* The GPIO is low when the battery is present */
	return gpio_get_level(CONFIG_BATTERY_PRESENT_GPIO) ? BP_NO : BP_YES;
}
#endif

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

static void print_battery_status(void)
{
	static const char * const st[] = {"EMPTY", "FULL", "DCHG", "INIT",};
	static const char * const al[] = {"RT", "RC", "--", "TD",
					  "OT", "--", "TC", "OC"};

	int value, i;

	print_item_name("Status:");
	if (check_print_error(battery_status(&value))) {
		ccprintf("0x%04x", value);

		/* bits 0-3 are only valid when the previous transaction
		 * failed, so ignore them */

		/* bits 4-7 are status */
		for (i = 0; i < 4; i++)
			if (value & (1 << (i+4)))
				ccprintf(" %s", st[i]);

		/* bits 15-8 are alarms */
		for (i = 0; i < 8; i++)
			if (value & (1 << (i+8)))
				ccprintf(" %s", al[i]);

		ccprintf("\n");
	}
}

static void print_battery_strings(void)
{
	char text[32];

	print_item_name("Manuf:");
	if (check_print_error(battery_manufacturer_name(text, sizeof(text))))
		ccprintf("%s\n", text);

	print_item_name("Device:");
	if (check_print_error(battery_device_name(text, sizeof(text))))
		ccprintf("%s\n", text);

	print_item_name("Chem:");
	if (check_print_error(battery_device_chemistry(text, sizeof(text))))
		ccprintf("%s\n", text);
}

static void print_battery_params(void)
{
#if defined(HAS_TASK_CHARGER)
	/* Ask charger so that we don't need to ask battery again. */
	const struct batt_params *batt = charger_current_battery_params();
#else
	/* This is for test code, where doesn't have charger task. */
	struct batt_params _batt;
	const struct batt_params *batt = &_batt;

	battery_get_params(&_batt);
#endif

	print_item_name("Param flags:");
	ccprintf("%08x\n", batt->flags);

	print_item_name("Temp:");
	ccprintf("0x%04x = %.1d K (%.1d C)\n",
		 batt->temperature,
		 batt->temperature,
		 batt->temperature - 2731);

	print_item_name("V:");
	ccprintf("0x%04x = %d mV\n", batt->voltage, batt->voltage);

	print_item_name("V-desired:");
	ccprintf("0x%04x = %d mV\n", batt->desired_voltage,
		 batt->desired_voltage);

	print_item_name("I:");
	ccprintf("0x%04x = %d mA", batt->current & 0xffff, batt->current);
	if (batt->current > 0)
		ccputs("(CHG)");
	else if (batt->current < 0)
		ccputs("(DISCHG)");
	ccputs("\n");

	print_item_name("I-desired:");
	ccprintf("0x%04x = %d mA\n", batt->desired_current,
		 batt->desired_current);

	print_item_name("Charging:");
	ccprintf("%sAllowed\n",
		 batt->flags & BATT_FLAG_WANT_CHARGE ? "" : "Not ");

	print_item_name("Charge:");
		ccprintf("%d %%\n", batt->state_of_charge);
}

static void print_battery_info(void)
{
	int value;
	int hour, minute;

	print_item_name("Serial:");
	if (check_print_error(battery_serial_number(&value)))
		ccprintf("0x%04x\n", value);

	print_item_name("V-design:");
	if (check_print_error(battery_design_voltage(&value)))
		ccprintf("0x%04x = %d mV\n", value, value);

	print_item_name("Mode:");
	if (check_print_error(battery_get_mode(&value)))
		ccprintf("0x%04x\n", value);

	print_item_name("Abs charge:");
	if (check_print_error(battery_state_of_charge_abs(&value)))
		ccprintf("%d %%\n", value);

	print_item_name("Remaining:");
	if (check_print_error(battery_remaining_capacity(&value)))
		ccprintf("%d mAh\n", value);

	print_item_name("Cap-full:");
	if (check_print_error(battery_full_charge_capacity(&value)))
		ccprintf("%d mAh\n", value);

	print_item_name("  Design:");
	if (check_print_error(battery_design_capacity(&value)))
		ccprintf("%d mAh\n", value);

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
}

void print_battery_debug(void)
{
	print_battery_status();
	print_battery_params();
	print_battery_strings();
	print_battery_info();
}

static int command_battery(int argc, char **argv)
{
	int repeat = 1;
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
		print_battery_debug();

		/*
		 * Running with a high repeat count will take so long the
		 * watchdog timer fires.  So reset the watchdog timer each
		 * iteration.
		 */
		watchdog_reload();

		if (sleep_ms)
			msleep(sleep_ms);
	}

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(battery, command_battery,
			"<repeat_count> <sleep_ms>",
			"Print battery info");

#ifdef CONFIG_BATTERY_CUT_OFF
int battery_is_cut_off(void)
{
	return (battery_cutoff_state == BATTERY_CUTOFF_STATE_CUT_OFF);
}

static void pending_cutoff_deferred(void)
{
	int rv;

	rv = board_cut_off_battery();

	if (rv == EC_SUCCESS)
		CPRINTF("[%T Battery cut off succeeded.]\n");
	else
		CPRINTF("[%T Battery cut off failed!]\n");
}
DECLARE_DEFERRED(pending_cutoff_deferred);

static void clear_pending_cutoff(void)
{
	if (extpower_is_present()) {
		battery_cutoff_state = BATTERY_CUTOFF_STATE_NORMAL;
		hook_call_deferred(&pending_cutoff_deferred_data, -1);
	}
}
DECLARE_HOOK(HOOK_AC_CHANGE, clear_pending_cutoff, HOOK_PRIO_DEFAULT);

static int battery_command_cutoff(struct host_cmd_handler_args *args)
{
	const struct ec_params_battery_cutoff *p;
	int rv;

	if (args->version == 1) {
		p = args->params;
		if (p->flags & EC_BATTERY_CUTOFF_FLAG_AT_SHUTDOWN) {
			battery_cutoff_state = BATTERY_CUTOFF_STATE_PENDING;
			CPRINTS("Battery cut off at-shutdown is scheduled");
			return EC_RES_SUCCESS;
		}
	}

	rv = board_cut_off_battery();
	if (!rv) {
		CPRINTS("Battery cut off is successful.");
		battery_cutoff_state = BATTERY_CUTOFF_STATE_CUT_OFF;
	} else {
		CPRINTS("Battery cut off has failed.");
	}

	return rv;
}
DECLARE_HOST_COMMAND(EC_CMD_BATTERY_CUT_OFF, battery_command_cutoff,
		EC_VER_MASK(0) | EC_VER_MASK(1));

static void check_pending_cutoff(void)
{
	if (battery_cutoff_state == BATTERY_CUTOFF_STATE_PENDING) {
		CPRINTF("[%T Cutting off battery in %d second(s)]\n",
			CONFIG_BATTERY_CUTOFF_DELAY_US / SECOND);
		hook_call_deferred(&pending_cutoff_deferred_data,
				   CONFIG_BATTERY_CUTOFF_DELAY_US);
	}
}
DECLARE_HOOK(HOOK_CHIPSET_SHUTDOWN, check_pending_cutoff, HOOK_PRIO_LAST);

static int command_cutoff(int argc, char **argv)
{
	int rv;

	if (argc > 1) {
		if (!strcasecmp(argv[1], "at-shutdown")) {
			battery_cutoff_state = BATTERY_CUTOFF_STATE_PENDING;
			return EC_SUCCESS;
		} else {
			return EC_ERROR_INVAL;
		}
	}

	rv = board_cut_off_battery();
	if (!rv) {
		ccprintf("[%T Battery cut off]\n");
		battery_cutoff_state = BATTERY_CUTOFF_STATE_CUT_OFF;
	}

	return rv;
}
DECLARE_CONSOLE_COMMAND(cutoff, command_cutoff,
		"[at-shutdown]",
		"Cut off the battery output");
#else
int battery_is_cut_off(void)
{
	return 0;  /* Always return NOT cut off */
}
#endif  /* CONFIG_BATTERY_CUT_OFF */

#ifdef CONFIG_BATTERY_VENDOR_PARAM
static int console_command_battery_vendor_param(int argc, char **argv)
{
	uint32_t param;
	uint32_t value;
	char *e;
	int rv;

	if (argc < 2)
		return EC_ERROR_INVAL;

	param = strtoi(argv[1], &e, 0);
	if (*e) {
		ccputs("Invalid param\n");
		return EC_ERROR_INVAL;
	}

	if (argc > 2) {
		value = strtoi(argv[2], &e, 0);
		if (*e) {
			ccputs("Invalid value\n");
			return EC_ERROR_INVAL;
		}
		rv = battery_set_vendor_param(param, value);
		if (rv != EC_SUCCESS)
			return rv;
	}

	rv = battery_get_vendor_param(param, &value);
	if (rv != EC_SUCCESS)
		return rv;

	ccprintf("0x%08x\n", value);
	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(battparam, console_command_battery_vendor_param,
			"<param> [value]",
			"Get or set battery vendor parameters");

static int host_command_battery_vendor_param(struct host_cmd_handler_args *args)
{
	int rv;
	const struct ec_params_battery_vendor_param *p = args->params;
	struct ec_response_battery_vendor_param *r = args->response;

	args->response_size = sizeof(*r);

	if (p->mode != BATTERY_VENDOR_PARAM_MODE_GET &&
	    p->mode != BATTERY_VENDOR_PARAM_MODE_SET)
		return EC_RES_INVALID_PARAM;

	if (p->mode == BATTERY_VENDOR_PARAM_MODE_SET) {
		rv = battery_set_vendor_param(p->param, p->value);
		if (rv != EC_SUCCESS)
			return rv;
	}

	rv = battery_get_vendor_param(p->param, &r->value);
	return rv;
}
DECLARE_HOST_COMMAND(EC_CMD_BATTERY_VENDOR_PARAM,
		     host_command_battery_vendor_param,
		     EC_VER_MASK(0));
#endif /* CONFIG_BATTERY_VENDOR_PARAM */
