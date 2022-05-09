/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <ztest.h>
#include <zephyr/drivers/emul.h>
#include <zephyr/kernel.h>
#include <ztest_assert.h>
#include <zephyr/drivers/i2c_emul.h>

#include "driver/ln9310.h"
#include "emul/emul_ln9310.h"
#include "emul/emul_common_i2c.h"
#include "timer.h"
#include "test/drivers/test_state.h"

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

ZTEST(ln9310, test_ln9310_read_chip_fails)
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

	zassert_true(ln9310_init() != 0, NULL);
	zassert_false(ln9310_emul_is_init(emulator), NULL);

	/* TODO(b/201420132) */
	k_msleep(TEST_DELAY_MS);
	zassert_false(ln9310_power_good(), NULL);

	i2c_common_emul_set_read_fail_reg(i2c_emul,
					  I2C_COMMON_EMUL_NO_FAIL_REG);
}

ZTEST(ln9310, test_ln9310_2s_powers_up)
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

	ln9310_software_enable(true);

	k_msleep(TEST_DELAY_MS);
	zassert_true(ln9310_power_good(), NULL);
}

ZTEST(ln9310, test_ln9310_3s_powers_up)
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

	ln9310_software_enable(true);

	k_msleep(TEST_DELAY_MS);
	zassert_true(ln9310_power_good(), NULL);
}

struct startup_workaround_data {
	bool startup_workaround_attempted;
	bool startup_workaround_should_fail;
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

	if (test_data->startup_workaround_should_fail)
		return -1;

	return 1;
}

ZTEST(ln9310, test_ln9310_2s_cfly_precharge_startup)
{
	const struct emul *emulator =
		emul_get_binding(DT_LABEL(DT_NODELABEL(ln9310)));

	struct i2c_emul *emul = ln9310_emul_get_i2c_emul(emulator);

	struct startup_workaround_data test_data = {
		.startup_workaround_attempted = false,
		.startup_workaround_should_fail = false,
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
		emul, mock_write_fn_intercept_startup_workaround, &test_data);

	ln9310_software_enable(true);
	zassert_true(test_data.startup_workaround_attempted, NULL);

	/* TODO(b/201420132) */
	k_msleep(TEST_DELAY_MS);
	zassert_true(ln9310_power_good(), NULL);

	ln9310_software_enable(false);

	/* TODO(b/201420132) */
	k_msleep(TEST_DELAY_MS);
	zassert_false(ln9310_power_good(), NULL);

	i2c_common_emul_set_write_func(emul, NULL, NULL);
}

ZTEST(ln9310, test_ln9310_3s_cfly_precharge_startup)
{
	const struct emul *emulator =
		emul_get_binding(DT_LABEL(DT_NODELABEL(ln9310)));
	struct i2c_emul *emul = ln9310_emul_get_i2c_emul(emulator);

	struct startup_workaround_data test_data = {
		.startup_workaround_attempted = false,
		.startup_workaround_should_fail = false,
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
		emul, mock_write_fn_intercept_startup_workaround, &test_data);

	ln9310_software_enable(true);
	zassert_true(test_data.startup_workaround_attempted, NULL);

	/* TODO(b/201420132) */
	k_msleep(TEST_DELAY_MS);
	zassert_true(ln9310_power_good(), NULL);

	ln9310_software_enable(false);

	/* TODO(b/201420132) */
	k_msleep(TEST_DELAY_MS);
	zassert_false(ln9310_power_good(), NULL);

	i2c_common_emul_set_write_func(emul, NULL, NULL);
}

