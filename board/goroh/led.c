/* Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "ec_commands.h"
#include "led_common.h"
#include "led_pwm.h"
#include "pwm.h"

struct pwm_led_color_map led_color_map[EC_LED_COLOR_COUNT] = {
	/* Green, Red */
	[EC_LED_COLOR_RED] = { 0, 100 }, [EC_LED_COLOR_GREEN] = { 100, 0 },
	[EC_LED_COLOR_BLUE] = { 0, 0 },	 [EC_LED_COLOR_YELLOW] = { 0, 0 },
	[EC_LED_COLOR_WHITE] = { 0, 0 }, [EC_LED_COLOR_AMBER] = { 0, 0 },
};

struct pwm_led pwm_leds[CONFIG_LED_PWM_COUNT] = {
	[PWM_LED0] = {
		/* left port LEDs */
		.ch0 = PWM_CH_LED_GREEN,
		.ch1 = PWM_CH_LED_RED,
		.ch2 = PWM_LED_NO_CHANNEL,
		.enable = &pwm_enable,
		.set_duty = &pwm_set_duty,
	},
};

const enum ec_led_id supported_led_ids[] = {
	EC_LED_ID_BATTERY_LED,
	EC_LED_ID_POWER_LED,
};

const int supported_led_ids_count = ARRAY_SIZE(supported_led_ids);

void led_get_brightness_range(enum ec_led_id led_id, uint8_t *brightness_range)
{
	if (led_id == EC_LED_ID_BATTERY_LED)
		brightness_range[EC_LED_COLOR_RED] = 100;
	else if (led_id == EC_LED_ID_POWER_LED)
		brightness_range[EC_LED_COLOR_GREEN] = 100;
}

int led_set_brightness(enum ec_led_id led_id, const uint8_t *brightness)
{
	enum pwm_led_id pwm_id;

	/* Convert ec_led_id to pwm_led_id. */
	if (led_id == EC_LED_ID_POWER_LED || led_id == EC_LED_ID_BATTERY_LED)
		pwm_id = PWM_LED0;
	else
		return EC_ERROR_UNKNOWN;

	if (brightness[EC_LED_COLOR_RED])
		set_pwm_led_color(pwm_id, EC_LED_COLOR_RED);
	else if (brightness[EC_LED_COLOR_GREEN])
		set_pwm_led_color(pwm_id, EC_LED_COLOR_GREEN);
	else
		/* Otherwise, the "color" is "off". */
		set_pwm_led_color(pwm_id, -1);

	return EC_SUCCESS;
}
