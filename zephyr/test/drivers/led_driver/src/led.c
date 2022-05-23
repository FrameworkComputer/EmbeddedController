/* Copyright 2022 The ChromiumOS Authors.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <ztest.h>
#include "ec_commands.h"
#include "gpio.h"
#include "led.h"
#include "led_common.h"
#include "test/drivers/test_state.h"

#define VERIFY_LED_COLOR(color, led_id)                                    \
	{                                                                  \
		const struct led_pins_node_t *pin_node =                   \
			led_get_node(color, led_id);                       \
		for (int j = 0; j < pin_node->pins_count; j++) {           \
			int val = gpio_pin_get_dt(gpio_get_dt_spec(        \
				pin_node->gpio_pins[j].signal));           \
			int expecting = pin_node->gpio_pins[j].val;        \
			zassert_equal(expecting, val, "[%d]: %d != %d", j, \
				      expecting, val);                     \
		}                                                          \
	}

ZTEST_SUITE(led_driver, drivers_predicate_post_main, NULL, NULL, NULL, NULL);

ZTEST(led_driver, test_led_control)
{
	/* Exercise valid led_id, set to RESET state */
	led_control(EC_LED_ID_SYSRQ_DEBUG_LED, LED_STATE_RESET);
	VERIFY_LED_COLOR(LED_OFF, EC_LED_ID_SYSRQ_DEBUG_LED);

	/* Exercise valid led_id, set to OFF state.
	 * Verify matches OFF color defined in device tree
	 */
	led_control(EC_LED_ID_SYSRQ_DEBUG_LED, LED_STATE_OFF);
	VERIFY_LED_COLOR(LED_OFF, EC_LED_ID_SYSRQ_DEBUG_LED);

	/* Exercise valid led_id, set to ON state.
	 * Verify matches ON color defined in device tree
	 */
	led_control(EC_LED_ID_SYSRQ_DEBUG_LED, LED_STATE_ON);
	VERIFY_LED_COLOR(LED_BLUE, EC_LED_ID_SYSRQ_DEBUG_LED);

	/* Exercise invalid led_id -- no change to led color */
	led_control(EC_LED_ID_LEFT_LED, LED_STATE_RESET);
	VERIFY_LED_COLOR(LED_BLUE, EC_LED_ID_SYSRQ_DEBUG_LED);
}

ZTEST(led_driver, test_led_brightness)
{
	uint8_t brightness[EC_LED_COLOR_COUNT] = { -1 };

	/* Verify LED set to OFF */
	led_set_brightness(EC_LED_ID_SYSRQ_DEBUG_LED, brightness);
	VERIFY_LED_COLOR(LED_OFF, EC_LED_ID_SYSRQ_DEBUG_LED);

	/* Verify LED colors defined in device tree are reflected in the
	 * brightness array.
	 */
	led_get_brightness_range(EC_LED_ID_SYSRQ_DEBUG_LED, brightness);
	zassert_equal(brightness[EC_LED_COLOR_BLUE], 1, NULL);
	zassert_equal(brightness[EC_LED_COLOR_WHITE], 1, NULL);

	/* Verify LED set to WHITE */
	led_set_brightness(EC_LED_ID_SYSRQ_DEBUG_LED, brightness);
	VERIFY_LED_COLOR(LED_WHITE, EC_LED_ID_SYSRQ_DEBUG_LED);
}
