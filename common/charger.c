/* Copyright 2013 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Common functions for battery charging.
 */

/*
 * TODO(b/272518464): Work around coreboot GCC preprocessor bug.
 * #line marks the *next* line, so it is off by one.
 */
#line 13

#include "battery_smart.h"
#include "builtin/assert.h"
#include "charge_state.h"
#include "charger.h"
#include "common.h"
#include "console.h"
#include "dptf.h"
#include "hooks.h"
#include "host_command.h"
#include "printf.h"
#include "util.h"

/* Console output macros */
#define CPUTS(outstr) cputs(CC_CHARGER, outstr)
#define CPRINTS(format, args...) cprints(CC_CHARGER, format, ##args)

/* DPTF current limit, -1 = none */
static int dptf_limit_ma = -1;

void dptf_set_charging_current_limit(int ma)
{
	dptf_limit_ma = ma >= 0 ? ma : -1;
}

int dptf_get_charging_current_limit(void)
{
	return dptf_limit_ma;
}

static void dptf_disable_hook(void)
{
	/* Before get to Sx, EC should take control of charger from DPTF */
	dptf_limit_ma = -1;
}
DECLARE_HOOK(HOOK_CHIPSET_SUSPEND, dptf_disable_hook, HOOK_PRIO_DEFAULT);
DECLARE_HOOK(HOOK_CHIPSET_SHUTDOWN, dptf_disable_hook, HOOK_PRIO_DEFAULT);

/*
 * Boards should override this function if their count may vary during run-time
 * due to different DB options.
 */
__overridable uint8_t board_get_charger_chip_count(void)
{
	return CHARGER_NUM;
}

int charger_closest_voltage(int voltage)
{
	const struct charger_info *info = charger_get_info();

	/*
	 * If the requested voltage is non-zero but below our minimum,
	 * return the minimum.  See crosbug.com/p/8662.
	 */
	if (voltage > 0 && voltage < info->voltage_min)
		return info->voltage_min;

	/* Clip to max */
	if (voltage > info->voltage_max)
		return info->voltage_max;

	/* Otherwise round down to nearest voltage step */
	return voltage - (voltage % info->voltage_step);
}

int charger_closest_current(int current)
{
	const struct charger_info *const info = charger_get_info();

	/* Apply DPTF limit if necessary */
	if (dptf_limit_ma >= 0 && current > dptf_limit_ma)
		current = dptf_limit_ma;

	/*
	 * If the requested current is non-zero but below our minimum,
	 * return the minimum.  See crosbug.com/p/8662.
	 */
	if (current > 0 && current < info->current_min)
		return info->current_min;

	/* Clip to max */
	if (current > info->current_max)
		return info->current_max;

	/* Otherwise round down to nearest current step */
	return current - (current % info->current_step);
}

void charger_get_params(struct charger_params *chg)
{
	int chgnum = 0;

	if (IS_ENABLED(CONFIG_OCPC)) {
		chgnum = charge_get_active_chg_chip();
		/* set to CHARGE_PORT_NONE when no charger connected */
		if (chgnum < 0)
			chgnum = 0;
	}

	memset(chg, 0, sizeof(*chg));

	/*
	 * Only the primary charger(0) can tightly regulate the current,
	 * therefore always query the primary charger.
	 */
	if (charger_get_current(0, &chg->current))
		chg->flags |= CHG_FLAG_BAD_CURRENT;

	if (charger_get_voltage(chgnum, &chg->voltage))
		chg->flags |= CHG_FLAG_BAD_VOLTAGE;

	if (charger_get_input_current_limit(chgnum, &chg->input_current))
		chg->flags |= CHG_FLAG_BAD_INPUT_CURRENT;

	if (charger_get_status(&chg->status))
		chg->flags |= CHG_FLAG_BAD_STATUS;

	if (charger_get_option(&chg->option))
		chg->flags |= CHG_FLAG_BAD_OPTION;
}

static void print_item_name(const char *name)
{
	ccprintf("  %-8s", name);
}

