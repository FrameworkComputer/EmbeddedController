/* Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Power and battery LED control for Jacuzzi
 */

#include "common.h"
#include "driver/ioexpander/it8801.h"
#include "ec_commands.h"
#include "led_common.h"
#include "led_pwm.h"

const enum ec_led_id supported_led_ids[] = {
	EC_LED_ID_POWER_LED,
};
const int supported_led_ids_count = ARRAY_SIZE(supported_led_ids);

struct pwm_led led_color_map[EC_LED_COLOR_COUNT] = {
	[EC_LED_COLOR_RED] =    {5, 0, 0},
	[EC_LED_COLOR_GREEN] =  {0, 5, 0},
	[EC_LED_COLOR_BLUE] =   {0, 0, 5},
	[EC_LED_COLOR_YELLOW] = {5, 5, 0},
	[EC_LED_COLOR_WHITE] =  {2, 2, 2},
	[EC_LED_COLOR_AMBER] =  {5, 3, 0},
};

struct pwm_led pwm_leds[CONFIG_LED_PWM_COUNT] = {
	[PWM_LED0] = {
		.ch0 = PWM_CH_LED_RED,
		.ch1 = PWM_CH_LED_GREEN,
		.ch2 = PWM_CH_LED_BLUE,
		.enable = &it8801_pwm_enable,
		.set_duty = &it8801_pwm_set_duty,
	},
};

void led_get_brightness_range(enum ec_led_id led_id, uint8_t *brightness_range)
{
	brightness_range[EC_LED_COLOR_RED] = 100;
	brightness_range[EC_LED_COLOR_GREEN] = 100;
	brightness_range[EC_LED_COLOR_YELLOW] = 100;
	brightness_range[EC_LED_COLOR_AMBER] = 100;
	brightness_range[EC_LED_COLOR_BLUE] = 100;
	brightness_range[EC_LED_COLOR_WHITE] = 100;
}

int led_set_brightness(enum ec_led_id led_id, const uint8_t *brightness)
{
	if (brightness[EC_LED_COLOR_RED])
		set_pwm_led_color(PWM_LED0, EC_LED_COLOR_RED);
	else if (brightness[EC_LED_COLOR_GREEN])
		set_pwm_led_color(PWM_LED0, EC_LED_COLOR_GREEN);
	else if (brightness[EC_LED_COLOR_YELLOW])
		set_pwm_led_color(PWM_LED0, EC_LED_COLOR_YELLOW);
	else if (brightness[EC_LED_COLOR_AMBER])
		set_pwm_led_color(PWM_LED0, EC_LED_COLOR_AMBER);
	else if (brightness[EC_LED_COLOR_BLUE])
		set_pwm_led_color(PWM_LED0, EC_LED_COLOR_BLUE);
	else if (brightness[EC_LED_COLOR_WHITE])
		set_pwm_led_color(PWM_LED0, EC_LED_COLOR_WHITE);
	else
		/* Otherwise, the "color" is "off". */
		set_pwm_led_color(PWM_LED0, -1);

	return EC_SUCCESS;
}
