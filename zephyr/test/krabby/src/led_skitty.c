/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "charge_state.h"
#include "hooks.h"
#include "led_common.h"
#include "led_onoff_states.h"
#include "system.h"

#include <zephyr/fff.h>
#include <zephyr/ztest.h>

extern int ztest_duty_white;
extern int ztest_duty_amber;
static enum led_pwr_state test_state;
static int test_chg_percent;

__override enum led_pwr_state skitty_led_pwr_get_state()
{
	return test_state;
}

__override int skitty_charge_get_percent()
{
	return test_chg_percent;
}

ZTEST(skitty_led, test_led_control)
{
	uint8_t brightness[EC_LED_COLOR_COUNT] = { -1 };

	/* Verify LED set to OFF */
	brightness[EC_LED_COLOR_WHITE] = 1;
	brightness[EC_LED_COLOR_AMBER] = 0;

	int rv = led_set_brightness(EC_LED_ID_POWER_LED, brightness);
	zassert_equal(rv, EC_ERROR_PARAM1);

	led_set_brightness(EC_LED_ID_BATTERY_LED, brightness);
	zassert_equal(1, ztest_duty_white);
	zassert_equal(0, ztest_duty_amber);

	brightness[EC_LED_COLOR_WHITE] = 0;
	brightness[EC_LED_COLOR_AMBER] = 1;

	led_set_brightness(EC_LED_ID_BATTERY_LED, brightness);
	zassert_equal(0, ztest_duty_white);
	zassert_equal(1, ztest_duty_amber);

	brightness[EC_LED_COLOR_WHITE] = 0;
	brightness[EC_LED_COLOR_AMBER] = 0;
	led_set_brightness(EC_LED_ID_BATTERY_LED, brightness);
	zassert_equal(0, ztest_duty_white);
	zassert_equal(0, ztest_duty_amber);

	led_get_brightness_range(EC_LED_ID_BATTERY_LED, brightness);
	zassert_equal(brightness[EC_LED_COLOR_WHITE], 1);
	zassert_equal(brightness[EC_LED_COLOR_AMBER], 1);

	led_get_brightness_range(EC_LED_ID_POWER_LED, brightness);

	test_state = LED_PWRS_CHARGE;
	k_msleep(HOOK_TICK_INTERVAL_MS);
	zassert_equal(100, ztest_duty_amber);
	zassert_equal(0, ztest_duty_white);

	test_state = LED_PWRS_ERROR;

	k_msleep(HOOK_TICK_INTERVAL_MS);
	zassert_equal(0, ztest_duty_white);
	zassert_equal(0, ztest_duty_amber);

	k_msleep(HOOK_TICK_INTERVAL_MS);
	zassert_equal(0, ztest_duty_white);
	zassert_equal(100, ztest_duty_amber);

	test_state = LED_PWRS_DISCHARGE;

	test_chg_percent = 60;
	k_msleep(HOOK_TICK_INTERVAL_MS);
	zassert_equal(0, ztest_duty_white);
	zassert_equal(0, ztest_duty_amber);

	test_chg_percent = 7;
	k_msleep(HOOK_TICK_INTERVAL_MS);
	zassert_equal(0, ztest_duty_white);
	zassert_not_equal(0, ztest_duty_amber);

	test_chg_percent = 4;
	k_msleep(HOOK_TICK_INTERVAL_MS);
	zassert_equal(0, ztest_duty_white);
	zassert_not_equal(0, ztest_duty_amber);

	test_state = LED_PWRS_CHARGE_NEAR_FULL;
	k_msleep(HOOK_TICK_INTERVAL_MS);
	zassert_equal(0, ztest_duty_amber);
	zassert_equal(100, ztest_duty_white);

	test_state = LED_PWRS_IDLE;
	k_msleep(HOOK_TICK_INTERVAL_MS);
	zassert_equal(0, ztest_duty_amber);
	zassert_equal(100, ztest_duty_white);

	test_state = LED_PWRS_FORCED_IDLE;
	k_msleep(HOOK_TICK_INTERVAL_MS);
	zassert_equal(0, ztest_duty_white);
	zassert_not_equal(0, ztest_duty_amber);
}

static void skitty_led_test_init(void *fixture)
{
	test_state = LED_PWRS_CHARGE;
	test_chg_percent = 100;
}

ZTEST_SUITE(skitty_led, NULL, skitty_led_test_init, NULL, NULL, NULL);