ZTEST(ln9310, test_ln9310_cfly_precharge_exceeds_retries)
{
	const struct emul *emulator =
		emul_get_binding(DT_LABEL(DT_NODELABEL(ln9310)));

	struct i2c_emul *emul = ln9310_emul_get_i2c_emul(emulator);

	struct startup_workaround_data test_data = {
		.startup_workaround_attempted = false,
		.startup_workaround_should_fail = true,
	};

	zassert_not_null(emulator, NULL);

	ln9310_emul_set_context(emulator);
	ln9310_emul_reset(emulator);
	/*
	 * Battery and chip rev won't matter for statement
	 * coverage here so only testing one pair.
	 */
	ln9310_emul_set_battery_cell_type(emulator, BATTERY_CELL_TYPE_2S);
	ln9310_emul_set_version(emulator,
				REQUIRES_CFLY_PRECHARGE_STARTUP_CHIP_REV);

	zassert_ok(ln9310_init(), NULL);
	zassert_true(ln9310_emul_is_init(emulator), NULL);

	/* TODO(b/201420132) */
	k_msleep(TEST_DELAY_MS);
	zassert_false(ln9310_power_good(), NULL);

	i2c_common_emul_set_write_func(
		emul, mock_write_fn_intercept_startup_workaround, &test_data);

	ln9310_software_enable(true);
	zassert_true(test_data.startup_workaround_attempted, NULL);

	/* TODO(b/201420132) */
	k_msleep(TEST_DELAY_MS);
	zassert_false(ln9310_power_good(), NULL);

	i2c_common_emul_set_write_func(emul, NULL, NULL);
}

ZTEST(ln9310, test_ln9310_battery_unknown)
{
	const struct emul *emulator =
		emul_get_binding(DT_LABEL(DT_NODELABEL(ln9310)));

	zassert_not_null(emulator, NULL);

	ln9310_emul_set_context(emulator);
	ln9310_emul_reset(emulator);
	/*
	 * Chip rev won't matter for statement
	 * cov so only testing one version.
	 */
	ln9310_emul_set_battery_cell_type(emulator, BATTERY_CELL_TYPE_UNKNOWN);
	ln9310_emul_set_version(emulator, LN9310_BC_STS_C_CHIP_REV_FIXED);

	zassert_true(ln9310_init() != 0, NULL);
	zassert_false(ln9310_emul_is_init(emulator), NULL);

	/* TODO(b/201420132) */
	k_msleep(TEST_DELAY_MS);
	zassert_false(ln9310_power_good(), NULL);

	ln9310_software_enable(true);

	/* TODO(b/201420132) */
	k_msleep(TEST_DELAY_MS);
	zassert_false(ln9310_power_good(), NULL);
}

ZTEST(ln9310, test_ln9310_2s_battery_read_fails)
{
	const struct emul *emulator =
		emul_get_binding(DT_LABEL(DT_NODELABEL(ln9310)));
	struct i2c_emul *i2c_emul = ln9310_emul_get_i2c_emul(emulator);

	zassert_not_null(emulator, NULL);
	zassert_not_null(i2c_emul, NULL);

	ln9310_emul_set_context(emulator);
	ln9310_emul_reset(emulator);

	ln9310_emul_set_battery_cell_type(emulator, BATTERY_CELL_TYPE_2S);
	ln9310_emul_set_version(emulator, LN9310_BC_STS_C_CHIP_REV_FIXED);

	i2c_common_emul_set_read_fail_reg(i2c_emul, LN9310_REG_BC_STS_B);

	zassert_true(ln9310_init() != 0, NULL);
	zassert_false(ln9310_emul_is_init(emulator), NULL);

	/* TODO(b/201420132) */
	k_msleep(TEST_DELAY_MS);
	zassert_false(ln9310_power_good(), NULL);

	/* For Battery 2S Versions: Test Read Battery Voltage Failure Too */
	ln9310_emul_reset(emulator);
	ln9310_emul_set_battery_cell_type(emulator, BATTERY_CELL_TYPE_2S);
	ln9310_emul_set_version(emulator, LN9310_BC_STS_C_CHIP_REV_FIXED);

	i2c_common_emul_set_read_fail_reg(i2c_emul, LN9310_REG_TRACK_CTRL);

	zassert_false(ln9310_init() == 0, NULL);
	i2c_common_emul_set_read_fail_reg(i2c_emul,
					  I2C_COMMON_EMUL_NO_FAIL_REG);
}

