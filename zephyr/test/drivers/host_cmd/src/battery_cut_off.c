/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "battery.h"
#include "button.h"
#include "charge_manager.h"
#include "emul/emul_common_i2c.h"
#include "emul/emul_smart_battery.h"
#include "hooks.h"
#include "host_command.h"
#include "keyboard_scan.h"
#include "test/drivers/test_state.h"
#include "test/drivers/utils.h"

#include <zephyr/drivers/emul.h>
#include <zephyr/ztest.h>

struct host_cmd_battery_cut_off_fixture {
	const struct emul *emul;
	struct i2c_common_emul_data *i2c_emul;
};

static void *host_cmd_battery_cut_off_setup(void)
{
	static struct host_cmd_battery_cut_off_fixture fixture = {
		.emul = EMUL_DT_GET(DT_NODELABEL(battery)),
	};

	fixture.i2c_emul = emul_smart_battery_get_i2c_common_data(fixture.emul);

	return &fixture;
}

static void host_cmd_battery_cut_off_before(void *f)
{
	ARG_UNUSED(f);
	test_set_battery_level(75);

	/* Tests assume AC is initially connected. */
	set_ac_enabled(true);
	hook_notify(HOOK_AC_CHANGE);
	k_msleep(1000);
}

static void host_cmd_battery_cut_off_after(void *f)
{
	struct host_cmd_battery_cut_off_fixture *fixture = f;

	i2c_common_emul_set_write_fail_reg(fixture->i2c_emul,
					   I2C_COMMON_EMUL_NO_FAIL_REG);
}

static void host_cmd_battery_cut_off_teardown(void *f)
{
	/* Apply external power again to clear battery cutoff. */
	set_ac_enabled(true);
	hook_notify(HOOK_AC_CHANGE);
	k_msleep(1000);
}

ZTEST_SUITE(host_cmd_battery_cut_off, drivers_predicate_post_main,
	    host_cmd_battery_cut_off_setup, host_cmd_battery_cut_off_before,
	    host_cmd_battery_cut_off_after, host_cmd_battery_cut_off_teardown);

ZTEST_USER_F(host_cmd_battery_cut_off, test_fail_sb_write)
{
	int rv;

	/* Force a failure on the battery i2c write to 0x00 */
	i2c_common_emul_set_write_fail_reg(fixture->i2c_emul, 0);

	rv = ec_cmd_battery_cut_off(NULL);
	zassert_equal(EC_RES_ERROR, rv, "Expected 0, but got %d", rv);
}

ZTEST_USER(host_cmd_battery_cut_off, test_cutoff_battery)
{
	int rv;

	rv = ec_cmd_battery_cut_off(NULL);
	zassert_equal(EC_RES_SUCCESS, rv, "Expected 0, but got %d", rv);
	zassert_true(battery_cutoff_in_progress());
	/* CONFIG_BATTERY_CUTOFF_TIMEOUT_MSEC is set to 500 in prj.conf. */
	zassert_true(WAIT_FOR(battery_is_cut_off(), 510000, k_msleep(250)));
}

ZTEST_USER(host_cmd_battery_cut_off, test_cutoff_v1)
{
	int rv;
	struct ec_params_battery_cutoff params = {
		.flags = 0,
	};

	rv = ec_cmd_battery_cut_off_v1(NULL, &params);
	zassert_equal(EC_RES_SUCCESS, rv, "Expected 0, but got %d", rv);
	zassert_true(battery_cutoff_in_progress());
	k_msleep(500);
	zassert_true(battery_is_cut_off());
}

ZTEST_USER(host_cmd_battery_cut_off, test_cutoff_at_shutdown)
{
	int rv;
	struct ec_params_battery_cutoff params = {
		.flags = EC_BATTERY_CUTOFF_FLAG_AT_SHUTDOWN,
	};

	rv = ec_cmd_battery_cut_off_v1(NULL, &params);
	zassert_equal(EC_RES_SUCCESS, rv, "Expected 0, but got %d", rv);
	zassert_false(battery_is_cut_off(), NULL);
	test_set_chipset_to_g3();
	zassert_true(WAIT_FOR(battery_is_cut_off(), 1500000, k_msleep(250)));
}

void boot_key_set(enum boot_key key);
void boot_key_clear(enum boot_key key);
void boot_button_set(enum button button);
void boot_button_clear(enum button button);

ZTEST_USER(host_cmd_battery_cut_off, test_cutoff_by_unplug)
{
	const struct charge_port_info charge = {
		.current = 3000,
		.voltage = 15000,
	};

	boot_key_set(BOOT_KEY_REFRESH);

	/* This fails because !had_active_charge_port. */
	hook_notify(HOOK_POWER_SUPPLY_CHANGE);
	zassert_false(battery_cutoff_in_progress());
	zassert_false(battery_is_cut_off());

	/* Plug AC. */
	charge_manager_update_dualrole(0, CAP_DEDICATED);
	charge_manager_update_charge(CHARGE_SUPPLIER_PD, 0, &charge);
	/* No cutoff because there is active charge port. */
	zassert_false(
		WAIT_FOR(battery_cutoff_in_progress(), 1500000, k_msleep(250)));

	/*
	 * Unplug AC to start scheduled cutoff (that will fail because the
	 * system doesn't brown out after cutting off the battery despite not
	 * having external power connected).
	 */
	set_ac_enabled(false);
	hook_notify(HOOK_AC_CHANGE);
	charge_manager_update_charge(CHARGE_SUPPLIER_PD, 0, NULL);
	zassert_true(
		WAIT_FOR(battery_cutoff_in_progress(), 1500000, k_msleep(250)));

	boot_key_clear(BOOT_KEY_REFRESH);

	/*
	 * Plug AC to cancel cutoff, before the operation started by AC unplug
	 * times out and cancels automatically.
	 */
	charge_manager_update_dualrole(0, CAP_DEDICATED);
	charge_manager_update_charge(CHARGE_SUPPLIER_PD, 0, &charge);
	set_ac_enabled(true);
	hook_notify(HOOK_AC_CHANGE);
	zassert_false(
		WAIT_FOR(battery_cutoff_in_progress(), 1500000, k_msleep(250)));

	boot_button_set(BUTTON_VOLUME_UP);

	/* Unplug AC to trigger cutoff, which completes with AC connected. */
	charge_manager_update_charge(CHARGE_SUPPLIER_PD, 0, NULL);
	zassert_true(WAIT_FOR(battery_is_cut_off(), 1500000, k_msleep(250)));

	boot_button_clear(BUTTON_VOLUME_UP);
}

ZTEST_USER(host_cmd_battery_cut_off, test_no_cutoff_by_key)
{
	const struct charge_port_info charge = {
		.current = 3000,
		.voltage = 15000,
	};

	/* Plug AC. */
	charge_manager_update_dualrole(0, CAP_DEDICATED);
	charge_manager_update_charge(CHARGE_SUPPLIER_PD, 0, &charge);
	/* Let charge manager update available charge. */
	k_msleep(500);
	/* Unplug AC. */
	charge_manager_update_charge(CHARGE_SUPPLIER_PD, 0, NULL);
	zassert_false(
		WAIT_FOR(battery_cutoff_in_progress(), 1500000, k_msleep(250)));
}