static int check_print_error(int rv)
{
	if (rv == EC_SUCCESS)
		return 1;
	if (rv == EC_ERROR_UNIMPLEMENTED) {
		ccputs("(unsupported)\n");

	} else {
		ccputs("(error)\n");
	}
	return 0;
}

void print_charger_debug(int chgnum)
{
	int d;
	const struct charger_info *info = charger_get_info();

	/* info */
	print_item_name("Name:");
	ccprintf("%s\n", info->name);

	/* option */
	print_item_name("Option:");
	if (check_print_error(charger_get_option(&d)))
		ccprintf("(0x%04x)\n", d);

	/* manufacturer id */
	print_item_name("Man id:");
	if (check_print_error(charger_manufacturer_id(&d)))
		ccprintf("0x%04x\n", d);

	/* device id */
	print_item_name("Dev id:");
	if (check_print_error(charger_device_id(&d)))
		ccprintf("0x%04x\n", d);

	/* dptf current limit */
	print_item_name("I_dptf:");
	if (dptf_limit_ma >= 0)
		ccprintf("%5d\n", dptf_limit_ma);
	else
		ccputs("disabled\n");

	/* Limits */
	ccprintf("Limits\t\t\t ( min     max step)\n");

	/* charge voltage limit */
	print_item_name("chg_voltage:");
	if (check_print_error(charger_get_voltage(chgnum, &d)))
		ccprintf("%7d mV (%4d - %5d, %3d)\n", d, info->voltage_min,
			 info->voltage_max, info->voltage_step);

	/* charge current limit */
	print_item_name("chg_current:");
	if (check_print_error(charger_get_current(chgnum, &d)))
		ccprintf("%7d mA (%4d - %5d, %3d)\n", d, info->current_min,
			 info->current_max, info->current_step);

	/* input current limit */
	print_item_name("input_current:");
	if (check_print_error(charger_get_input_current_limit(chgnum, &d)))
		ccprintf("%5d mA (%4d - %5d, %3d)\n", d,
			 info->input_current_min, info->input_current_max,
			 info->input_current_step);
}

void print_charger_prochot(int chgnum)
{
	if ((chgnum < 0) || (chgnum >= board_get_charger_chip_count()))
		return;

	if (chg_chips[chgnum].drv->dump_prochot)
		chg_chips[chgnum].drv->dump_prochot(chgnum);
}

static int command_charger(int argc, const char **argv)
{
	int d;
	char *e;
	int idx_provided = 0;
	int chgnum;

	if (argc == 1) {
		print_charger_debug(0);
		return EC_SUCCESS;
	}

	idx_provided = isdigit((unsigned char)argv[1][0]);
	if (idx_provided) {
		chgnum = atoi(argv[1]);
		if ((chgnum < 0) || (chgnum >= board_get_charger_chip_count()))
			return EC_ERROR_PARAM1;
	} else {
		chgnum = 0;
	}

	if ((argc == 2) && idx_provided) {
		print_charger_debug(chgnum);
		return EC_SUCCESS;
	}

	if (strcasecmp(argv[1 + idx_provided], "input") == 0) {
		d = strtoi(argv[2 + idx_provided], &e, 0);
		if (*e)
			return EC_ERROR_PARAM2 + idx_provided;
		return charger_set_input_current_limit(chgnum, d);
	} else if (IS_ENABLED(CONFIG_BATTERY) &&
		   strcasecmp(argv[1 + idx_provided], "current") == 0) {
		d = strtoi(argv[2 + idx_provided], &e, 0);
		if (*e)
			return EC_ERROR_PARAM2 + idx_provided;
		chgstate_set_manual_current(d);
		return charger_set_current(chgnum, d);
	} else if (IS_ENABLED(CONFIG_BATTERY) &&
		   strcasecmp(argv[1 + idx_provided], "voltage") == 0) {
		d = strtoi(argv[2 + idx_provided], &e, 0);
		if (*e)
			return EC_ERROR_PARAM2 + idx_provided;
		chgstate_set_manual_voltage(d);
		return charger_set_voltage(chgnum, d);
	} else if (strcasecmp(argv[1 + idx_provided], "dptf") == 0) {
		d = strtoi(argv[2 + idx_provided], &e, 0);
		if (*e)
			return EC_ERROR_PARAM2 + idx_provided;
		dptf_limit_ma = d;
		return EC_SUCCESS;
	} else if (strcasecmp(argv[1 + idx_provided], "dump") == 0) {
		if (!IS_ENABLED(CONFIG_CMD_CHARGER_DUMP) ||
		    !chg_chips[chgnum].drv->dump_registers) {
			ccprintf("dump not supported\n");
			return EC_ERROR_PARAM1 + idx_provided;
		}
		ccprintf("Dump %s registers\n",
			 chg_chips[chgnum].drv->get_info(chgnum)->name);
		chg_chips[chgnum].drv->dump_registers(chgnum);
		return EC_SUCCESS;
	} else {
		return EC_ERROR_PARAM1 + idx_provided;
	}
}

