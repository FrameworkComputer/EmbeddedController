/* Copyright 2022 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <zephyr.h>
#include <ztest.h>

#include "battery.h"
#include "battery_smart.h"
#include "emul/emul_smart_battery.h"
#include "hooks.h"
#include "power.h"
#include "stubs.h"

#define BATTERY_ORD DT_DEP_ORD(DT_NODELABEL(battery))

void test_set_chipset_to_s0(void)
{
	struct sbat_emul_bat_data *bat;
	struct i2c_emul *emul;

	emul = sbat_emul_get_ptr(BATTERY_ORD);
	bat = sbat_emul_get_bat_data(emul);

	/*
	 * Make sure that battery is in good condition to
	 * not trigger hibernate in charge_state_v2.c
	 * Set battery voltage to expected value and capacity to 75%. Battery
	 * will not be full and accepts charging, but will not trigger
	 * hibernate. Charge level is chosen arbitrary.
	 */
	bat->cap = bat->full_cap * 3 / 4;
	bat->volt = battery_get_info()->voltage_normal;
	bat->design_mv = bat->volt;

	power_set_state(POWER_S0);
	test_power_common_state();

	/* Run all hooks on chipset  */
	hook_notify(HOOK_CHIPSET_RESUME);
	k_msleep(1);

	/* Check if chipset is in correct state */
	zassert_equal(POWER_S0, power_get_state(), NULL);
}
