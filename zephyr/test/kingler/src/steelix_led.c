/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * LED test for Steelix
 */

#include "gpio_signal.h"
#include "led_common.h"
#include "led_onoff_states.h"

#include <zephyr/drivers/gpio/gpio_emul.h>
#include <zephyr/fff.h>
#include <zephyr/ztest.h>

FAKE_VOID_FUNC(x_ec_interrupt, enum gpio_signal);

static void steelix_led_before(void *f)
{
	ARG_UNUSED(f);
	led_control(EC_LED_ID_SYSRQ_DEBUG_LED, LED_STATE_RESET);
}

static void *steelix_led_setup(void)
{
	return NULL;
}

ZTEST_SUITE(steelix_led, NULL, steelix_led_setup, steelix_led_before, NULL,
	    NULL);

ZTEST(steelix_led, test_led_control)
{
	zassert_equal(1, led_auto_control_is_enabled(EC_LED_ID_BATTERY_LED));
	zassert_equal(0,
		      led_auto_control_is_enabled(EC_LED_ID_SYSRQ_DEBUG_LED));

	led_control(EC_LED_ID_SYSRQ_DEBUG_LED, LED_STATE_ON);

	zassert_equal(0, led_auto_control_is_enabled(EC_LED_ID_BATTERY_LED));

	led_control(EC_LED_ID_SYSRQ_DEBUG_LED, LED_STATE_RESET);

	zassert_equal(1, led_auto_control_is_enabled(EC_LED_ID_BATTERY_LED));
}