DECLARE_CONSOLE_COMMAND(charger, command_charger,
			"[chgnum] [input | current | voltage | dptf] [newval]"
#ifdef CONFIG_CMD_CHARGER_DUMP
			"\n\t[chgnum] dump"
#endif
			,
			"Get or set charger param(s)"
#ifdef CONFIG_CMD_CHARGER_DUMP
			". Dump registers."
#endif
);

/* Driver wrapper functions */

static void charger_chips_init(void)
{
	int chip;

	for (chip = 0; chip < board_get_charger_chip_count(); chip++) {
		if (chg_chips[chip].drv->init)
			chg_chips[chip].drv->init(chip);
	}
}
DECLARE_HOOK(HOOK_INIT, charger_chips_init, HOOK_PRIO_POST_BATTERY_INIT);

enum ec_error_list charger_post_init(void)
{
	int chgnum = 0;

	if (chgnum >= board_get_charger_chip_count()) {
		CPRINTS("%s(%d) Invalid charger!", __func__, chgnum);
		return EC_ERROR_INVAL;
	}

	if (!chg_chips[chgnum].drv->post_init)
		return EC_ERROR_UNIMPLEMENTED;

	return chg_chips[chgnum].drv->post_init(chgnum);
}

const struct charger_info *charger_get_info(void)
{
	int chgnum = 0;

	if (chgnum >= board_get_charger_chip_count()) {
		CPRINTS("%s(%d) Invalid charger!", __func__, chgnum);
		return NULL;
	}

	if (!chg_chips[chgnum].drv->get_info)
		return NULL;

	return chg_chips[chgnum].drv->get_info(chgnum);
}

enum ec_error_list charger_get_status(int *status)
{
	int chgnum = 0;

	if (chgnum >= board_get_charger_chip_count()) {
		CPRINTS("%s(%d) Invalid charger!", __func__, chgnum);
		return EC_ERROR_INVAL;
	}

	if (!chg_chips[chgnum].drv->get_status)
		return EC_ERROR_UNIMPLEMENTED;

	return chg_chips[chgnum].drv->get_status(chgnum, status);
}

enum ec_error_list charger_set_mode(int mode)
{
	int chgnum = 0;

	if (chgnum >= board_get_charger_chip_count()) {
		CPRINTS("%s(%d) Invalid charger!", __func__, chgnum);
		return EC_ERROR_INVAL;
	}

	if (!chg_chips[chgnum].drv->set_mode)
		return EC_ERROR_UNIMPLEMENTED;

	return chg_chips[chgnum].drv->set_mode(chgnum, mode);
}

enum ec_error_list charger_enable_otg_power(int chgnum, int enabled)
{
	if ((chgnum < 0) || (chgnum >= board_get_charger_chip_count())) {
		CPRINTS("%s(%d) Invalid charger!", __func__, chgnum);
		return EC_ERROR_INVAL;
	}

	if (!chg_chips[chgnum].drv->enable_otg_power)
		return EC_ERROR_UNIMPLEMENTED;

