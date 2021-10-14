/* Copyright 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Common battery command.
 */

#include "battery.h"
#include "charge_manager.h"
#include "charge_state.h"
#include "common.h"
#include "console.h"
#include "ec_ec_comm_client.h"
#include "extpower.h"
#include "gpio.h"
#include "hooks.h"
#include "host_command.h"
#include "math_util.h"
#include "timer.h"
#include "usb_pd.h"
#include "util.h"
#include "watchdog.h"

#define CPRINTF(format, args...) cprintf(CC_CHARGER, format, ## args)
#define CPRINTS(format, args...) cprints(CC_CHARGER, format, ## args)
#define CUTOFFPRINTS(info) CPRINTS("%s %s", "Battery cut off", info)

/* See config.h for details */
const static int batt_host_full_factor = CONFIG_BATT_HOST_FULL_FACTOR;
const static int batt_host_shutdown_pct = CONFIG_BATT_HOST_SHUTDOWN_PERCENTAGE;

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

	if (IS_ENABLED(CONFIG_CHARGER)) {
		int value;

		print_item_name("  Display:");
		value = charge_get_display_charge();
		ccprintf("%d.%d %%\n", value / 10, value % 10);
	}
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

	print_item_name("full_factor:");
	ccprintf("0.%d\n", batt_host_full_factor);

	print_item_name("shutdown_soc:");
	ccprintf("%d %%\n", batt_host_shutdown_pct);
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

	if (rv == EC_RES_SUCCESS) {
		CUTOFFPRINTS("succeeded.");
		battery_cutoff_state = BATTERY_CUTOFF_STATE_CUT_OFF;
	} else {
		CUTOFFPRINTS("failed!");
		battery_cutoff_state = BATTERY_CUTOFF_STATE_NORMAL;
	}
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

static enum ec_status battery_command_cutoff(struct host_cmd_handler_args *args)
{
	const struct ec_params_battery_cutoff *p;
	int rv;

	if (args->version == 1) {
		p = args->params;
		if (p->flags & EC_BATTERY_CUTOFF_FLAG_AT_SHUTDOWN) {
			battery_cutoff_state = BATTERY_CUTOFF_STATE_PENDING;
			CUTOFFPRINTS("at-shutdown is scheduled");
			return EC_RES_SUCCESS;
		}
	}

	rv = board_cut_off_battery();
	if (rv == EC_RES_SUCCESS) {
		CUTOFFPRINTS("is successful.");
		battery_cutoff_state = BATTERY_CUTOFF_STATE_CUT_OFF;
	} else {
		CUTOFFPRINTS("has failed.");
	}

	return rv;
}
DECLARE_HOST_COMMAND(EC_CMD_BATTERY_CUT_OFF, battery_command_cutoff,
		EC_VER_MASK(0) | EC_VER_MASK(1));

