/* Copyright 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Common functions for battery charging.
 */

#include "battery_smart.h"
#include "charge_state_v2.h"
#include "charger.h"
#include "common.h"
#include "console.h"
#include "dptf.h"
#include "host_command.h"
#include "printf.h"
#include "util.h"
#include "hooks.h"

/* Console output macros */
#define CPUTS(outstr) cputs(CC_CHARGER, outstr)
#define CPRINTS(format, args...) cprints(CC_CHARGER, format, ## args)

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
	const struct charger_info * const info = charger_get_info();

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

	if (IS_ENABLED(CONFIG_OCPC))
		chgnum = charge_get_active_chg_chip();

	memset(chg, 0, sizeof(*chg));

	/*
	 * Only the primary charger(0) can tightly regulate the current,
	 * therefore always query the primary charger.
	 */
	if (charger_get_current(0, &chg->current))
		chg->flags |= CHG_FLAG_BAD_CURRENT;

	if (charger_get_voltage(chgnum, &chg->voltage))
		chg->flags |= CHG_FLAG_BAD_VOLTAGE;

	if (charger_get_input_current(chgnum, &chg->input_current))
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
	ccputs(rv == EC_ERROR_UNIMPLEMENTED ? "(unsupported)\n" : "(error)\n");
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
		ccprintf("%pb (0x%04x)\n", BINARY_VALUE(d, 16), d);

	/* manufacturer id */
	print_item_name("Man id:");
	if (check_print_error(charger_manufacturer_id(&d)))
		ccprintf("0x%04x\n", d);

	/* device id */
	print_item_name("Dev id:");
	if (check_print_error(charger_device_id(&d)))
		ccprintf("0x%04x\n", d);

	/* charge voltage limit */
	print_item_name("V_batt:");
	if (check_print_error(charger_get_voltage(chgnum, &d)))
		ccprintf("%5d (%4d - %5d, %3d)\n", d, info->voltage_min,
			 info->voltage_max, info->voltage_step);

	/* charge current limit */
	print_item_name("I_batt:");
	if (check_print_error(charger_get_current(chgnum, &d)))
		ccprintf("%5d (%4d - %5d, %3d)\n", d, info->current_min,
			 info->current_max, info->current_step);

	/* input current limit */
	print_item_name("I_in:");
	if (check_print_error(charger_get_input_current(chgnum, &d)))
		ccprintf("%5d (%4d - %5d, %3d)\n", d, info->input_current_min,
			 info->input_current_max, info->input_current_step);

	/* dptf current limit */
	print_item_name("I_dptf:");
	if (dptf_limit_ma >= 0)
		ccprintf("%5d\n", dptf_limit_ma);
	else
		ccputs("disabled\n");
}

static int command_charger(int argc, char **argv)
{
	int d;
	char *e;
	int idx_provided = 0;
	int chgnum;

	if (argc == 1) {
		print_charger_debug(0);
		return EC_SUCCESS;
	}

	idx_provided = isdigit(argv[1][0]);
	if (idx_provided)
		chgnum = atoi(argv[1]);
	else
		chgnum = 0;

	if ((argc == 2) && idx_provided) {
		print_charger_debug(chgnum);
		return EC_SUCCESS;
	}

	if (strcasecmp(argv[1+idx_provided], "input") == 0) {
		d = strtoi(argv[2+idx_provided], &e, 0);
		if (*e)
			return EC_ERROR_PARAM2+idx_provided;
		return charger_set_input_current(chgnum, d);
	} else if (strcasecmp(argv[1+idx_provided], "current") == 0) {
		d = strtoi(argv[2+idx_provided], &e, 0);
		if (*e)
			return EC_ERROR_PARAM2+idx_provided;
		chgstate_set_manual_current(d);
		return charger_set_current(chgnum, d);
	} else if (strcasecmp(argv[1+idx_provided], "voltage") == 0) {
		d = strtoi(argv[2+idx_provided], &e, 0);
		if (*e)
			return EC_ERROR_PARAM2+idx_provided;
		chgstate_set_manual_voltage(d);
		return charger_set_voltage(chgnum, d);
	} else if (strcasecmp(argv[1+idx_provided], "dptf") == 0) {
		d = strtoi(argv[2+idx_provided], &e, 0);
		if (*e)
			return EC_ERROR_PARAM2+idx_provided;
		dptf_limit_ma = d;
		return EC_SUCCESS;
	} else {
		return EC_ERROR_PARAM1+idx_provided;
	}
}