	return chg_chips[chgnum].drv->enable_otg_power(chgnum, enabled);
}

enum ec_error_list charger_set_otg_current_voltage(int chgnum,
						   int output_current,
						   int output_voltage)
{
	if ((chgnum < 0) || (chgnum >= board_get_charger_chip_count())) {
		CPRINTS("%s(%d) Invalid charger!", __func__, chgnum);
		return EC_ERROR_INVAL;
	}

	if (!chg_chips[chgnum].drv->set_otg_current_voltage)
		return EC_ERROR_UNIMPLEMENTED;

	return chg_chips[chgnum].drv->set_otg_current_voltage(
		chgnum, output_current, output_voltage);
}

int charger_is_sourcing_otg_power(int port)
{
	int chgnum = 0;

	if (IS_ENABLED(CONFIG_OCPC))
		chgnum = port;

	if ((chgnum < 0) || (chgnum >= board_get_charger_chip_count())) {
		CPRINTS("%s(%d) Invalid charger!", __func__, chgnum);
		return 0;
	}

	if (!chg_chips[chgnum].drv->is_sourcing_otg_power)
		return 0;

	return chg_chips[chgnum].drv->is_sourcing_otg_power(chgnum, port);
}

enum ec_error_list charger_get_actual_current(int chgnum, int *current)
{
	/* Note: chgnum may be -1 if no active port is selected */
	if (chgnum < 0)
		return EC_ERROR_INVAL;

	if (chgnum >= board_get_charger_chip_count()) {
		CPRINTS("%s(%d) Invalid charger!", __func__, chgnum);
		return EC_ERROR_INVAL;
	}

	if (!chg_chips[chgnum].drv->get_actual_current)
		return EC_ERROR_UNIMPLEMENTED;

	return chg_chips[chgnum].drv->get_actual_current(chgnum, current);
}

enum ec_error_list charger_get_current(int chgnum, int *current)
{
	/* Note: chgnum may be -1 if no active port is selected */
	if (chgnum < 0)
		return EC_ERROR_INVAL;

	if (chgnum >= board_get_charger_chip_count()) {
		CPRINTS("%s(%d) Invalid charger!", __func__, chgnum);
		return EC_ERROR_INVAL;
	}

	if (!chg_chips[chgnum].drv->get_current)
		return EC_ERROR_UNIMPLEMENTED;

	return chg_chips[chgnum].drv->get_current(chgnum, current);
}

enum ec_error_list charger_set_current(int chgnum, int current)
{
	if ((chgnum < 0) || (chgnum >= board_get_charger_chip_count())) {
		CPRINTS("%s(%d) Invalid charger!", __func__, chgnum);
		return EC_ERROR_INVAL;
	}

	if (!chg_chips[chgnum].drv->set_current)
		return EC_ERROR_UNIMPLEMENTED;

	return chg_chips[chgnum].drv->set_current(chgnum, current);
}

enum ec_error_list charger_get_actual_voltage(int chgnum, int *voltage)
{
	if (chgnum < 0)
		return EC_ERROR_INVAL;

	if (chgnum >= board_get_charger_chip_count()) {
		CPRINTS("%s(%d) Invalid charger!", __func__, chgnum);
		return EC_ERROR_INVAL;
	}

	if (!chg_chips[chgnum].drv->get_actual_voltage)
		return EC_ERROR_UNIMPLEMENTED;

	return chg_chips[chgnum].drv->get_actual_voltage(chgnum, voltage);
}

enum ec_error_list charger_get_voltage(int chgnum, int *voltage)
{
	if (chgnum < 0)
		return EC_ERROR_INVAL;

	if (chgnum >= board_get_charger_chip_count()) {
		CPRINTS("%s(%d) Invalid charger!", __func__, chgnum);
		return EC_ERROR_INVAL;
	}

	if (!chg_chips[chgnum].drv->get_voltage)
		return EC_ERROR_UNIMPLEMENTED;

