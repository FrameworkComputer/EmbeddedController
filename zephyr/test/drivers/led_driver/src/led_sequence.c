/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "gpio.h"
#include "hooks.h"
#include "led_common.h"
#include "test/drivers/test_state.h"
#include "test/drivers/utils.h"

#include <zephyr/ztest.h>

void set_board_led_alt_policy(int label);

ZTEST_SUITE(led_driver_sequence, drivers_predicate_post_main, NULL, NULL, NULL,
	    NULL);

static bool is_blue_on(void)
{
	return gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_chg_led_y_c0));
}

static bool is_white_on(void)
{
	return gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_chg_led_w_c0));
}

ZTEST(led_driver_sequence, test_infinite_loop)
{
	/* Select the infinite loop policy node */
	set_board_led_alt_policy(2);
	led_control(EC_LED_ID_BATTERY_LED, LED_STATE_RESET);

	/* Pattern: Blue(250ms) -> White(250ms). Test 5 full cycles. */
	for (int i = 0; i < 5; i++) {
		hook_notify(HOOK_TICK);
		zassert_true(is_blue_on(), "Cycle %d: Blue should be on", i);
		hook_notify(HOOK_TICK);
		zassert_true(is_white_on(), "Cycle %d: White should be on", i);
	}
}

ZTEST(led_driver_sequence, test_run_once)
{
	/* Select the run-once policy node */
	set_board_led_alt_policy(3);
	led_control(EC_LED_ID_BATTERY_LED, LED_STATE_RESET);

	/* Step 0: White */
	hook_notify(HOOK_TICK);
	zassert_true(is_white_on());

	/* Step 1: Blue */
	hook_notify(HOOK_TICK);
	zassert_true(is_blue_on());

	/*
	 * Pattern cycle limit (1) has been reached. Test several more ticks to
	 * verify the state remains held on Blue.
	 */
	for (int i = 0; i < 10; i++) {
		hook_notify(HOOK_TICK);
		zassert_true(is_blue_on(), "Tick %d: Expected to hold Blue", i);
	}
}
