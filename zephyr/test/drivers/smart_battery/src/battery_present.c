/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "battery.h"
#include "battery_smart.h"
#include "emul/emul_common_i2c.h"
#include "emul/emul_smart_battery.h"
#include "test/drivers/test_state.h"

#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

#define BATTERY_NODE DT_NODELABEL(battery)

struct battery_present_fixture {
	const struct emul *emul;
	struct i2c_common_emul_data *common_data;
};

static void *battery_present_setup(void)
{
	static struct battery_present_fixture fixture;

	fixture.emul = EMUL_DT_GET(BATTERY_NODE);
	fixture.common_data =
		emul_smart_battery_get_i2c_common_data(fixture.emul);

	return &fixture;
}

static void battery_present_before_and_teardown(void *fixture)
{
	struct battery_present_fixture *f = fixture;

	i2c_common_emul_set_fail_auto_clear(f->common_data, false);
	i2c_common_emul_set_read_fail_reg(f->common_data,
					  I2C_COMMON_EMUL_NO_FAIL_REG);
	i2c_common_emul_set_write_fail_reg(f->common_data,
					   I2C_COMMON_EMUL_NO_FAIL_REG);
}

ZTEST_USER_F(battery_present, test_i2c_good)
{
	zassert_equal(battery_is_present(), BP_YES);
}

ZTEST_USER_F(battery_present, test_i2c_1_bad_read)
{
	i2c_common_emul_set_fail_auto_clear(fixture->common_data, true);
	i2c_common_emul_set_read_fail_reg(fixture->common_data,
					  I2C_COMMON_EMUL_FAIL_ALL_REG);

	zassert_equal(battery_is_present(), BP_YES);
}

ZTEST_USER_F(battery_present, test_i2c_access_fails)
{
	i2c_common_emul_set_fail_auto_clear(fixture->common_data, false);
	i2c_common_emul_set_read_fail_reg(fixture->common_data,
					  I2C_COMMON_EMUL_FAIL_ALL_REG);
	i2c_common_emul_set_write_fail_reg(fixture->common_data,
					   I2C_COMMON_EMUL_FAIL_ALL_REG);

	zassert_equal(battery_is_present(), BP_NOT_SURE);
}

ZTEST_SUITE(battery_present, drivers_predicate_post_main, battery_present_setup,
	    battery_present_before_and_teardown, NULL,
	    battery_present_before_and_teardown);
