/* Copyright 2020 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Waddledee specific PWM LED settings. */

#include "common.h"
#include "ec_commands.h"
#include "led_pwm.h"
#include "pwm.h"
#include "util.h"

const enum ec_led_id supported_led_ids[] = {
	EC_LED_ID_POWER_LED,
};
const int supported_led_ids_count = ARRAY_SIZE(supported_led_ids);

/*
 * Board has one physical LED with red, green, and blue
 */
struct pwm_led_color_map led_color_map[EC_LED_COLOR_COUNT] = {
	/* Red, Green, Blue */
	[EC_LED_COLOR_RED] = { 100, 0, 0 },
	[EC_LED_COLOR_GREEN] = { 0, 100, 0 },
	[EC_LED_COLOR_BLUE] = { 0, 0, 100 },
	[EC_LED_COLOR_YELLOW] = { 50, 50, 0 },
	[EC_LED_COLOR_WHITE] = { 50, 50, 50 },
	[EC_LED_COLOR_AMBER] = { 70, 30, 0 },
};

/* One logical LED with red, green, and blue channels. */
struct pwm_led pwm_leds[CONFIG_LED_PWM_COUNT] = {
	{
		.ch0 = PWM_CH_LED_RED,
		.ch1 = PWM_CH_LED_GREEN,
		.ch2 = PWM_CH_LED_BLUE,
		.enable = &pwm_enable,
		.set_duty = &pwm_set_duty,
	},
};

void led_get_brightness_range(enum ec_led_id led_id, uint8_t *brightness_range)
{
	memset(brightness_range, '\0',
	       sizeof(*brightness_range) * EC_LED_COLOR_COUNT);
	brightness_range[EC_LED_COLOR_RED] = 100;
	brightness_range[EC_LED_COLOR_GREEN] = 100;
	brightness_range[EC_LED_COLOR_BLUE] = 100;
	brightness_range[EC_LED_COLOR_YELLOW] = 100;
	brightness_range[EC_LED_COLOR_WHITE] = 100;
	brightness_range[EC_LED_COLOR_AMBER] = 100;
}

int led_set_brightness(enum ec_led_id led_id, const uint8_t *brightness)
{
	enum pwm_led_id pwm_id;

	/* Convert ec_led_id to pwm_led_id. */
	if (led_id == EC_LED_ID_POWER_LED)
		pwm_id = PWM_LED0;
	else
		return EC_ERROR_UNKNOWN;

	if (brightness[EC_LED_COLOR_RED])
		set_pwm_led_color(pwm_id, EC_LED_COLOR_RED);
	else if (brightness[EC_LED_COLOR_GREEN])
		set_pwm_led_color(pwm_id, EC_LED_COLOR_GREEN);
	else if (brightness[EC_LED_COLOR_BLUE])
		set_pwm_led_color(pwm_id, EC_LED_COLOR_BLUE);
	else if (brightness[EC_LED_COLOR_YELLOW])
		set_pwm_led_color(pwm_id, EC_LED_COLOR_YELLOW);
	else if (brightness[EC_LED_COLOR_WHITE])
		set_pwm_led_color(pwm_id, EC_LED_COLOR_WHITE);
	else if (brightness[EC_LED_COLOR_AMBER])
		set_pwm_led_color(pwm_id, EC_LED_COLOR_AMBER);
	else
		/* Otherwise, the "color" is "off". */
		set_pwm_led_color(pwm_id, -1);

	return EC_SUCCESS;
}