static void check_pending_cutoff(void)
{
	if (battery_cutoff_state == BATTERY_CUTOFF_STATE_PENDING) {
		CPRINTS("Cutting off battery in %d second(s)",
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
	if (rv == EC_RES_SUCCESS) {
		ccprints("Battery cut off");
		battery_cutoff_state = BATTERY_CUTOFF_STATE_CUT_OFF;
		return EC_SUCCESS;
	}

	return EC_ERROR_UNKNOWN;
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
__overridable int battery_get_vendor_param(uint32_t param, uint32_t *value)
{
	const struct battery_info *bi = battery_get_info();
	struct battery_static_info *bs = &battery_static[BATT_IDX_MAIN];
	uint8_t *data = bs->vendor_param;
	int rv;

	if (param < bi->vendor_param_start)
		return EC_ERROR_ACCESS_DENIED;

	/* Skip read if cache is valid. */
	if (!data[0]) {
		rv = sb_read_string(bi->vendor_param_start, data,
				    sizeof(bs->vendor_param));
		if (rv) {
			data[0] = 0;
			return rv;
		}
	}

	if (param > bi->vendor_param_start + strlen(data))
		return EC_ERROR_INVAL;

	*value = data[param - bi->vendor_param_start];
	return EC_SUCCESS;
}

__overridable int battery_set_vendor_param(uint32_t param, uint32_t value)
{
	return EC_ERROR_UNIMPLEMENTED;
}

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

static enum ec_status
host_command_battery_vendor_param(struct host_cmd_handler_args *args)
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


void battery_compensate_params(struct batt_params *batt)
{
	int numer, denom;
	int *remain = &(batt->remaining_capacity);
	int full = batt->full_capacity;

	if ((batt->flags & BATT_FLAG_BAD_FULL_CAPACITY) ||
			(batt->flags & BATT_FLAG_BAD_REMAINING_CAPACITY))
		return;

	if (*remain <= 0 || full <= 0)
		return;

	/* Some batteries don't update full capacity as often. */
	if (*remain > full)
		*remain = full;

	/*
	 * EC calculates the display SoC like how Powerd used to do. Powerd
	 * reads the display SoC from the EC. This design allows the system to
	 * behave consistently on a single SoC value across all power states.
	 *
	 * Display SoC is computed as follows:
	 *
	 *   actual_soc = 100 * remain / full
	 *
	 *		   actual_soc - shutdown_pct
	 *   display_soc = --------------------------- x 1000
	 *		   full_factor - shutdown_pct
	 *
	 *		   (100 * remain / full) - shutdown_pct
	 *		 = ------------------------------------ x 1000
	 *		        full_factor - shutdown_pct
	 *
	 *		   100 x remain - full x shutdown_pct
	 *		 = ----------------------------------- x 1000
	 *		   full x (full_factor - shutdown_pct)
	 */
	numer = 1000 * ((100 * *remain) - (full * batt_host_shutdown_pct));
	denom = full * (batt_host_full_factor - batt_host_shutdown_pct);
	/* Rounding (instead of truncating) */
	batt->display_charge = (numer + denom / 2) / denom;
	if (batt->display_charge < 0)
		batt->display_charge = 0;
	if (batt->display_charge > 1000)
		batt->display_charge = 1000;
}

#ifdef CONFIG_CHARGER
static enum ec_status battery_display_soc(struct host_cmd_handler_args *args)
{
	struct ec_response_display_soc *r = args->response;

	r->display_soc = charge_get_display_charge();
	r->full_factor = batt_host_full_factor * 10;
	r->shutdown_soc = batt_host_shutdown_pct * 10;
	args->response_size = sizeof(*r);

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_DISPLAY_SOC, battery_display_soc, EC_VER_MASK(0));
#endif

__overridable void board_battery_compensate_params(struct batt_params *batt)
{
}

__attribute__((weak)) int get_battery_manufacturer_name(char *dest, int size)
{
	strzcpy(dest, "<unkn>", size);
	return EC_SUCCESS;
}

__overridable int battery_get_avg_voltage(void)
{
	return -EC_ERROR_UNIMPLEMENTED;
}

__overridable int battery_get_avg_current(void)
{
	return -EC_ERROR_UNIMPLEMENTED;
}

int battery_manufacturer_name(char *dest, int size)
{
	return get_battery_manufacturer_name(dest, size);
}

__overridable enum battery_disconnect_state battery_get_disconnect_state(void)
{
	return BATTERY_NOT_DISCONNECTED;
}

#ifdef CONFIG_BATT_FULL_CHIPSET_OFF_INPUT_LIMIT_MV

#if CONFIG_BATT_FULL_CHIPSET_OFF_INPUT_LIMIT_MV < 5000 || \
    CONFIG_BATT_FULL_CHIPSET_OFF_INPUT_LIMIT_MV >= PD_MAX_VOLTAGE_MV
	#error "Voltage limit must be between 5000 and PD_MAX_VOLTAGE_MV"
#endif

#if !((defined(CONFIG_USB_PD_TCPMV1) && defined(CONFIG_USB_PD_DUAL_ROLE)) || \
    (defined(CONFIG_USB_PD_TCPMV2) && defined(CONFIG_USB_PE_SM)))
	#error "Voltage reducing requires TCPM with Policy Engine"
#endif

/*
 * Returns true if input voltage should be reduced (chipset is in S5/G3) and
 * battery is full, otherwise returns false
 */
static bool board_wants_reduced_input_voltage(void) {
	struct batt_params batt;

	/* Chipset not in S5/G3, so we don't want to reduce voltage */
	if (!chipset_in_or_transitioning_to_state(CHIPSET_STATE_ANY_OFF))
		return false;

	battery_get_params(&batt);

	/* Battery needs charge, so we don't want to reduce voltage */
	if (batt.flags & BATT_FLAG_WANT_CHARGE)
		return false;

	return true;
}

static void reduce_input_voltage_when_full(void)
{
	static int saved_input_voltage = -1;
	int max_pd_voltage_mv = pd_get_max_voltage();
	int port;

	port = charge_manager_get_active_charge_port();
	if (port < 0 || port >= board_get_usb_pd_port_count())
		return;

	if (board_wants_reduced_input_voltage()) {
		/*
		 * Board wants voltage to be reduced. Apply limit if current
		 * voltage is different. Save current voltage, it will be
		 * restored when board wants to stop reducing input voltage.
		 */
		if (max_pd_voltage_mv !=
		    CONFIG_BATT_FULL_CHIPSET_OFF_INPUT_LIMIT_MV) {
			saved_input_voltage = max_pd_voltage_mv;
			max_pd_voltage_mv =
			    CONFIG_BATT_FULL_CHIPSET_OFF_INPUT_LIMIT_MV;
		}
	} else if (saved_input_voltage != -1) {
		/*
		 * Board doesn't want to reduce input voltage. If current
		 * voltage is reduced we will restore previously saved voltage.
		 * If current voltage is different we will respect newer value.
		 */
		if (max_pd_voltage_mv ==
		    CONFIG_BATT_FULL_CHIPSET_OFF_INPUT_LIMIT_MV)
			max_pd_voltage_mv = saved_input_voltage;

		saved_input_voltage = -1;
	}

	if (pd_get_max_voltage() != max_pd_voltage_mv)
		pd_set_external_voltage_limit(port, max_pd_voltage_mv);
}
DECLARE_HOOK(HOOK_AC_CHANGE, reduce_input_voltage_when_full,
	     HOOK_PRIO_DEFAULT);
DECLARE_HOOK(HOOK_BATTERY_SOC_CHANGE, reduce_input_voltage_when_full,
	     HOOK_PRIO_DEFAULT);
DECLARE_HOOK(HOOK_CHIPSET_STARTUP, reduce_input_voltage_when_full,
	     HOOK_PRIO_DEFAULT);
DECLARE_HOOK(HOOK_CHIPSET_SHUTDOWN, reduce_input_voltage_when_full,
	     HOOK_PRIO_DEFAULT);
#endif

void battery_validate_params(struct batt_params *batt)
{
	/*
	 * TODO(crosbug.com/p/27527). Sometimes the battery thinks its
	 * temperature is 6280C, which seems a bit high. Let's ignore
	 * anything above the boiling point of tungsten until this bug
	 * is fixed. If the battery is really that warm, we probably
	 * have more urgent problems.
	 */
	if (batt->temperature > CELSIUS_TO_DECI_KELVIN(5660)) {
		CPRINTS("ignoring ridiculous batt.temp of %dC",
			 DECI_KELVIN_TO_CELSIUS(batt->temperature));
		batt->flags |= BATT_FLAG_BAD_TEMPERATURE;
	}

	/* If the battery thinks it's above 100%, don't believe it */
	if (batt->state_of_charge > 100) {
		CPRINTS("ignoring ridiculous batt.soc of %d%%",
			batt->state_of_charge);
		batt->flags |= BATT_FLAG_BAD_STATE_OF_CHARGE;
	}
}
