/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Battery pack vendor provided charging profile
 */

#include "charge_state.h"
#include "console.h"
#include "ec_commands.h"
#include "util.h"

static const struct battery_info info = {
	/*
	 * Design voltage
	 *   max    = 8.4V
	 *   normal = 7.4V
	 *   min    = 6.0V
	 */
	.voltage_max    = 8400,
	.voltage_normal = 7400,
	.voltage_min    = 6000,

	/* Pre-charge current: I <= 0.01C */
	.precharge_current  = 64, /* mA */

	/*
	 * Operational temperature range
	 *   0 <= T_charge    <= 50 deg C
	 * -20 <= T_discharge <= 60 deg C
	 */
	.start_charging_min_c = 0,
	.start_charging_max_c = 50,
	.charging_min_c       = 0,
	.charging_max_c       = 50,
	.discharging_min_c    = -20,
	.discharging_max_c    = 60,
};

const struct battery_info *battery_get_info(void)
{
	return &info;
}

#ifdef CONFIG_CHARGER_PROFILE_OVERRIDE

/*
 * For Samus, we'd like to set the CONFIG_CHARGER_INPUT_CURRENT to a higher
 * value, but the AC adapters freak out if we do. So instead we set it to a
 * low value, and it gets reset to that point every time AC is applied. Then we
 * bump it up a little bit every time through the loop until it's where we
 * wanted it in the first place.
 */
/* FIXME(crosbug.com/p/24461): determine correct values for this */
#define MAX_INPUT_CURRENT 2048
#define INPUT_CURRENT_INCR 64

static int fast_charging_allowed;

/*
 * This can override the smart battery's charging profile. To make a change,
 * modify one or more of requested_voltage, requested_current, or state.
 * Leave everything else unchanged.
 *
 * Return the next poll period in usec, or zero to use the default (which is
 * state dependent).
 */
int charger_profile_override(struct charge_state_data *curr)
{
	int rv;

	/* We only want to override how we charge, nothing else. */
	if (curr->state != ST_CHARGE)
		return 0;

	/* Bump the input current up a little at a time if needed. */
	if (curr->chg.input_current < MAX_INPUT_CURRENT) {
		rv = charger_set_input_current(curr->chg.input_current +
					       INPUT_CURRENT_INCR);
		/*
		 * If we can't set the input current, indicate the error
		 * (negative, since positive changes the poll period) and
		 * don't override the default behavior.
		 */
		if (rv)
			return -rv;
	}

	/* Do we want to mess with the charge profile too? */
	if (!fast_charging_allowed)
		return 0;

	/* Okay, impose our custom will */
	curr->requested_current = 9000;
	curr->requested_voltage = 8300;
	if (curr->batt.current <= 6300) {
		curr->requested_current = 6300;
		curr->requested_voltage = 8400;
	} else if (curr->batt.current <= 4500) {
		curr->requested_current = 4500;
		curr->requested_voltage = 8500;
	} else if (curr->batt.current <= 2700) {
		curr->requested_current = 2700;
		curr->requested_voltage = 8700;
	} else if (curr->batt.current <= 475) {
		/*
		 * Should we stop? If so, how do we start again?
		 * For now, just use the battery's profile.
		 */
		curr->requested_current = curr->batt.desired_current;
		curr->requested_voltage = curr->batt.desired_voltage;
	}

	return 0;
}

/* Customs options controllable by host command. */
#define PARAM_FASTCHARGE (CS_PARAM_CUSTOM_PROFILE_MIN + 0)

enum ec_status charger_profile_override_get_param(uint32_t param,
						  uint32_t *value)
{
	if (param == PARAM_FASTCHARGE) {
		*value = fast_charging_allowed;
		return EC_RES_SUCCESS;
	}
	return EC_RES_INVALID_PARAM;
}

enum ec_status charger_profile_override_set_param(uint32_t param,
						  uint32_t value)
{
	if (param == PARAM_FASTCHARGE) {
		fast_charging_allowed = value;
		return EC_RES_SUCCESS;
	}
	return EC_RES_INVALID_PARAM;
}

static int command_fastcharge(int argc, char **argv)
{
	if (argc > 1 && !parse_bool(argv[1], &fast_charging_allowed))
		return EC_ERROR_PARAM1;

	ccprintf("fastcharge %s\n", fast_charging_allowed ? "on" : "off");

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(fastcharge, command_fastcharge,
			"[on|off]",
			"Get or set fast charging profile",
			NULL);

#endif	/* CONFIG_CHARGER_PROFILE_OVERRIDE */
