/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "charge_state.h"

#include <zephyr/ztest.h>

/* Test external variable defined in charge_state_v2 */
extern int charge_prevent_power_on_automatic_power_on;

struct charge_state_prevent_power_on_fixture {
	struct charge_state_data charge_state_backup;
	int automatic_power_on;
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

	/* Reset the automatic_power_on global */
	charge_prevent_power_on_automatic_power_on = 1;
}

static void after(void *f)
{
	struct charge_state_prevent_power_on_fixture *fixture = f;

	/* Restore the state from 'before' */
	*charge_get_status() = fixture->charge_state_backup;
	charge_prevent_power_on_automatic_power_on =
		fixture->automatic_power_on;
}

ZTEST_SUITE(charge_state_prevent_power_on, NULL, setup, before, after, NULL);

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
