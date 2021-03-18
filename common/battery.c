/* Copyright 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Common battery command.
 */

#include "battery.h"
#include "charge_state.h"
#include "common.h"
#include "console.h"
#include "ec_ec_comm_master.h"
#include "extpower.h"
#include "gpio.h"
#include "hooks.h"
#include "host_command.h"
#include "timer.h"
#include "util.h"
#include "watchdog.h"

#define CPRINTF(format, args...) cprintf(CC_CHARGER, format, ## args)
#define CPRINTS(format, args...) cprints(CC_CHARGER, format, ## args)
#define CUTOFFPRINTS(info) CPRINTS("%s %s", "Battery cut off", info)

/* See config.h for details */
const static int batt_full_factor = CONFIG_BATT_FULL_FACTOR;
const static int batt_host_full_factor = CONFIG_BATT_HOST_FULL_FACTOR;
const static int batt_host_shutdown_pct = CONFIG_BATT_HOST_SHUTDOWN_PERCENTAGE;

#ifdef CONFIG_BATTERY_V2
/*
 * Store battery information in these 2 structures. Main (lid) battery is always
 * at index 0, and secondary (base) battery at index 1.
 */
struct ec_response_battery_static_info battery_static[CONFIG_BATTERY_COUNT];
struct ec_response_battery_dynamic_info battery_dynamic[CONFIG_BATTERY_COUNT];
#endif

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
		ccprintf("%d mAh (%d mAh with %d %% compensation)\n",
			 value, value*batt_full_factor/100, batt_full_factor);

#ifdef CONFIG_CHARGER
	print_item_name("Display:");
	value = charge_get_display_charge();
	ccprintf("%d.%d %%\n", value / 10, value % 10);
#endif

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

#ifdef CONFIG_BATTERY_V2
#ifdef CONFIG_HOSTCMD_BATTERY_V2
static void battery_update(enum battery_index i);
static enum ec_status
host_command_battery_get_static(struct host_cmd_handler_args *args)
{
	const struct ec_params_battery_static_info *p = args->params;
	struct ec_response_battery_static_info *r = args->response;

	if (p->index < 0 || p->index >= CONFIG_BATTERY_COUNT)
		return EC_RES_INVALID_PARAM;
	battery_update(p->index);
	args->response_size = sizeof(*r);
	memcpy(r, &battery_static[p->index], sizeof(*r));

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_BATTERY_GET_STATIC,
		     host_command_battery_get_static,
		     EC_VER_MASK(0));

static enum ec_status
host_command_battery_get_dynamic(struct host_cmd_handler_args *args)
{
	const struct ec_params_battery_dynamic_info *p = args->params;
	struct ec_response_battery_dynamic_info *r = args->response;

	if (p->index < 0 || p->index >= CONFIG_BATTERY_COUNT)
		return EC_RES_INVALID_PARAM;

	args->response_size = sizeof(*r);
	memcpy(r, &battery_dynamic[p->index], sizeof(*r));

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_BATTERY_GET_DYNAMIC,
		     host_command_battery_get_dynamic,
		     EC_VER_MASK(0));
#endif /* CONFIG_HOSTCMD_BATTERY_V2 */

#ifdef HAS_TASK_HOSTCMD
static void battery_update(enum battery_index i)
{
	char *batt_str;
	int *memmap_dcap = (int *)host_get_memmap(EC_MEMMAP_BATT_DCAP);
	int *memmap_dvlt = (int *)host_get_memmap(EC_MEMMAP_BATT_DVLT);
	int *memmap_ccnt = (int *)host_get_memmap(EC_MEMMAP_BATT_CCNT);
	int *memmap_volt = (int *)host_get_memmap(EC_MEMMAP_BATT_VOLT);
	int *memmap_rate = (int *)host_get_memmap(EC_MEMMAP_BATT_RATE);
	int *memmap_cap = (int *)host_get_memmap(EC_MEMMAP_BATT_CAP);
	int *memmap_lfcc = (int *)host_get_memmap(EC_MEMMAP_BATT_LFCC);
	uint8_t *memmap_flags = host_get_memmap(EC_MEMMAP_BATT_FLAG);

	/* Smart battery serial number is 16 bits */
	batt_str = (char *)host_get_memmap(EC_MEMMAP_BATT_SERIAL);
	memcpy(batt_str, battery_static[i].serial, EC_MEMMAP_TEXT_MAX);

	/* Design Capacity of Full */
	*memmap_dcap = battery_static[i].design_capacity;

	/* Design Voltage */
	*memmap_dvlt = battery_static[i].design_voltage;

	/* Cycle Count */
	*memmap_ccnt = battery_static[i].cycle_count;

	/* Battery Manufacturer string */
	batt_str = (char *)host_get_memmap(EC_MEMMAP_BATT_MFGR);
	memcpy(batt_str, battery_static[i].manufacturer, EC_MEMMAP_TEXT_MAX);

	/* Battery Model string */
	batt_str = (char *)host_get_memmap(EC_MEMMAP_BATT_MODEL);
	memcpy(batt_str, battery_static[i].model, EC_MEMMAP_TEXT_MAX);

	/* Battery Type string */
	batt_str = (char *)host_get_memmap(EC_MEMMAP_BATT_TYPE);
	memcpy(batt_str, battery_static[i].type, EC_MEMMAP_TEXT_MAX);

	*memmap_volt = battery_dynamic[i].actual_voltage;
	*memmap_rate = battery_dynamic[i].actual_current;
	*memmap_cap = battery_dynamic[i].remaining_capacity;
	*memmap_lfcc = battery_dynamic[i].full_capacity;
	*memmap_flags = battery_dynamic[i].flags;
}

