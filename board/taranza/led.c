/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Taranza specific LED settings. */

#include "chipset.h"
#include "ec_commands.h"
#include "gpio.h"
#include "hooks.h"
#include "led_common.h"

#define LED_ON_LVL 0
#define LED_OFF_LVL 1

const enum ec_led_id supported_led_ids[] = {
	EC_LED_ID_POWER_LED,
};

const int supported_led_ids_count = ARRAY_SIZE(supported_led_ids);

enum led_color {
	LED_OFF = 0,
	LED_GREEN,
	LED_COLOR_COUNT /* Number of colors, not a color itself */
};

static int led_set_color_power(enum led_color color)
{
	switch (color) {
	case LED_GREEN:
		gpio_set_level(GPIO_LED_W_ODL, LED_ON_LVL);
		break;
	case LED_OFF:
		gpio_set_level(GPIO_LED_W_ODL, LED_OFF_LVL);
		break;
	default:
		return EC_ERROR_UNKNOWN;
	}
	return EC_SUCCESS;
}

void led_get_brightness_range(enum ec_led_id led_id, uint8_t *brightness_range)
{
	brightness_range[EC_LED_COLOR_GREEN] = 1;
}

int led_set_brightness(enum ec_led_id led_id, const uint8_t *brightness)
{
	if (brightness[EC_LED_COLOR_GREEN] != 0)
		led_set_color_power(LED_GREEN);
	else
		led_set_color_power(LED_OFF);

	return EC_SUCCESS;
}

/* Called by hook task every TICK */
static void led_tick(void)
{
	if (!led_auto_control_is_enabled(EC_LED_ID_POWER_LED))
		return;

	if (chipset_in_state(CHIPSET_STATE_ON))
		led_set_color_power(LED_GREEN);
	else
		led_set_color_power(LED_OFF);
}
DECLARE_HOOK(HOOK_TICK, led_tick, HOOK_PRIO_DEFAULT);
