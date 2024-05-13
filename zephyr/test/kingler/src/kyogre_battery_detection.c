/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "gpio_signal.h"
#include "hooks.h"
#include "zephyr/kernel.h"

#include <zephyr/drivers/gpio/gpio_emul.h>
#include <zephyr/fff.h>
#include <zephyr/ztest.h>

FAKE_VOID_FUNC(x_ec_interrupt);
FAKE_VOID_FUNC(lsm6dso_interrupt);
FAKE_VOID_FUNC(lis2dw12_interrupt);
FAKE_VOID_FUNC(pen_fault_interrupt);

static void *kyogre_battery_detection_setup(void)
{
	hook_notify(HOOK_INIT);

	return NULL;
}

ZTEST_SUITE(main_battery_detection, NULL, kyogre_battery_detection_setup, NULL,
	    NULL, NULL);

ZTEST(main_battery_detection, test_main_battery_detection)
{
	int flags;
	gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(ec_batt_pres_odl));

	/* check the initial state of ec_batt_pres_odl*/
	zassert_ok(gpio_pin_get_config_dt(
		GPIO_DT_FROM_NODELABEL(ec_batt_pres_odl), &flags));
	zassert_equal(flags, GPIO_OUTPUT_HIGH, "actual GPIO flags were %#x",
		      flags);

	/* wait for 1.001 seconds */
	k_sleep(K_MSEC(1001));

	/* check the updated state of ec_batt_pres_odl*/
	zassert_ok(gpio_pin_get_config_dt(
		GPIO_DT_FROM_NODELABEL(ec_batt_pres_odl), &flags));
	zassert_equal(flags, GPIO_INPUT, "actual GPIO flags were %#x", flags);
}