DECLARE_CONSOLE_COMMAND(charger, command_charger,
			"[chgnum] [input | current | voltage | dptf] [newval]",
			"Get or set charger param(s)");

/* Driver wrapper functions */
#ifndef CONFIG_CHARGER_CUSTOMER_SETTING
static void charger_chips_init(void)
{
	int chip;

	for (chip = 0; chip < board_get_charger_chip_count(); chip++) {
		if (chg_chips[chip].drv->init)
			chg_chips[chip].drv->init(chip);
	}
}
DECLARE_HOOK(HOOK_INIT, charger_chips_init, HOOK_PRIO_INIT_I2C + 1);
#endif

enum ec_error_list charger_post_init(void)
{
	int chgnum = 0;
	int rv = EC_ERROR_UNIMPLEMENTED;

	if ((chgnum < 0) || (chgnum >= board_get_charger_chip_count())) {
		CPRINTS("%s(%d) Invalid charger!", __func__, chgnum);
		return EC_ERROR_INVAL;
	}

	if (chg_chips[chgnum].drv->post_init)
		rv = chg_chips[chgnum].drv->post_init(chgnum);

	return rv;
}

const struct charger_info *charger_get_info(void)
{
	int chgnum = 0;
	const struct charger_info *ret = NULL;

	if ((chgnum < 0) || (chgnum >= board_get_charger_chip_count())) {
		CPRINTS("%s(%d) Invalid charger!", __func__, chgnum);
		return NULL;
	}

	if (chg_chips[chgnum].drv->get_info)
		ret = chg_chips[chgnum].drv->get_info(chgnum);

	return ret;
}

enum ec_error_list charger_get_status(int *status)
{
	int chgnum = 0;
	int rv = EC_ERROR_UNIMPLEMENTED;

	if ((chgnum < 0) || (chgnum >= board_get_charger_chip_count())) {
		CPRINTS("%s(%d) Invalid charger!", __func__, chgnum);
		return EC_ERROR_INVAL;
	}

	if (chg_chips[chgnum].drv->get_status)
		rv = chg_chips[chgnum].drv->get_status(chgnum, status);

	return rv;
}

enum ec_error_list charger_set_mode(int mode)
{
	int chgnum = 0;
	int rv = EC_ERROR_UNIMPLEMENTED;

	if ((chgnum < 0) || (chgnum >= board_get_charger_chip_count())) {
		CPRINTS("%s(%d) Invalid charger!", __func__, chgnum);
		return EC_ERROR_INVAL;
	}

	if (chg_chips[chgnum].drv->set_mode)
		rv = chg_chips[chgnum].drv->set_mode(chgnum, mode);

	return rv;
}

enum ec_error_list charger_enable_otg_power(int chgnum, int enabled)
{
	int rv = EC_ERROR_UNIMPLEMENTED;

	if ((chgnum < 0) || (chgnum >= board_get_charger_chip_count())) {
		CPRINTS("%s(%d) Invalid charger!", __func__, chgnum);
		return EC_ERROR_INVAL;
	}

	if (chg_chips[chgnum].drv->enable_otg_power)
		rv = chg_chips[chgnum].drv->enable_otg_power(chgnum, enabled);

	return rv;
}

enum ec_error_list charger_set_otg_current_voltage(int chgnum,
						   int output_current,
						   int output_voltage)
{
	int rv = EC_ERROR_UNIMPLEMENTED;

	if ((chgnum < 0) || (chgnum >= board_get_charger_chip_count())) {
		CPRINTS("%s(%d) Invalid charger!", __func__, chgnum);
		return EC_ERROR_INVAL;
	}

	if (chg_chips[chgnum].drv->set_otg_current_voltage)
		rv = chg_chips[chgnum].drv->set_otg_current_voltage(chgnum,
						output_current, output_voltage);

	return rv;
}

int charger_is_sourcing_otg_power(int port)
{
	int chgnum = 0;
	int rv = 0;

	if (IS_ENABLED(CONFIG_OCPC))
		chgnum = port;

	if ((chgnum < 0) || (chgnum >= board_get_charger_chip_count())) {
		CPRINTS("%s(%d) Invalid charger!", __func__, chgnum);
		return 0;
	}

	if (chg_chips[chgnum].drv->is_sourcing_otg_power)
		rv = chg_chips[chgnum].drv->is_sourcing_otg_power(chgnum, port);

	return rv;
}

enum ec_error_list charger_get_current(int chgnum, int *current)
{
	int rv = EC_ERROR_UNIMPLEMENTED;