	return chg_chips[chgnum].drv->get_voltage(chgnum, voltage);
}

enum ec_error_list charger_set_voltage(int chgnum, int voltage)
{
	if ((chgnum < 0) || (chgnum >= board_get_charger_chip_count())) {
		CPRINTS("%s(%d) Invalid charger!", __func__, chgnum);
		return EC_ERROR_INVAL;
	}

	if (!chg_chips[chgnum].drv->set_voltage)
		return EC_ERROR_UNIMPLEMENTED;

	return chg_chips[chgnum].drv->set_voltage(chgnum, voltage);
}

test_mockable enum ec_error_list charger_discharge_on_ac(int enable)
{
	int chgnum;
	int rv = EC_ERROR_UNIMPLEMENTED;

	if (IS_ENABLED(CONFIG_CHARGER_DISCHARGE_ON_AC_CUSTOM))
		return board_discharge_on_ac(enable);

	/*
	 * When discharge on AC is selected, cycle through all chargers to
	 * enable or disable this feature.
	 */
	for (chgnum = 0; chgnum < board_get_charger_chip_count(); chgnum++) {
		if (chg_chips[chgnum].drv->discharge_on_ac)
			rv = chg_chips[chgnum].drv->discharge_on_ac(chgnum,
								    enable);
	}

	return rv;
}

enum ec_error_list charger_enable_bypass_mode(int chgnum, bool enable)
{
	ASSERT(chgnum >= 0 && chgnum < board_get_charger_chip_count());

	if (!chg_chips[chgnum].drv->enable_bypass_mode)
		return EC_ERROR_UNIMPLEMENTED;
	return chg_chips[chgnum].drv->enable_bypass_mode(chgnum, enable);
}

static int charger_get_valid_chgnum(int port)
{
	int chgnum = 0;

	/* Note: Assumes USBPD port == chgnum on multi-charger systems */
	if (!IS_ENABLED(CONFIG_CHARGER_SINGLE_CHIP))
		chgnum = port;

	if ((chgnum < 0) || (chgnum >= board_get_charger_chip_count())) {
		CPRINTS("%s(%d) Invalid charger!", __func__, chgnum);
		return -1;
	}

	return chgnum;
}

enum ec_error_list charger_get_vbus_voltage(int port, int *voltage)
{
	int chgnum = charger_get_valid_chgnum(port);

	if (chgnum < 0)
		return EC_ERROR_INVAL;

	if (!chg_chips[chgnum].drv->get_vbus_voltage)
		return EC_ERROR_UNIMPLEMENTED;

	return chg_chips[chgnum].drv->get_vbus_voltage(chgnum, port, voltage);
}

enum ec_error_list charger_get_vsys_voltage(int port, int *voltage)
{
	int chgnum = charger_get_valid_chgnum(port);

	if (chgnum < 0)
		return EC_ERROR_INVAL;

	if (!chg_chips[chgnum].drv->get_vsys_voltage)
		return EC_ERROR_UNIMPLEMENTED;

	return chg_chips[chgnum].drv->get_vsys_voltage(chgnum, port, voltage);
}

enum ec_error_list charger_set_input_current_limit(int chgnum,
						   int input_current)
{
	/* Note: may be called with CHARGE_PORT_NONE regularly */
	if (chgnum < 0)
		return EC_ERROR_INVAL;

	if (chgnum >= board_get_charger_chip_count()) {
		CPRINTS("%s(%d) Invalid charger!", __func__, chgnum);
		return EC_ERROR_INVAL;
	}

	if (!chg_chips[chgnum].drv->set_input_current_limit)
		return EC_ERROR_UNIMPLEMENTED;

	return chg_chips[chgnum].drv->set_input_current_limit(chgnum,
							      input_current);
}