ZTEST(ln9310, test_ln9310_lion_ctrl_reg_fails)
{
	const struct emul *emulator =
		emul_get_binding(DT_LABEL(DT_NODELABEL(ln9310)));
	struct i2c_emul *i2c_emul = ln9310_emul_get_i2c_emul(emulator);

	zassert_not_null(emulator, NULL);
	zassert_not_null(i2c_emul, NULL);

	ln9310_emul_set_context(emulator);
	ln9310_emul_reset(emulator);
	/* Battery won't matter here so only testing one version */
	ln9310_emul_set_battery_cell_type(emulator, BATTERY_CELL_TYPE_2S);
	ln9310_emul_set_version(emulator, LN9310_BC_STS_C_CHIP_REV_FIXED);

	i2c_common_emul_set_read_fail_reg(i2c_emul, LN9310_REG_LION_CTRL);

	zassert_true(ln9310_init() != 0, NULL);
	zassert_false(ln9310_emul_is_init(emulator), NULL);

	/* TODO(b/201420132) */
	k_msleep(TEST_DELAY_MS);
	zassert_false(ln9310_power_good(), NULL);

	/* TODO(b/201420132) */
	k_msleep(TEST_DELAY_MS);
	ln9310_software_enable(true);
	zassert_false(ln9310_power_good(), NULL);

	i2c_common_emul_set_read_fail_reg(i2c_emul,
					  I2C_COMMON_EMUL_NO_FAIL_REG);
}


struct precharge_timeout_data {
	timestamp_t time_to_mock;
	bool handled_clearing_standby_en_bit_timeout;
};

static int mock_intercept_startup_ctrl_reg(struct i2c_emul *emul, int reg,
					   uint8_t val, int bytes, void *data)
{
	struct precharge_timeout_data *test_data = data;

	if (reg == LN9310_REG_STARTUP_CTRL &&
	    test_data->handled_clearing_standby_en_bit_timeout == false) {
		if (val == 0) {
			timestamp_t time = get_time();

			time.val += 1 + LN9310_CFLY_PRECHARGE_TIMEOUT;
			test_data->time_to_mock = time;
			get_time_mock = &test_data->time_to_mock;
		} else {
			/* ln9310 aborts a startup attempt */
			test_data->handled_clearing_standby_en_bit_timeout =
				true;
			get_time_mock = NULL;
		}
	}
	return 1;
}

ZTEST(ln9310, test_ln9310_cfly_precharge_timesout)
{
	const struct emul *emulator =
		emul_get_binding(DT_LABEL(DT_NODELABEL(ln9310)));
	struct i2c_emul *i2c_emul = ln9310_emul_get_i2c_emul(emulator);
	struct precharge_timeout_data test_data = {
		.time_to_mock = {
			.val = -1,
			.le = {
				.lo = -1,
				.hi = -1,
			},
		},
		.handled_clearing_standby_en_bit_timeout = false,
	};

	zassert_not_null(emulator, NULL);
	zassert_not_null(i2c_emul, NULL);

	ln9310_emul_set_context(emulator);
	ln9310_emul_reset(emulator);
	/* Battery and chip rev won't matter here so only testing one pair */
	ln9310_emul_set_battery_cell_type(emulator, BATTERY_CELL_TYPE_2S);
	ln9310_emul_set_version(emulator,
				REQUIRES_CFLY_PRECHARGE_STARTUP_CHIP_REV);

	zassert_ok(ln9310_init(), NULL);
	zassert_true(ln9310_emul_is_init(emulator), NULL);

	/* TODO(b/201420132) */
	k_msleep(TEST_DELAY_MS);
	zassert_false(ln9310_power_good(), NULL);

	i2c_common_emul_set_write_func(
		i2c_emul, mock_intercept_startup_ctrl_reg, &test_data);

	ln9310_software_enable(true);
	/* TODO(b/201420132) */
	k_msleep(TEST_DELAY_MS);
	zassert_true(test_data.handled_clearing_standby_en_bit_timeout, NULL);
	/* It only times out on one attempt, it should subsequently startup */
	zassert_true(ln9310_power_good(), NULL);

	i2c_common_emul_set_write_func(i2c_emul, NULL, NULL);
}

struct reg_to_fail_data {
	int reg_access_to_fail;
	int reg_access_fail_countdown;
};

static int mock_read_intercept_reg_to_fail(struct i2c_emul *emul, int reg,
					   uint8_t *val, int bytes, void *data)
{
	struct reg_to_fail_data *test_data = data;

	if (reg == test_data->reg_access_to_fail) {
		test_data->reg_access_fail_countdown--;
		if (test_data->reg_access_fail_countdown <= 0)
			return -1;
	}
	return 1;
}