	/* Note: chgnum may be -1 if no active port is selected */
	if (chgnum < 0)
		return EC_ERROR_INVAL;

	if (chgnum >= board_get_charger_chip_count()) {
		CPRINTS("%s(%d) Invalid charger!", __func__, chgnum);
		return EC_ERROR_INVAL;
	}

	if (chg_chips[chgnum].drv->get_current)
		rv = chg_chips[chgnum].drv->get_current(chgnum, current);

	return rv;
}

enum ec_error_list charger_set_current(int chgnum, int current)
{
	int rv = EC_ERROR_UNIMPLEMENTED;

	if ((chgnum < 0) || (chgnum >= board_get_charger_chip_count())) {
		CPRINTS("%s(%d) Invalid charger!", __func__, chgnum);
		return EC_ERROR_INVAL;
	}

	if (chg_chips[chgnum].drv->set_current)
		rv = chg_chips[chgnum].drv->set_current(chgnum, current);

	return rv;
}

enum ec_error_list charger_get_voltage(int chgnum, int *voltage)
{
	int rv = EC_ERROR_UNIMPLEMENTED;

	if (chgnum < 0)
		return EC_ERROR_INVAL;

	if (chgnum >= board_get_charger_chip_count()) {
		CPRINTS("%s(%d) Invalid charger!", __func__, chgnum);
		return EC_ERROR_INVAL;
	}

	if (chg_chips[chgnum].drv->get_voltage)
		rv = chg_chips[chgnum].drv->get_voltage(chgnum, voltage);

	return rv;
}

enum ec_error_list charger_set_voltage(int chgnum, int voltage)
{
	int rv = EC_ERROR_UNIMPLEMENTED;

	if ((chgnum < 0) || (chgnum >= board_get_charger_chip_count())) {
		CPRINTS("%s(%d) Invalid charger!", __func__, chgnum);
		return EC_ERROR_INVAL;
	}

	if (chg_chips[chgnum].drv->set_voltage)
		rv = chg_chips[chgnum].drv->set_voltage(chgnum, voltage);

	return rv;
}

enum ec_error_list charger_discharge_on_ac(int enable)
{
	int chgnum;
	int rv = EC_ERROR_UNIMPLEMENTED;

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

enum ec_error_list charger_get_vbus_voltage(int port, int *voltage)
{
	int chgnum = 0;
	int rv = 0;

	/* Note: Assumes USBPD port == chgnum on multi-charger systems */
	if (!IS_ENABLED(CONFIG_CHARGER_SINGLE_CHIP))
		chgnum = port;

	if ((chgnum < 0) || (chgnum >= board_get_charger_chip_count())) {
		CPRINTS("%s(%d) Invalid charger!", __func__, chgnum);
		return 0;
	}

	if (chg_chips[chgnum].drv->get_vbus_voltage)
		rv = chg_chips[chgnum].drv->get_vbus_voltage(chgnum, port,
							     voltage);

	return rv;
}

enum ec_error_list charger_set_input_current(int chgnum, int input_current)
{
	int rv = EC_ERROR_UNIMPLEMENTED;

	if ((chgnum < 0) || (chgnum >= board_get_charger_chip_count())) {
		CPRINTS("%s(%d) Invalid charger!", __func__, chgnum);
		return EC_ERROR_INVAL;
	}

	if (chg_chips[chgnum].drv->set_input_current)
		rv = chg_chips[chgnum].drv->set_input_current(chgnum,
							      input_current);

	return rv;
}

enum ec_error_list charger_get_input_current(int chgnum, int *input_current)
{
	int rv = EC_ERROR_UNIMPLEMENTED;

	if (chgnum < 0)
		return EC_ERROR_INVAL;

	if (chgnum >= board_get_charger_chip_count()) {
		CPRINTS("%s(%d) Invalid charger!", __func__, chgnum);
		return EC_ERROR_INVAL;
	}

	if (chg_chips[chgnum].drv->get_input_current)
		rv = chg_chips[chgnum].drv->get_input_current(chgnum,
							      input_current);

	return rv;
}

enum ec_error_list charger_manufacturer_id(int *id)
{
	int chgnum = 0;
	int rv = EC_ERROR_UNIMPLEMENTED;

	if ((chgnum < 0) || (chgnum >= board_get_charger_chip_count())) {
		CPRINTS("%s(%d) Invalid charger!", __func__, chgnum);
		return EC_ERROR_INVAL;
	}

