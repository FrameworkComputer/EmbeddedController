/* Copyright 2022 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/**
 * @file
 * @brief Unit Tests for LEDs.
 */

#include <device.h>

#include <drivers/gpio/gpio_emul.h>
#include <logging/log.h>
#include <zephyr.h>
#include <ztest.h>

#include "common.h"
#include "emul/emul_smart_battery.h"
#include "hooks.h"
#include "led_common.h"
#include "test/drivers/stubs.h"
#include "test/drivers/test_state.h"
#include "test/drivers/utils.h"

#define LED_BLUE_PATH DT_PATH(named_gpios, led_blue)
#define LED_BLUE_PORT DT_GPIO_PIN(LED_BLUE_PATH, gpios)
#define LED_AMBER_PATH DT_PATH(named_gpios, led_amber)
#define LED_AMBER_PORT DT_GPIO_PIN(LED_AMBER_PATH, gpios)

int get_blue_led(void)
{
	const struct device *dev =
		DEVICE_DT_GET(DT_GPIO_CTLR(LED_BLUE_PATH, gpios));
	return gpio_emul_output_get(dev, LED_BLUE_PORT);
}

int get_amber_led(void)
{
	const struct device *dev =
		DEVICE_DT_GET(DT_GPIO_CTLR(LED_AMBER_PATH, gpios));
	/* Amber LED is GPIO_ACTIVE_LOW, so return inverted value */
	return !gpio_emul_output_get(dev, LED_AMBER_PORT);
}

/**
 * @brief TestPurpose: Verify LED operation.
 *
 * @details
 * Validate LED operation depending on charger.
 *
 * Expected Results
 *  - LEDs GPIOs get set according to policy.
 */
ZTEST(leds, test_auto_policy)
{
	/*
	 * By default, test charger is set to discharging.
	 */
	/* set low battery */
	k_sleep(K_MSEC(10));
	test_set_battery_level(5);
	k_sleep(K_MSEC(1500));
	zassert_equal(1, get_amber_led(), "Expected amber==1");
	zassert_equal(1, get_blue_led(), "Expected blue==1");
	/* Restore normal battery and turn off CPU. */
	test_set_battery_level(75);
	test_set_chipset_to_g3();
	zassert_equal(1, get_amber_led(), "Expected amber==1");
	zassert_equal(0, get_blue_led(), "Expected blue==0");
	/* Turn on CPU */
	test_set_chipset_to_s0();
	zassert_equal(0, get_amber_led(), "Expected amber==0");
	zassert_equal(1, get_blue_led(), "Expected blue==1");
}

/**
 * @brief TestPurpose: Verify led_get_brightness_range API call.
 *
 * @details
 * Validate API get calls
 *
 * Expected Results
 *  - Can get LED brightness max range via common LED API.
 */
ZTEST(leds, test_common_api_get)
{
	uint8_t brightness[EC_LED_COLOR_COUNT];

	memset(brightness, 0, sizeof(brightness));

	led_get_brightness_range(EC_LED_ID_BATTERY_LED, brightness);
	zassert_equal(0, brightness[EC_LED_COLOR_RED], "Expected red==0");
	zassert_equal(0, brightness[EC_LED_COLOR_GREEN], "Expected green==0");
	zassert_equal(1, brightness[EC_LED_COLOR_BLUE], "Expected blue==1");
	zassert_equal(0, brightness[EC_LED_COLOR_YELLOW], "Expected yellow==0");
	zassert_equal(0, brightness[EC_LED_COLOR_WHITE], "Expected white==0");
	zassert_equal(1, brightness[EC_LED_COLOR_AMBER], "Expected amber==1");
}

/**
 * @brief TestPurpose: Verify led_set_brightness API call.
 *
 * @details
 * Validate API set call.
 *
 * Expected Results
 *  - Can set LEDs via common LED API.
 */
ZTEST(leds, test_common_api_set)
{
	uint8_t brightness[EC_LED_COLOR_COUNT];

	memset(brightness, 0, sizeof(brightness));
	/*
	 * Invalid ID.
	 */
	zassert_not_equal(0,
		led_set_brightness(EC_LED_ID_RIGHT_LED, brightness),
		"Should have failed with invalid ID");
	/* Turn off auto-control */
	led_auto_control(EC_LED_ID_BATTERY_LED, 0);
	/* Set all LEDs to 0 */
	zassert_ok(led_set_brightness(EC_LED_ID_BATTERY_LED, brightness),
		   "led_set_brightness_range failed");
	k_sleep(K_MSEC(1200));
	zassert_equal(0, get_amber_led(), "Expected amber==0");
	zassert_equal(0, get_blue_led(), "Expected blue==0");
	brightness[EC_LED_COLOR_AMBER] = 1;
	brightness[EC_LED_COLOR_BLUE] = 1;
	zassert_ok(led_set_brightness(EC_LED_ID_BATTERY_LED, brightness),
		   "led_set_brightness_range failed");
	zassert_equal(1, get_amber_led(), "Expected amber==1");
	zassert_equal(1, get_blue_led(), "Expected blue==1");
	/* Reset back to auto control */
	led_auto_control(EC_LED_ID_BATTERY_LED, 1);
	k_sleep(K_MSEC(1200));
	zassert_equal(0, get_amber_led(), "Expected amber==0");
	zassert_equal(1, get_blue_led(), "Expected blue==1");
}


/**
 * @brief Test Suite: Verifies LED GPIO functionality.
 */
ZTEST_SUITE(leds, drivers_predicate_post_main, NULL, NULL, NULL, NULL);