ZTEST(ln9310, test_ln9310_interrupt_reg_fail)
{
	const struct emul *emulator =
		emul_get_binding(DT_LABEL(DT_NODELABEL(ln9310)));
	struct i2c_emul *i2c_emul = ln9310_emul_get_i2c_emul(emulator);
	struct reg_to_fail_data test_data = {
		.reg_access_to_fail = 0,
		.reg_access_fail_countdown = 0,
	};

	zassert_not_null(emulator, NULL);
	zassert_not_null(i2c_emul, NULL);

	ln9310_emul_set_context(emulator);
	ln9310_emul_reset(emulator);
	/* Battery and chip rev won't matter here so only testing one pair */
	ln9310_emul_set_battery_cell_type(emulator, BATTERY_CELL_TYPE_2S);
	ln9310_emul_set_version(emulator, LN9310_BC_STS_C_CHIP_REV_FIXED);

	zassert_ok(ln9310_init(), NULL);
	zassert_true(ln9310_emul_is_init(emulator), NULL);

	i2c_common_emul_set_read_func(
		i2c_emul, mock_read_intercept_reg_to_fail, &test_data);

	/* Fail in beginning of software enable */
	test_data.reg_access_to_fail = LN9310_REG_INT1;
	test_data.reg_access_fail_countdown = 1;

	ln9310_software_enable(true);

	/* TODO(b/201420132) */
	k_msleep(TEST_DELAY_MS);
	zassert_false(ln9310_power_good(), NULL);
	zassert_true(test_data.reg_access_fail_countdown <= 0, NULL);

	/* Fail in irq interrupt handler */
	test_data.reg_access_fail_countdown = 2;

	ln9310_software_enable(true);
	/* TODO(b/201420132) */
	k_msleep(TEST_DELAY_MS);
	zassert_false(ln9310_power_good(), NULL);
	zassert_true(test_data.reg_access_fail_countdown <= 0, NULL);

	i2c_common_emul_set_read_func(i2c_emul, NULL, NULL);
}

ZTEST(ln9310, test_ln9310_sys_sts_reg_fail)
{
	const struct emul *emulator =
		emul_get_binding(DT_LABEL(DT_NODELABEL(ln9310)));
	struct i2c_emul *i2c_emul = ln9310_emul_get_i2c_emul(emulator);
	struct reg_to_fail_data test_data = {
		.reg_access_to_fail = 0,
		.reg_access_fail_countdown = 0,
	};

	zassert_not_null(emulator, NULL);
	zassert_not_null(i2c_emul, NULL);

	ln9310_emul_set_context(emulator);
	ln9310_emul_reset(emulator);
	/* Battery and chip rev won't matter here so only testing one pair */
	ln9310_emul_set_battery_cell_type(emulator, BATTERY_CELL_TYPE_2S);
	ln9310_emul_set_version(emulator, LN9310_BC_STS_C_CHIP_REV_FIXED);

	zassert_ok(ln9310_init(), NULL);
	zassert_true(ln9310_emul_is_init(emulator), NULL);

	i2c_common_emul_set_read_func(
		i2c_emul, &mock_read_intercept_reg_to_fail, &test_data);

	/* Register only read once and in the interrupt handler */
	test_data.reg_access_to_fail = LN9310_REG_SYS_STS;
	test_data.reg_access_fail_countdown = 1;

	ln9310_software_enable(1);

	/* TODO(b/201420132) */
	k_msleep(TEST_DELAY_MS);

	zassert_false(ln9310_power_good(), NULL);
	zassert_true(test_data.reg_access_fail_countdown <= 0, NULL);

	i2c_common_emul_set_read_func(i2c_emul, NULL, NULL);
}

struct reg_to_intercept {
	int reg;
	uint8_t replace_val;
};

static int mock_read_interceptor(struct i2c_emul *emul, int reg, uint8_t *val,
				 int bytes, void *data)
{
	struct reg_to_intercept *test_data = data;

	if (test_data->reg == reg) {
		*val = test_data->replace_val;
		return 0;
	}

	return 1;
}

