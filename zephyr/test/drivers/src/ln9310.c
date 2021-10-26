/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <ztest.h>
#include <drivers/emul.h>
#include <kernel.h>
#include <ztest_assert.h>
#include <drivers/i2c_emul.h>

#include "driver/ln9310.h"
#include "emul/emul_ln9310.h"
#include "emul/emul_common_i2c.h"

/*
 * TODO(b/201420132): Implement approach for tests to immediately schedule work
 * to avoid any sleeping
 */
#define TEST_DELAY_MS 50

/*
 * Chip revisions below LN9310_BC_STS_C_CHIP_REV_FIXED require an alternative
 * software startup to properly initialize and power up.
 */
#define REQUIRES_CFLY_PRECHARGE_STARTUP_CHIP_REV \
	(LN9310_BC_STS_C_CHIP_REV_FIXED - 1)

static void test_ln9310_read_chip_fails(void)
{
	const struct emul *emulator =
		emul_get_binding(DT_LABEL(DT_NODELABEL(ln9310)));
	struct i2c_emul *i2c_emul = ln9310_emul_get_i2c_emul(emulator);

	zassert_not_null(emulator, NULL);
	zassert_not_null(i2c_emul, NULL);

	ln9310_emul_set_context(emulator);
	ln9310_emul_reset(emulator);
	/* Battery and chip rev won't matter here so only testing one pair */
	ln9310_emul_set_battery_cell_type(emulator, BATTERY_CELL_TYPE_2S);
	ln9310_emul_set_version(emulator, LN9310_BC_STS_C_CHIP_REV_FIXED);

	i2c_common_emul_set_read_fail_reg(i2c_emul, LN9310_REG_BC_STS_C);

	zassert_ok(!ln9310_init(), NULL);
	zassert_false(ln9310_emul_is_init(emulator), NULL);

	/* TODO(b/201420132) */
	k_msleep(TEST_DELAY_MS);
	zassert_false(ln9310_power_good(), NULL);

	i2c_common_emul_set_read_fail_reg(i2c_emul,
					  I2C_COMMON_EMUL_NO_FAIL_REG);
}

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

	/* TODO(b/201420132) */
	k_msleep(TEST_DELAY_MS);
	zassert_false(ln9310_power_good(), NULL);

	ln9310_software_enable(1);

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

	/* TODO(b/201420132) */
	k_msleep(TEST_DELAY_MS);
	zassert_false(ln9310_power_good(), NULL);

	ln9310_software_enable(1);

	k_msleep(TEST_DELAY_MS);
	zassert_true(ln9310_power_good(), NULL);
}

struct startup_workaround_data {
	bool startup_workaround_attempted;
};

static int mock_write_fn_intercept_startup_workaround(struct i2c_emul *emul,
						      int reg, uint8_t val,
						      int bytes, void *data)
{
	struct startup_workaround_data *test_data = data;

	uint8_t startup_workaround_val =
		(LN9310_TEST_MODE_CTRL_FORCE_SC_OUT_PRECHARGE_ON |
		 LN9310_TEST_MODE_CTRL_FORCE_SC_OUT_PREDISCHARGE_ON);

	test_data->startup_workaround_attempted =
		test_data->startup_workaround_attempted ||
		((reg == LN9310_REG_TEST_MODE_CTRL) &&
		 (val == startup_workaround_val));

	return 1;
}

static void test_ln9310_2s_cfly_precharge_startup(void)
{
	const struct emul *emulator =
		emul_get_binding(DT_LABEL(DT_NODELABEL(ln9310)));

	struct i2c_emul *emul = ln9310_emul_get_i2c_emul(emulator);

	struct startup_workaround_data test_data = {
		.startup_workaround_attempted = false,
	};

	zassert_not_null(emulator, NULL);

	ln9310_emul_set_context(emulator);
	ln9310_emul_reset(emulator);
	ln9310_emul_set_battery_cell_type(emulator, BATTERY_CELL_TYPE_2S);
	ln9310_emul_set_version(emulator,
				REQUIRES_CFLY_PRECHARGE_STARTUP_CHIP_REV);

	zassert_ok(ln9310_init(), NULL);
	zassert_true(ln9310_emul_is_init(emulator), NULL);

	/* TODO(b/201420132) */
	k_msleep(TEST_DELAY_MS);
	zassert_false(ln9310_power_good(), NULL);

	i2c_common_emul_set_write_func(
		emul, &mock_write_fn_intercept_startup_workaround, &test_data);

	ln9310_software_enable(1);
	zassert_true(test_data.startup_workaround_attempted, NULL);

	/* TODO(b/201420132) */
	k_msleep(TEST_DELAY_MS);
	zassert_true(ln9310_power_good(), NULL);

	ln9310_software_enable(0);

	/* TODO(b/201420132) */
	k_msleep(TEST_DELAY_MS);
	zassert_false(ln9310_power_good(), NULL);

	i2c_common_emul_set_write_func(emul, NULL, NULL);
}

static void test_ln9310_3s_cfly_precharge_startup(void)
{
	const struct emul *emulator =
		emul_get_binding(DT_LABEL(DT_NODELABEL(ln9310)));
	struct i2c_emul *emul = ln9310_emul_get_i2c_emul(emulator);

	struct startup_workaround_data test_data = {
		.startup_workaround_attempted = false,
	};

	zassert_not_null(emulator, NULL);

	ln9310_emul_set_context(emulator);
	ln9310_emul_reset(emulator);
	ln9310_emul_set_battery_cell_type(emulator, BATTERY_CELL_TYPE_3S);
	ln9310_emul_set_version(emulator,
				REQUIRES_CFLY_PRECHARGE_STARTUP_CHIP_REV);

	zassert_ok(ln9310_init(), NULL);
	zassert_true(ln9310_emul_is_init(emulator), NULL);

	/* TODO(b/201420132) */
	k_msleep(TEST_DELAY_MS);
	zassert_false(ln9310_power_good(), NULL);

	i2c_common_emul_set_write_func(
		emul, &mock_write_fn_intercept_startup_workaround, &test_data);

	ln9310_software_enable(1);
	zassert_true(test_data.startup_workaround_attempted, NULL);

	/* TODO(b/201420132) */
	k_msleep(TEST_DELAY_MS);
	zassert_true(ln9310_power_good(), NULL);

	ln9310_software_enable(0);

	/* TODO(b/201420132) */
	k_msleep(TEST_DELAY_MS);
	zassert_false(ln9310_power_good(), NULL);

	i2c_common_emul_set_write_func(emul, NULL, NULL);
}

static void reset_ln9310_state(void)
{
	ln9310_reset_to_initial_state();
}

void test_suite_ln9310(void)
{
	ztest_test_suite(
		ln9310,
		ztest_unit_test_setup_teardown(test_ln9310_read_chip_fails,
					       reset_ln9310_state,
					       reset_ln9310_state),
		ztest_unit_test_setup_teardown(test_ln9310_2s_powers_up,
					       reset_ln9310_state,
					       reset_ln9310_state),
		ztest_unit_test_setup_teardown(test_ln9310_3s_powers_up,
					       reset_ln9310_state,
					       reset_ln9310_state),
		ztest_unit_test_setup_teardown(
			test_ln9310_2s_cfly_precharge_startup,
			reset_ln9310_state, reset_ln9310_state),
		ztest_unit_test_setup_teardown(
			test_ln9310_3s_cfly_precharge_startup,
			reset_ln9310_state, reset_ln9310_state));
	ztest_run_test_suite(ln9310);
}
