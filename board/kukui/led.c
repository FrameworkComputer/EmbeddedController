/* Copyright 2018 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Battery LED control for Kukui board.
 *TODO(b:80160408): Implement mt6370 led driver.
 */

#include "hooks.h"
#include "led_common.h"

/* LEDs on Kukui are active low. */
#define BAT_LED_ON 0
#define BAT_LED_OFF 1

const enum ec_led_id supported_led_ids[] = { EC_LED_ID_BATTERY_LED };

const int supported_led_ids_count = ARRAY_SIZE(supported_led_ids);

void led_get_brightness_range(enum ec_led_id led_id, uint8_t *brightness_range)
{
}

int led_set_brightness(enum ec_led_id led_id, const uint8_t *brightness)
{
	return EC_SUCCESS;
}

/* Called by hook task every 1 sec */
static void led_second(void)
{
}
DECLARE_HOOK(HOOK_SECOND, led_second, HOOK_PRIO_DEFAULT);
