/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "led_common.h"
#include "test/drivers/test_state.h"

#include <zephyr/fff.h>
#include <zephyr/ztest.h>

const enum ec_led_id supported_led_ids[] = { EC_LED_ID_BATTERY_LED,
					     EC_LED_ID_POWER_LED };
const int supported_led_ids_count = ARRAY_SIZE(supported_led_ids);

FAKE_VOID_FUNC(led_get_brightness_range, enum ec_led_id, uint8_t *);
FAKE_VALUE_FUNC(int, led_set_brightness, enum ec_led_id, const uint8_t *);

ZTEST_SUITE(led_common, drivers_predicate_post_main, NULL, NULL, NULL, NULL);

ZTEST(led_common, test_led_is_supported)
{
	zassert_true(led_is_supported(EC_LED_ID_BATTERY_LED));
	zassert_true(led_is_supported(EC_LED_ID_POWER_LED));

	zassert_false(led_is_supported(EC_LED_ID_ADAPTER_LED));
	zassert_false(led_is_supported(EC_LED_ID_LEFT_LED));
	zassert_false(led_is_supported(EC_LED_ID_RIGHT_LED));
	zassert_false(led_is_supported(EC_LED_ID_RECOVERY_HW_REINIT_LED));
	zassert_false(led_is_supported(EC_LED_ID_SYSRQ_DEBUG_LED));
}
