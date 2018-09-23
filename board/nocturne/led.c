/* Copyright 2018 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Nocturne specific PWM LED settings. */

#include "common.h"
#include "ec_commands.h"
#include "hooks.h"
#include "led_pwm.h"
#include "pwm.h"
#include "util.h"

const enum ec_led_id supported_led_ids[] = {
	EC_LED_ID_LEFT_LED,
	EC_LED_ID_RIGHT_LED,
};
const int supported_led_ids_count = ARRAY_SIZE(supported_led_ids);

struct pwm_led led_color_map_v3[EC_LED_COLOR_COUNT] = {
				/* Red, Green, Blue */
	[EC_LED_COLOR_RED]    = {  36,   0,   0 },
	[EC_LED_COLOR_GREEN]  = {   0,  15,   0 },
	[EC_LED_COLOR_BLUE]   = {   0,   0, 100 },
	[EC_LED_COLOR_YELLOW] = {  36,  15,   0 },
	[EC_LED_COLOR_WHITE]  = {  30,   9,  15 },
	[EC_LED_COLOR_AMBER]  = {  30,   1,   0 },
};

/* Map for board rev 2 */
struct pwm_led led_color_map_v2[EC_LED_COLOR_COUNT] = {
				/* Red, Green, Blue */
	[EC_LED_COLOR_RED]    = {  62,   0,   0 },
	[EC_LED_COLOR_GREEN]  = {   0,  31,   0 },
	[EC_LED_COLOR_BLUE]   = {   0,   0, 100 },
	[EC_LED_COLOR_YELLOW] = { 100,  54,   0 },
	[EC_LED_COLOR_WHITE]  = {  70,  54, 100 },
	[EC_LED_COLOR_AMBER]  = { 100,  15,   0 },
};

/* Map for board rev 0 and 1 */
struct pwm_led led_color_map_v0_1[EC_LED_COLOR_COUNT] = {
				/* Red, Green, Blue */
	[EC_LED_COLOR_RED]    = {   1,   0,   0 },
	[EC_LED_COLOR_GREEN]  = {   0,   1,   0 },
	[EC_LED_COLOR_BLUE]   = {   0,   0,   1 },
	[EC_LED_COLOR_YELLOW] = {   1,   1,   0 },
	[EC_LED_COLOR_WHITE]  = {   9,  15,  15 },
	[EC_LED_COLOR_AMBER]  = {  15,   1,   0 },
};

struct pwm_led led_color_map[EC_LED_COLOR_COUNT] = { { 0 } };

/* Two tri-color LEDs with red, green, and blue channels. */
struct pwm_led pwm_leds[CONFIG_LED_PWM_COUNT] = {
	{
		.ch0 = PWM_CH_DB0_LED_RED,
		.ch1 = PWM_CH_DB0_LED_GREEN,
		.ch2 = PWM_CH_DB0_LED_BLUE,
		.enable = &pwm_enable,
		.set_duty = &pwm_set_duty,
	},

	{
		.ch0 = PWM_CH_DB1_LED_RED,
		.ch1 = PWM_CH_DB1_LED_GREEN,
		.ch2 = PWM_CH_DB1_LED_BLUE,
		.enable = &pwm_enable,
		.set_duty = &pwm_set_duty,
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
	enum pwm_led_id pwm_id;

	/* Convert ec_led_id to pwm_led_id. */
	if (led_id == EC_LED_ID_LEFT_LED)
		pwm_id = PWM_LED0;
	else if (led_id == EC_LED_ID_RIGHT_LED)
		pwm_id = PWM_LED1;
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
	else if (brightness[EC_LED_COLOR_BLUE])
		set_pwm_led_color(pwm_id, EC_LED_COLOR_BLUE);
	else if (brightness[EC_LED_COLOR_WHITE])
		set_pwm_led_color(pwm_id, EC_LED_COLOR_WHITE);
	else
		/* Otherwise, the "color" is "off". */
		set_pwm_led_color(pwm_id, -1);

	return EC_SUCCESS;
}

static void fill_led_color_map(struct pwm_led map[])
{
	memcpy(led_color_map, map, EC_LED_COLOR_COUNT * sizeof(struct pwm_led));
}

static void select_color_map(void)
{
	switch (board_get_version()) {
	case 0:
	case 1:
		fill_led_color_map(led_color_map_v0_1);
		break;

	case 2:
		fill_led_color_map(led_color_map_v2);
		break;

	default:
		fill_led_color_map(led_color_map_v3);
		break;
	}
}
DECLARE_HOOK(HOOK_INIT, select_color_map, HOOK_PRIO_INIT_PWM-1);