enum ec_error_list charger_get_input_current_limit(int chgnum,
						   int *input_current)
{
	/* Note: may be called with CHARGE_PORT_NONE regularly */
	if (chgnum < 0)
		return EC_ERROR_INVAL;

	if (chgnum >= board_get_charger_chip_count()) {
		CPRINTS("%s(%d) Invalid charger!", __func__, chgnum);
		return EC_ERROR_INVAL;
	}

	if (!chg_chips[chgnum].drv->get_input_current_limit)
		return EC_ERROR_UNIMPLEMENTED;

	return chg_chips[chgnum].drv->get_input_current_limit(chgnum,
							      input_current);
}

enum ec_error_list charger_get_input_current(int chgnum, int *input_current)
{
	if (chgnum < 0)
		return EC_ERROR_INVAL;

	if (chgnum >= board_get_charger_chip_count()) {
		CPRINTS("%s(%d) Invalid charger!", __func__, chgnum);
		return EC_ERROR_INVAL;
	}

	if (!chg_chips[chgnum].drv->get_input_current)
		return EC_ERROR_UNIMPLEMENTED;

	return chg_chips[chgnum].drv->get_input_current(chgnum, input_current);
}

enum ec_error_list charger_manufacturer_id(int *id)
{
	int chgnum = 0;

	if (chgnum >= board_get_charger_chip_count()) {
		CPRINTS("%s(%d) Invalid charger!", __func__, chgnum);
		return EC_ERROR_INVAL;
	}

	if (!chg_chips[chgnum].drv->manufacturer_id)
		return EC_ERROR_UNIMPLEMENTED;

	return chg_chips[chgnum].drv->manufacturer_id(chgnum, id);
}

enum ec_error_list charger_device_id(int *id)
{
	int chgnum = 0;

	if (chgnum >= board_get_charger_chip_count()) {
		CPRINTS("%s(%d) Invalid charger!", __func__, chgnum);
		return EC_ERROR_INVAL;
	}

	if (!chg_chips[chgnum].drv->device_id)
		return EC_ERROR_UNIMPLEMENTED;

	return chg_chips[chgnum].drv->device_id(chgnum, id);
}

test_mockable enum ec_error_list charger_set_frequency(int freq_khz)
{
	int chgnum = 0;

	if (chgnum >= board_get_charger_chip_count()) {
		CPRINTS("%s(%d) Invalid charger!", __func__, chgnum);
		return EC_ERROR_INVAL;
	}

	if (!chg_chips[chgnum].drv->set_frequency)
		return EC_ERROR_UNIMPLEMENTED;

	return chg_chips[chgnum].drv->set_frequency(chgnum, freq_khz);
}

enum ec_error_list charger_get_option(int *option)
{
	int chgnum = 0;

	if (chgnum >= board_get_charger_chip_count()) {
		CPRINTS("%s(%d) Invalid charger!", __func__, chgnum);
		return EC_ERROR_INVAL;
	}

	if (!chg_chips[chgnum].drv->get_option)
		return EC_ERROR_UNIMPLEMENTED;

	return chg_chips[chgnum].drv->get_option(chgnum, option);
}

enum ec_error_list charger_set_option(int option)
{
	int chgnum = 0;

	if (chgnum >= board_get_charger_chip_count()) {
		CPRINTS("%s(%d) Invalid charger!", __func__, chgnum);
		return EC_ERROR_INVAL;
	}

	if (!chg_chips[chgnum].drv->set_option)
		return EC_ERROR_UNIMPLEMENTED;

	return chg_chips[chgnum].drv->set_option(chgnum, option);
}

enum ec_error_list charger_set_hw_ramp(int enable)
{
	int chgnum;
	int rv = EC_ERROR_UNIMPLEMENTED;

	for (chgnum = 0; chgnum < board_get_charger_chip_count(); chgnum++) {
		/* Check if the chg chip supports set_hw_ramp. */
		if (chg_chips[chgnum].drv->set_hw_ramp) {
			if (enable) {
				/* Check if this is the active chg chip. */
				if (chgnum == charge_get_active_chg_chip())
					rv = chg_chips[chgnum].drv->set_hw_ramp(
						chgnum, 1);
				/* This is not the active chg chip, disable
				 * hw_ramp. */
				else
					rv = chg_chips[chgnum].drv->set_hw_ramp(
						chgnum, 0);
			} else
				rv = chg_chips[chgnum].drv->set_hw_ramp(chgnum,
									0);
		}
	}

