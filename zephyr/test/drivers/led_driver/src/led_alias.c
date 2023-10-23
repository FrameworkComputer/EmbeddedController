/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "gpio.h"
#include "led_common.h"
#include "test/drivers/test_state.h"

#include <zephyr/ztest.h>

ZTEST_SUITE(led_driver_alias, drivers_predicate_post_main, NULL, NULL, NULL,
	    NULL);

ZTEST(led_driver_alias, test_control_sysrq_active)
{
	led_control(EC_LED_ID_SYSRQ_DEBUG_LED, LED_STATE_ON);
	zassert_true(
		gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_chg_led_w_c0)),
		"LED blue channel is not on");
}

ZTEST(led_driver_alias, test_control_sysrq_inactive)
{
	led_control(EC_LED_ID_SYSRQ_DEBUG_LED, LED_STATE_OFF);
	zassert_false(
		gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_chg_led_w_c0)),
		"LED blue channel is not on");
}

ZTEST(led_driver_alias, test_control_hw_reinit_active)
{
	led_control(EC_LED_ID_RECOVERY_HW_REINIT_LED, LED_STATE_ON);
	zassert_true(
		gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_chg_led_y_c0)),
		"LED blue channel is not on");
}

ZTEST(led_driver_alias, test_control_hw_reinit_inactive)
{
	led_control(EC_LED_ID_RECOVERY_HW_REINIT_LED, LED_STATE_OFF);
	zassert_false(
		gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_chg_led_y_c0)),
		"LED blue channel is not on");
}
