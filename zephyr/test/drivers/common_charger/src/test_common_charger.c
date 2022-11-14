/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "charge_ramp.h"
#include "driver/charger/isl923x_public.h"
#include "ec_commands.h"
#include "host_command.h"
#include "test/drivers/charger_utils.h"
#include "test/drivers/test_mocks.h"
#include "test/drivers/test_state.h"

#include <zephyr/kernel.h>
#include <zephyr/ztest.h>
#include <zephyr/ztest_assert.h>

/* Tested wrt isl923x without RAA489000 */

/* Only single charger-chip configured for the drivers overlay */
#define CHG_NUM get_charger_num(&isl923x_drv)

/*
 * Only faking functions that return an essentially constant value no chip
 * register reading and thus no emulator reading.
 */
FAKE_VALUE_FUNC(int, isl923x_ramp_is_stable, int);
FAKE_VALUE_FUNC(int, isl923x_ramp_is_detected, int);

ZTEST(common_charger, test_chg_ramp_is_stable)
{
	isl923x_ramp_is_stable_fake.return_val = 1;

	zassert_equal(chg_ramp_is_stable(),
		      isl923x_ramp_is_stable_fake.return_val);
	zassert_equal(isl923x_ramp_is_stable_fake.call_count, 1);
	zassert_equal(isl923x_ramp_is_stable_fake.arg0_val, CHG_NUM);
}

ZTEST(common_charger, test_chg_ramp_is_detected)
{
	isl923x_ramp_is_stable_fake.return_val = 0;

	zassert_equal(chg_ramp_is_detected(),
		      isl923x_ramp_is_detected_fake.return_val);
	zassert_equal(isl923x_ramp_is_detected_fake.call_count, 1);
	zassert_equal(isl923x_ramp_is_detected_fake.arg0_val, CHG_NUM);
}

ZTEST(common_charger, test_chg_ramp_get_current_limit)
{
	zassert_equal(chg_ramp_get_current_limit(),
		      CONFIG_CHARGER_INPUT_CURRENT);
}

ZTEST(common_charger, test_charger_get_min_bat_pct_for_power_on)
{
	zassert_equal(charger_get_min_bat_pct_for_power_on(),
		      CONFIG_CHARGER_MIN_BAT_PCT_FOR_POWER_ON);
}

ZTEST(common_charger, test_charger_set_vsys_compensation__bad_arg)
{
	/* Not supported without RAA489000 */
	struct ocpc_data unused = { 0 };
	/* All arguments but 0th are unused. */
	zassert_equal(charger_set_vsys_compensation(INT_MAX, &unused, 0, 0),
		      EC_ERROR_INVAL);
}

ZTEST(common_charger, test_charger_set_vsys_compensation__unsupported)
{
	/* Not supported without RAA489000 */
	struct ocpc_data unused = { 0 };
	/* All arguments but 0th are unused. */
	zassert_equal(charger_set_vsys_compensation(CHG_NUM, &unused, 0, 0),
		      EC_ERROR_UNIMPLEMENTED);
}

ZTEST(common_charger, test_charger_is_icl_reached__bad_arg)
{
	bool unused = false;

	zassert_equal(charger_is_icl_reached(INT_MAX, &unused), EC_ERROR_INVAL);
	zassert_equal(charger_is_icl_reached(INT_MIN, &unused), EC_ERROR_INVAL);
}

ZTEST(common_charger, test_charger_is_icl_reached__unsupported)
{
	/* Not supported by isl923x */
	bool unused;

	zassert_equal(charger_is_icl_reached(CHG_NUM, &unused),
		      EC_ERROR_UNIMPLEMENTED);
}

ZTEST(common_charger, test_charger_enable_linear_charge__bad_arg)
{
	/* Not supported without RAA489000 */
	/* All arguments but 0th are unused. */
	zassert_equal(charger_enable_linear_charge(INT_MAX, false),
		      EC_ERROR_INVAL);
}

ZTEST(common_charger, test_charger_enable_linear_charge__unsupported)
{
	/* Not supported without RAA489000 */
	/* All arguments but 0th are unused. */
	zassert_equal(charger_enable_linear_charge(CHG_NUM, false),
		      EC_ERROR_UNIMPLEMENTED);
}

ZTEST(common_charger, test_charger_get_battery_cells__bad_arg)
{
	/* Not supported by isl923x */
	/* All arguments but 0th are unused. */
	int unused;

	zassert_equal(charger_get_battery_cells(INT_MAX, &unused),
		      EC_ERROR_INVAL);
}

ZTEST(common_charger, test_charger_get_battery_cells__unsupported)
{
	/* Not supported by isl923x */
	/* All arguments but 0th are unused. */
	int unused;

	zassert_equal(charger_get_battery_cells(CHG_NUM, &unused),
		      EC_ERROR_UNIMPLEMENTED);
}

static void suite_common_charger_before_after(void *test_data)
{
	ARG_UNUSED(test_data);

	RESET_FAKE(isl923x_ramp_is_stable);
	RESET_FAKE(isl923x_ramp_is_detected);
	/* Driver's default hard-coded value */
	isl923x_ramp_is_detected_fake.return_val = 1;

	isl923x_drv.init(CHG_NUM);
}

ZTEST_SUITE(common_charger, drivers_predicate_post_main, NULL,
	    suite_common_charger_before_after, NULL, NULL);
