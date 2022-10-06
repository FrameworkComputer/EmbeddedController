/* Copyright 2020 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Power and battery LED control for Volteer
 */

#include "charge_manager.h"
#include "common.h"
#include "ec_commands.h"
#include "hooks.h"
#include "led_common.h"
#include "led_pwm.h"
#include "pwm.h"

const enum ec_led_id supported_led_ids[] = {
	EC_LED_ID_POWER_LED,
};
const int supported_led_ids_count = ARRAY_SIZE(supported_led_ids);

struct pwm_led_color_map led_color_map[EC_LED_COLOR_COUNT] = {
	/* Red, Green, Blue */
	[EC_LED_COLOR_RED] = { 100, 0, 0 },
	[EC_LED_COLOR_GREEN] = { 0, 100, 0 },
	[EC_LED_COLOR_BLUE] = { 0, 0, 100 },
	/* The green LED seems to be brighter than the others, so turn down
	 * green from its natural level for these secondary colors.
	 */
	[EC_LED_COLOR_YELLOW] = { 100, 70, 0 },
	[EC_LED_COLOR_WHITE] = { 100, 70, 100 },
	[EC_LED_COLOR_AMBER] = { 100, 20, 0 },
};

struct pwm_led pwm_leds[] = {
	/* 2 RGB diffusers controlled by 1 set of 3 channels. */
	[PWM_LED0] = {
		.ch0 = PWM_CH_LED3_RED,
		.ch1 = PWM_CH_LED2_GREEN,
		.ch2 = PWM_CH_LED1_BLUE,
		.enable = &pwm_enable,
		.set_duty = &pwm_set_duty,
	},
};

void led_get_brightness_range(enum ec_led_id led_id, uint8_t *brightness_range)
{
	brightness_range[EC_LED_COLOR_RED] = 255;
	brightness_range[EC_LED_COLOR_GREEN] = 255;
	brightness_range[EC_LED_COLOR_BLUE] = 255;
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

/* Illuminates the LED on the side of the active charging port. If not charging,
 * illuminates both LEDs.
 */
static void led_set_charge_port_tick(void)
{
	int port;
	int side_select_duty;

	port = charge_manager_get_active_charge_port();
	switch (port) {
	case 0:
		side_select_duty = 100;
		break;
	case 1:
		side_select_duty = 0;
		break;
	default:
		side_select_duty = 50;
	}

	if (led_auto_control_is_enabled(EC_LED_ID_POWER_LED))
		pwm_set_duty(PWM_CH_LED4_SIDESEL, side_select_duty);
}
DECLARE_HOOK(HOOK_TICK, led_set_charge_port_tick, HOOK_PRIO_DEFAULT);

static void board_led_init(void)
{
	/* Illuminate motherboard and daughter board LEDs equally to start. */
	pwm_enable(PWM_CH_LED4_SIDESEL, 1);
	pwm_set_duty(PWM_CH_LED4_SIDESEL, 50);
}
DECLARE_HOOK(HOOK_INIT, board_led_init, HOOK_PRIO_DEFAULT);