void battery_memmap_refresh(enum battery_index index)
{
	if (*host_get_memmap(EC_MEMMAP_BATT_INDEX) == index)
		battery_update(index);
}

void battery_memmap_set_index(enum battery_index index)
{
	if (*host_get_memmap(EC_MEMMAP_BATT_INDEX) == index)
		return;

	*host_get_memmap(EC_MEMMAP_BATT_INDEX) = BATT_IDX_INVALID;
	if (index < 0 || index >= CONFIG_BATTERY_COUNT)
		return;

	battery_update(index);
	*host_get_memmap(EC_MEMMAP_BATT_INDEX) = index;
}

static void battery_init(void)
{
	*host_get_memmap(EC_MEMMAP_BATT_INDEX) = BATT_IDX_INVALID;
	*host_get_memmap(EC_MEMMAP_BATT_COUNT) = CONFIG_BATTERY_COUNT;
	*host_get_memmap(EC_MEMMAP_BATTERY_VERSION) = 2;

	battery_memmap_set_index(BATT_IDX_MAIN);
}
DECLARE_HOOK(HOOK_INIT, battery_init, HOOK_PRIO_DEFAULT);
#endif /* HAS_TASK_HOSTCMD */
#endif /* CONFIG_BATTERY_V2 */

void battery_compensate_params(struct batt_params *batt)
{
	int numer, denom;
	int *remain = &(batt->remaining_capacity);
	int *full = &(batt->full_capacity);

	if ((batt->flags & BATT_FLAG_BAD_FULL_CAPACITY) ||
			(batt->flags & BATT_FLAG_BAD_REMAINING_CAPACITY))
		return;

	if (*remain <= 0 || *full <= 0)
		return;

	/* full_factor is effectively disabled in powerd. */
	*full = *full * batt_full_factor / 100;
	if (*remain > *full)
		*remain = *full;

	/*
	 * Powerd uses the following equation to calculate display percentage:
	 *   charge = 100 * remain / full
	 *   display = 100 * (charge - shutdown_pct) /
	 *		     (full_factor - shutdown_pct)
	 *	     = 100 * ((100 * remain / full) - shutdown_pct) /
	 *		     (full_factor - shutdown_pct)
	 *	     = 100 * ((100 * remain) - (full * shutdown_pct)) /
	 *		     (full * (full_factor - shutdown_pct))
	 *
	 * The unit of the following batt->display_charge is 0.1%.
	 */
	numer = 1000 * ((100 * *remain) - (*full * batt_host_shutdown_pct));
	denom = *full * (batt_host_full_factor - batt_host_shutdown_pct);
	/* Rounding (instead of truncating) */
	batt->display_charge = (numer + denom / 2) / denom;
	if (batt->display_charge < 0)
		batt->display_charge = 0;
	if (batt->display_charge > 1000)
		batt->display_charge = 1000;
}

__overridable void board_battery_compensate_params(struct batt_params *batt)
{
}

__attribute__((weak)) int get_battery_manufacturer_name(char *dest, int size)
{
	strzcpy(dest, "<unkn>", size);
	return EC_SUCCESS;
}

int battery_manufacturer_name(char *dest, int size)
{
	return get_battery_manufacturer_name(dest, size);
}

__overridable void battery_charger_notify(uint8_t flag)
{
}
