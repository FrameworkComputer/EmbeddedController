/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "charge_state.h"
#include "charger_test.h"

#include <zephyr/ztest.h>

/* Test external variable defined in charge_state_v2 */
extern int charge_prevent_power_on_automatic_power_on;

extern const struct battery_info *batt_info;

struct charge_state_prevent_power_on_fixture {
	struct charge_state_data charge_state_backup;
	const struct battery_info *batt_info;
	int automatic_power_on;
};

static const struct battery_info BATT_INFO = {
	.discharging_max_c = 50,
	.discharging_min_c = 5,
};

static void *setup(void)
{
	static struct charge_state_prevent_power_on_fixture fixture;

	return &fixture;
}

static void before(void *f)
{
	struct charge_state_prevent_power_on_fixture *fixture = f;

	/* Backup the current state */
	fixture->charge_state_backup = *charge_get_status();
	fixture->automatic_power_on =
		charge_prevent_power_on_automatic_power_on;
	fixture->batt_info = batt_info;

	/* Reset the automatic_power_on global */
	charge_prevent_power_on_automatic_power_on = 1;

	/*
	 * Set the battery temperature to a comfortable 20C,
	 * it tolerates 5 to 50 degrees.
	 */
	batt_info = &BATT_INFO;
	charge_get_status()->batt.temperature = 2931;
}

static void after(void *f)
{
	struct charge_state_prevent_power_on_fixture *fixture = f;

	/* Restore the state from 'before' */
	*charge_get_status() = fixture->charge_state_backup;
	charge_prevent_power_on_automatic_power_on =
		fixture->automatic_power_on;
	batt_info = fixture->batt_info;
}

ZTEST_SUITE(charge_state_prevent_power_on, charger_predicate_post_main, setup,
	    before, after, NULL);

ZTEST(charge_state_prevent_power_on, test_allow_power_on)
{
	struct batt_params *params = &charge_get_status()->batt;

	/* Force a call to refresh the battery parameters */
	params->is_present = BP_NOT_SURE;
	/* Set the charge state to be high enough */
	params->state_of_charge =
		CONFIG_PLATFORM_EC_CHARGER_MIN_BAT_PCT_FOR_POWER_ON;

	/* Verify that we can power on when the power button was pressed */
	zassert_false(charge_prevent_power_on(true));
}

ZTEST(charge_state_prevent_power_on, test_low_charge)
{
	struct batt_params *params = &charge_get_status()->batt;

	/* Force a low charge state */
	params->state_of_charge =
		CONFIG_PLATFORM_EC_CHARGER_MIN_BAT_PCT_FOR_POWER_ON - 1;

	/* Verify that we cannot power on during an automatic power-on */
	zassert_true(charge_prevent_power_on(false));
}

ZTEST(charge_state_prevent_power_on, test_consuming_full_input_current)
{
	struct batt_params *params = &charge_get_status()->batt;

	params->state_of_charge = 50;
	zassert_true(charge_is_consuming_full_input_current());

	params->state_of_charge = 0;
	zassert_false(charge_is_consuming_full_input_current());

	params->state_of_charge = 100;
	zassert_false(charge_is_consuming_full_input_current());
}

ZTEST(charge_state_prevent_power_on, test_extreme_temperature)
{
	struct batt_params *params = &charge_get_status()->batt;

	/* Very hot, not safe to use. */
	params->temperature = 3500;
	/* Automatic and user-requested power-on are both blocked. */
	zassert_true(charge_prevent_power_on(true));
	zassert_true(charge_prevent_power_on(false));

	/* Below freezing, the battery won't operate well. */
	params->temperature = 2700;
	zassert_true(charge_prevent_power_on(true));
	zassert_true(charge_prevent_power_on(false));
}

ZTEST(charge_state_prevent_power_on, test_no_battery_insufficient_charger)
{
	struct batt_params *params = &charge_get_status()->batt;

	/* Set battery to not connected */
	params->is_present = false;

	/* Remove global power on prevention */
	charge_prevent_power_on_automatic_power_on = 0;

	/* since there is no battery and 0mW power on should be prevented */
	zassert_true(charge_prevent_power_on(true));
	zassert_true(charge_prevent_power_on(false));
}