	if (chg_chips[chgnum].drv->manufacturer_id)
		rv = chg_chips[chgnum].drv->manufacturer_id(chgnum, id);

	return rv;
}

enum ec_error_list charger_device_id(int *id)
{
	int chgnum = 0;
	int rv = EC_ERROR_UNIMPLEMENTED;

	if ((chgnum < 0) || (chgnum >= board_get_charger_chip_count())) {
		CPRINTS("%s(%d) Invalid charger!", __func__, chgnum);
		return EC_ERROR_INVAL;
	}

	if (chg_chips[chgnum].drv->device_id)
		rv = chg_chips[chgnum].drv->device_id(chgnum, id);

	return rv;
}

enum ec_error_list charger_get_option(int *option)
{
	int chgnum = 0;
	int rv = EC_ERROR_UNIMPLEMENTED;

	if ((chgnum < 0) || (chgnum >= board_get_charger_chip_count())) {
		CPRINTS("%s(%d) Invalid charger!", __func__, chgnum);
		return EC_ERROR_INVAL;
	}

	if (chg_chips[chgnum].drv->get_option)
		rv = chg_chips[chgnum].drv->get_option(chgnum, option);

	return rv;
}

enum ec_error_list charger_set_option(int option)
{
	int chgnum = 0;
	int rv = EC_ERROR_UNIMPLEMENTED;

	if ((chgnum < 0) || (chgnum >= board_get_charger_chip_count())) {
		CPRINTS("%s(%d) Invalid charger!", __func__, chgnum);
		return EC_ERROR_INVAL;
	}

	if (chg_chips[chgnum].drv->set_option)
		rv = chg_chips[chgnum].drv->set_option(chgnum, option);

	return rv;
}

enum ec_error_list charger_set_hw_ramp(int enable)
{
	int chgnum = 0;
	int rv = EC_ERROR_UNIMPLEMENTED;

	if ((chgnum < 0) || (chgnum >= board_get_charger_chip_count())) {
		CPRINTS("%s(%d) Invalid charger!", __func__, chgnum);
		return EC_ERROR_INVAL;
	}

	if (chg_chips[chgnum].drv->set_hw_ramp)
		rv = chg_chips[chgnum].drv->set_hw_ramp(chgnum, enable);

	return rv;
}

#ifdef CONFIG_CHARGE_RAMP_HW
int chg_ramp_is_stable(void)
{
	int chgnum = 0;
	int rv = 0;

	if ((chgnum < 0) || (chgnum >= board_get_charger_chip_count())) {
		CPRINTS("%s(%d) Invalid charger!", __func__, chgnum);
		return 0;
	}

	if (chg_chips[chgnum].drv->ramp_is_stable)
		rv = chg_chips[chgnum].drv->ramp_is_stable(chgnum);

	return rv;
}

int chg_ramp_is_detected(void)
{
	int chgnum = 0;
	int rv = 0;

	if ((chgnum < 0) || (chgnum >= board_get_charger_chip_count())) {
		CPRINTS("%s(%d) Invalid charger!", __func__, chgnum);
		return 0;
	}

	if (chg_chips[chgnum].drv->ramp_is_detected)
		rv = chg_chips[chgnum].drv->ramp_is_detected(chgnum);

	return rv;
}

int chg_ramp_get_current_limit(void)
{
	int chgnum = 0;
	int rv = 0;

	if ((chgnum < 0) || (chgnum >= board_get_charger_chip_count())) {
		CPRINTS("%s(%d) Invalid charger!", __func__, chgnum);
		return 0;
	}

	if (chg_chips[chgnum].drv->ramp_get_current_limit)
		rv = chg_chips[chgnum].drv->ramp_get_current_limit(chgnum);

	return rv;
}
#endif

enum ec_error_list charger_set_vsys_compensation(int chgnum,
						 struct ocpc_data *ocpc,
						 int current_ma,
						 int voltage_mv)
{
	if ((chgnum < 0) || (chgnum >= board_get_charger_chip_count())) {
		CPRINTS("%s(%d) Invalid charger!", __func__, chgnum);
		return EC_ERROR_INVAL;
	}

	if (chg_chips[chgnum].drv->set_vsys_compensation)
		return chg_chips[chgnum].drv->set_vsys_compensation(chgnum,
								    ocpc,
								    current_ma,
								    voltage_mv);

	/*
	 * This shouldn't happen as this should only be called on chargers
	 * that support this.
	 */
	return EC_ERROR_UNIMPLEMENTED;
}
