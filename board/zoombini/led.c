/* Copyright 2017 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Zoombini/Meowth specific LED settings. */

#include "common.h"
#include "ec_commands.h"
#include "led_pwm.h"
#include "pwm.h"
#include "util.h"

const enum ec_led_id supported_led_ids[] = {
#ifdef BOARD_MEOWTH
	EC_LED_ID_LEFT_LED,
	EC_LED_ID_RIGHT_LED,
#else
	EC_LED_ID_POWER_LED,
#endif /* defined(BOARD_MEOWTH) */
};
const int supported_led_ids_count = ARRAY_SIZE(supported_led_ids);

#ifdef BOARD_MEOWTH
/* Meowth LED definitions */
/* We won't be using the blue channel long term. */
struct pwm_led led_color_map[EC_LED_COLOR_COUNT] = {
				/* Red, Green, Blue */
	[EC_LED_COLOR_RED]    = {   8,   0,   0 },
	[EC_LED_COLOR_GREEN]  = {   0,   8,   0 },
	[EC_LED_COLOR_BLUE]   = {   0,   0,   0 },
	[EC_LED_COLOR_YELLOW] = {   8,  24,   0 },
	[EC_LED_COLOR_WHITE]  = {   0,   0,   0 },
	[EC_LED_COLOR_AMBER]  = {  12,   9,   0 },
};

/* Two tri-color LEDs with red, green, and blue channels. */
struct pwm_led pwm_leds[CONFIG_LED_PWM_COUNT] = {
	{
		PWM_CH_DB0_LED_RED,
		PWM_CH_DB0_LED_GREEN,
		PWM_CH_DB0_LED_BLUE,
	},

	{
		PWM_CH_DB1_LED_RED,
		PWM_CH_DB1_LED_GREEN,
		PWM_CH_DB1_LED_BLUE,
	},
};
#else
/* Zoombini LED definitions. */
struct pwm_led led_color_map[EC_LED_COLOR_COUNT] = {
				/* Red, Green, Blue */
	[EC_LED_COLOR_RED]    = { 100,   0,   0 },
	[EC_LED_COLOR_GREEN]  = {   0, 100,   0 },
	[EC_LED_COLOR_BLUE]   = {   0,   0,   0 },
	[EC_LED_COLOR_YELLOW] = { 100,  50,   0 },
	[EC_LED_COLOR_WHITE]  = {   0,   0,   0 },
	[EC_LED_COLOR_AMBER]  = { 100,  10,   0 },
};

/* A single bi-color LED with red and green channels. */
struct pwm_led pwm_leds[CONFIG_LED_PWM_COUNT] = {
	{
		.ch0 = PWM_CH_LED_RED,
		.ch1 = PWM_CH_LED_GREEN,
		.ch2 = PWM_LED_NO_CHANNEL,
	},
};
#endif /* !defined(BOARD_MEOWTH) */

void led_get_brightness_range(enum ec_led_id led_id, uint8_t *brightness_range)
{
	brightness_range[EC_LED_COLOR_RED] = 100;
	brightness_range[EC_LED_COLOR_GREEN] = 100;
	brightness_range[EC_LED_COLOR_YELLOW] = 100;
	brightness_range[EC_LED_COLOR_AMBER] = 100;
	/* Zoombini has no blue channel; it's also going away for Meowth. */
	brightness_range[EC_LED_COLOR_BLUE] = 0;
	brightness_range[EC_LED_COLOR_WHITE] = 0;
}

int led_set_brightness(enum ec_led_id led_id, const uint8_t *brightness)
{
	enum pwm_led_id pwm_id;

	/* Convert ec_led_id to pwm_led_id. */
#ifdef BOARD_MEOWTH
	if (led_id == EC_LED_ID_LEFT_LED)
		pwm_id = PWM_LED0;
	else if (led_id == EC_LED_ID_RIGHT_LED)
		pwm_id = PWM_LED1;
#else
	if (led_id == EC_LED_ID_POWER_LED)
		pwm_id = PWM_LED0;
#endif /* defined(BOARD_MEOWTH) */
	else
		return EC_ERROR_UNKNOWN;

	if (brightness[EC_LED_COLOR_RED])
		set_pwm_led_color(pwm_id, EC_LED_COLOR_RED);
	else if (brightness[EC_LED_COLOR_GREEN])
		set_pwm_led_color(pwm_id, EC_LED_COLOR_GREEN);
	else if (brightness[EC_LED_COLOR_YELLOW])
		set_pwm_led_color(pwm_id, EC_LED_COLOR_YELLOW);
	else if (brightness[EC_LED_COLOR_AMBER])
		set_pwm_led_color(pwm_id, EC_LED_COLOR_AMBER);
	else
		/* Otherwise, the "color" is "off". */
		set_pwm_led_color(pwm_id, -1);

	return EC_SUCCESS;
}
