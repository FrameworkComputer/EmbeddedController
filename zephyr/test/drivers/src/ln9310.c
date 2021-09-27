/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <ztest.h>
#include <drivers/emul.h>
#include <kernel.h>
#include <ztest_assert.h>

#include "driver/ln9310.h"
#include "emul/emul_ln9310.h"

/*
 * TODO(b/201420132): Implement approach for tests to immediately schedule work
 * to avoid any sleeping
 */
#define TEST_DELAY_MS 50

static void test_ln9310_2s_powers_up(void)
{
	const struct emul *emulator =
		emul_get_binding(DT_LABEL(DT_NODELABEL(ln9310)));

	zassert_not_null(emulator, NULL);

	ln9310_emul_set_context(emulator);
	ln9310_emul_reset(emulator);
	ln9310_emul_set_battery_cell_type(emulator, BATTERY_CELL_TYPE_2S);
	ln9310_emul_set_version(emulator, LN9310_BC_STS_C_CHIP_REV_FIXED);

	zassert_ok(ln9310_init(), NULL);
	zassert_true(ln9310_emul_is_init(emulator), NULL);

	k_msleep(TEST_DELAY_MS);

	zassert_true(ln9310_power_good(), NULL);
}

static void test_ln9310_3s_powers_up(void)
{
	const struct emul *emulator =
		emul_get_binding(DT_LABEL(DT_NODELABEL(ln9310)));

	zassert_not_null(emulator, NULL);

	ln9310_emul_set_context(emulator);
	ln9310_emul_reset(emulator);
	ln9310_emul_set_battery_cell_type(emulator, BATTERY_CELL_TYPE_3S);
	ln9310_emul_set_version(emulator, LN9310_BC_STS_C_CHIP_REV_FIXED);

	zassert_ok(ln9310_init(), NULL);
	zassert_true(ln9310_emul_is_init(emulator), NULL);

	k_msleep(TEST_DELAY_MS);

	zassert_true(ln9310_power_good(), NULL);
}

static void reset_ln9310_state(void)
{
	ln9310_reset_to_initial_state();
}

void test_suite_ln9310(void)
{
	ztest_test_suite(
		ln9310,
		ztest_unit_test_setup_teardown(test_ln9310_2s_powers_up,
					       reset_ln9310_state,
					       reset_ln9310_state),
		ztest_unit_test_setup_teardown(test_ln9310_3s_powers_up,
					       reset_ln9310_state,
					       reset_ln9310_state));
	ztest_run_test_suite(ln9310);
}