ZTEST(ln9310, test_ln9310_reset_explicit_detected_startup)
{
	const struct emul *emulator =
		emul_get_binding(DT_LABEL(DT_NODELABEL(ln9310)));
	struct i2c_emul *i2c_emul = ln9310_emul_get_i2c_emul(emulator);
	struct reg_to_intercept test_data = {
		.reg = LN9310_REG_LION_CTRL,
		.replace_val = 0,
	};

	zassert_not_null(emulator, NULL);
	zassert_not_null(i2c_emul, NULL);

	ln9310_emul_set_context(emulator);
	ln9310_emul_reset(emulator);
	/* Battery and chip rev won't matter here so only testing one pair */
	ln9310_emul_set_battery_cell_type(emulator, BATTERY_CELL_TYPE_2S);
	ln9310_emul_set_version(emulator, LN9310_BC_STS_C_CHIP_REV_FIXED);

	zassert_ok(ln9310_init(), NULL);
	zassert_true(ln9310_emul_is_init(emulator), NULL);

	i2c_common_emul_set_read_func(i2c_emul, &mock_read_interceptor,
				      &test_data);

	ln9310_software_enable(true);

	/* TODO(b/201420132) */
	k_msleep(TEST_DELAY_MS);

	zassert_true(ln9310_power_good(), NULL);

	i2c_common_emul_set_read_func(i2c_emul, NULL, NULL);
}

ZTEST(ln9310, test_ln9310_update_startup_seq_fails)
{
	const struct emul *emulator =
		emul_get_binding(DT_LABEL(DT_NODELABEL(ln9310)));
	struct i2c_emul *i2c_emul = ln9310_emul_get_i2c_emul(emulator);
	struct reg_to_fail_data test_data = {
		.reg_access_to_fail = LN9310_REG_CFG_4,
		.reg_access_fail_countdown = 1,
	};

	zassert_not_null(emulator, NULL);
	zassert_not_null(i2c_emul, NULL);

	ln9310_emul_set_context(emulator);
	ln9310_emul_reset(emulator);
	/* Battery won't matter here so only testing one pair */
	ln9310_emul_set_battery_cell_type(emulator, BATTERY_CELL_TYPE_2S);
	/* Requires older version of chip */
	ln9310_emul_set_version(emulator,
				REQUIRES_CFLY_PRECHARGE_STARTUP_CHIP_REV);

	i2c_common_emul_set_read_func(
		i2c_emul, &mock_read_intercept_reg_to_fail, &test_data);

	zassert_false(ln9310_init() == 0, NULL);
	zassert_false(ln9310_emul_is_init(emulator), NULL);

	ln9310_software_enable(true);

	/* TODO(b/201420132) */
	k_msleep(TEST_DELAY_MS);

	zassert_false(ln9310_power_good(), NULL);
	zassert_true(test_data.reg_access_fail_countdown <= 0, NULL);

	i2c_common_emul_set_read_func(i2c_emul, NULL, NULL);
}

ZTEST(ln9310, test_ln9310_state_change_only_on_mode_change_interrupt)
{
	const struct emul *emulator =
		emul_get_binding(DT_LABEL(DT_NODELABEL(ln9310)));
	struct i2c_emul *i2c_emul = ln9310_emul_get_i2c_emul(emulator);
	struct reg_to_intercept test_data = {
		.reg = LN9310_REG_INT1,
		.replace_val = 0,
	};

	zassert_not_null(emulator, NULL);
	zassert_not_null(i2c_emul, NULL);

	ln9310_emul_set_context(emulator);
	ln9310_emul_reset(emulator);
	/* Battery and chip rev won't matter here so only testing one pair */
	ln9310_emul_set_battery_cell_type(emulator, BATTERY_CELL_TYPE_2S);
	ln9310_emul_set_version(emulator, LN9310_BC_STS_C_CHIP_REV_FIXED);

	zassert_ok(ln9310_init(), NULL);
	zassert_true(ln9310_emul_is_init(emulator), NULL);

	i2c_common_emul_set_read_func(i2c_emul, &mock_read_interceptor,
				      &test_data);

	ln9310_software_enable(true);

	/* TODO(b/201420132) */
	k_msleep(TEST_DELAY_MS);

	zassert_false(ln9310_power_good(), NULL);

	i2c_common_emul_set_read_func(i2c_emul, NULL, NULL);
}

static inline void reset_ln9310_state(void)
{
	ln9310_reset_to_initial_state();
	get_time_mock = NULL;
}

static void ln9310_before(void *state)
{
	ARG_UNUSED(state);
	reset_ln9310_state();
}

static void ln9310_after(void *state)
{
	ARG_UNUSED(state);
	reset_ln9310_state();
}

ZTEST_SUITE(ln9310, drivers_predicate_post_main, NULL, ln9310_before,
	    ln9310_after, NULL);
