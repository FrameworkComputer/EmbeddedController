/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "battery.h"
#include "test/drivers/test_state.h"

#include <stdbool.h>

#include <zephyr/drivers/emul.h>
#include <zephyr/drivers/emul_fuel_gauge.h>
#include <zephyr/drivers/fuel_gauge.h>
#include <zephyr/ztest.h>
#include <zephyr/ztest_assert.h>

/* TODO(b271889974/) Use default_battery label */
#define BATT_EMUL EMUL_DT_GET(DT_NODELABEL(upstream_battery))

ZTEST(upstream_fuel_gauge, test_battery_get_params__success)
{
	const struct emul *sbs_gauge = BATT_EMUL;

	emul_fuel_gauge_set_battery_charging(sbs_gauge, 5000 * 1000,
					     3000 * 1000);

	struct batt_params ret_params = { 0 };

	battery_get_params(&ret_params);

	zassert_equal(ret_params.voltage, 5000);
	zassert_equal(ret_params.current, 3000);
}

ZTEST(upstream_fuel_gauge, test_battery_cutoff)
{
	const struct emul *sbs_gauge = BATT_EMUL;
	bool was_cutoff;

	board_cut_off_battery();

	emul_fuel_gauge_is_battery_cutoff(sbs_gauge, &was_cutoff);
	zassert_true(was_cutoff);
}

ZTEST_SUITE(upstream_fuel_gauge, drivers_predicate_post_main, NULL, NULL, NULL,
	    NULL);