	return rv;
}

#ifdef CONFIG_CHARGE_RAMP_HW
int chg_ramp_is_stable(void)
{
	int chgnum = 0;

	if (chgnum >= board_get_charger_chip_count()) {
		CPRINTS("%s(%d) Invalid charger!", __func__, chgnum);
		return 0;
	}

	if (!chg_chips[chgnum].drv->ramp_is_stable)
		return 0;

	return chg_chips[chgnum].drv->ramp_is_stable(chgnum);
}

int chg_ramp_is_detected(void)
{
	int chgnum = 0;

	if (chgnum >= board_get_charger_chip_count()) {
		CPRINTS("%s(%d) Invalid charger!", __func__, chgnum);
		return 0;
	}

	if (!chg_chips[chgnum].drv->ramp_is_detected)
		return 0;

	return chg_chips[chgnum].drv->ramp_is_detected(chgnum);
}

int chg_ramp_get_current_limit(void)
{
	int chgnum = 0;

	if (chgnum >= board_get_charger_chip_count()) {
		CPRINTS("%s(%d) Invalid charger!", __func__, chgnum);
		return 0;
	}

	if (!chg_chips[chgnum].drv->ramp_get_current_limit)
		return 0;

	return chg_chips[chgnum].drv->ramp_get_current_limit(chgnum);
}
#endif

test_mockable enum ec_error_list
charger_set_vsys_compensation(int chgnum, struct ocpc_data *ocpc,
			      int current_ma, int voltage_mv)
{
	if ((chgnum < 0) || (chgnum >= board_get_charger_chip_count())) {
		CPRINTS("%s(%d) Invalid charger!", __func__, chgnum);
		return EC_ERROR_INVAL;
	}

	/*
	 * This shouldn't happen as this should only be called on chargers
	 * that support this.
	 */
	if (!chg_chips[chgnum].drv->set_vsys_compensation)
		return EC_ERROR_UNIMPLEMENTED;

	return chg_chips[chgnum].drv->set_vsys_compensation(
		chgnum, ocpc, current_ma, voltage_mv);
}

enum ec_error_list charger_is_icl_reached(int chgnum, bool *reached)
{
	if ((chgnum < 0) || (chgnum >= board_get_charger_chip_count())) {
		CPRINTS("%s(%d) Invalid charger!", __func__, chgnum);
		return EC_ERROR_INVAL;
	}

	if (chg_chips[chgnum].drv->is_icl_reached)
		return chg_chips[chgnum].drv->is_icl_reached(chgnum, reached);

	return EC_ERROR_UNIMPLEMENTED;
}

enum ec_error_list charger_enable_linear_charge(int chgnum, bool enable)
{
	if ((chgnum < 0) || (chgnum >= board_get_charger_chip_count())) {
		CPRINTS("%s(%d) Invalid charger!", __func__, chgnum);
		return EC_ERROR_INVAL;
	}

	if (chg_chips[chgnum].drv->enable_linear_charge)
		return chg_chips[chgnum].drv->enable_linear_charge(chgnum,
								   enable);

	return EC_ERROR_UNIMPLEMENTED;
}

#ifdef CONFIG_CHARGER_MIN_BAT_PCT_FOR_POWER_ON
inline int charger_get_min_bat_pct_for_power_on(void)
{
	return CONFIG_CHARGER_MIN_BAT_PCT_FOR_POWER_ON;
}
#endif

enum ec_error_list charger_get_battery_cells(int chgnum, int *cells)
{
	if ((chgnum < 0) || (chgnum >= board_get_charger_chip_count()))
		return EC_ERROR_INVAL;

	if (chg_chips[chgnum].drv->get_battery_cells)
		return chg_chips[chgnum].drv->get_battery_cells(chgnum, cells);

	return EC_ERROR_UNIMPLEMENTED;
}
