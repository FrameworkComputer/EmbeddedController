/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Power and battery LED control for Asurada
 */
#include "charge_manager.h"
#include "charge_state.h"
#include "common.h"
#include "console.h"
#include "driver/bc12/mt6360.h"
#include "hooks.h"
#include "led_common.h"
#include "pwm.h"
#include "stdbool.h"
#include "util.h"

#define CPRINTS(format, args...) cprints(CC_PWM, format, ## args)

#define LED_OFF EC_LED_COLOR_COUNT

const enum ec_led_id supported_led_ids[] = {
	/* Main LED */
	EC_LED_ID_LEFT_LED,
	EC_LED_ID_RIGHT_LED,

	/* Not used, give them some random name for testing */
	EC_LED_ID_POWER_LED,
	EC_LED_ID_BATTERY_LED
};
const int supported_led_ids_count = ARRAY_SIZE(supported_led_ids);

static void led_set_color_left(enum ec_led_colors color, int duty)
{
	mt6360_led_set_brightness(MT6360_LED_RGB2, duty);
	mt6360_led_set_brightness(MT6360_LED_RGB3, duty);

	switch (color) {
	case EC_LED_COLOR_AMBER:
		mt6360_led_enable(MT6360_LED_RGB2, 0);
		mt6360_led_enable(MT6360_LED_RGB3, 1);
		break;
	case EC_LED_COLOR_WHITE:
		mt6360_led_enable(MT6360_LED_RGB2, 1);
		mt6360_led_enable(MT6360_LED_RGB3, 0);
		break;
	default: /* LED_OFF and other unsupported colors */
		mt6360_led_enable(MT6360_LED_RGB2, 0);
		mt6360_led_enable(MT6360_LED_RGB3, 0);
		break;
	}
}

static void led_set_color_right(enum ec_led_colors color, int duty)
{
	pwm_set_duty(PWM_CH_LED2, duty);
	pwm_set_duty(PWM_CH_LED3, duty);

	switch (color) {
	case EC_LED_COLOR_AMBER:
		pwm_enable(PWM_CH_LED2, 0);
		pwm_enable(PWM_CH_LED3, 1);
		break;
	case EC_LED_COLOR_WHITE:
		pwm_enable(PWM_CH_LED2, 1);
		pwm_enable(PWM_CH_LED3, 0);
		break;
	default: /* LED_OFF and other unsupported colors */
		pwm_enable(PWM_CH_LED2, 0);
		pwm_enable(PWM_CH_LED3, 0);
		break;
	}
}

static void led_set_color_power(enum ec_led_colors color, int duty)
{
	pwm_set_duty(PWM_CH_LED1, duty);
	pwm_enable(PWM_CH_LED1, color == EC_LED_COLOR_WHITE);
}

static void led_set_color_battery(enum ec_led_colors color, int duty)
{
	mt6360_led_set_brightness(MT6360_LED_RGB1, duty);
	mt6360_led_enable(MT6360_LED_RGB1, color == EC_LED_COLOR_WHITE);
}

static enum ec_error_list set_color(enum ec_led_id led_id,
				    enum ec_led_colors color,
				    int duty)
{
	switch (led_id) {
	case EC_LED_ID_LEFT_LED:
		led_set_color_left(color, duty);
		return EC_SUCCESS;
	case EC_LED_ID_RIGHT_LED:
		led_set_color_right(color, duty);
		return EC_SUCCESS;
	case EC_LED_ID_POWER_LED:
		led_set_color_power(color, duty);
		return EC_SUCCESS;
	case EC_LED_ID_BATTERY_LED:
		led_set_color_battery(color, duty);
		return EC_SUCCESS;
	default:
		return EC_ERROR_INVAL;
	}
}

void led_get_brightness_range(enum ec_led_id led_id, uint8_t *brightness_range)
{
	switch (led_id) {
	case EC_LED_ID_LEFT_LED:
		brightness_range[EC_LED_COLOR_AMBER] =
			MT6360_LED_BRIGHTNESS_MAX;
		brightness_range[EC_LED_COLOR_WHITE] =
			MT6360_LED_BRIGHTNESS_MAX;
		break;
	case EC_LED_ID_RIGHT_LED:
		brightness_range[EC_LED_COLOR_AMBER] = 100;
		brightness_range[EC_LED_COLOR_WHITE] = 100;
		break;
	case EC_LED_ID_POWER_LED:
		brightness_range[EC_LED_COLOR_WHITE] = 100;
		break;
	case EC_LED_ID_BATTERY_LED:
		brightness_range[EC_LED_COLOR_WHITE] =
			MT6360_LED_BRIGHTNESS_MAX;
		break;
	default:
		break;
	}
}

int led_set_brightness(enum ec_led_id led_id, const uint8_t *brightness)
{
	if (brightness[EC_LED_COLOR_AMBER])
		return set_color(led_id, EC_LED_COLOR_AMBER,
				 brightness[EC_LED_COLOR_AMBER]);
	if (brightness[EC_LED_COLOR_WHITE])
		return set_color(led_id, EC_LED_COLOR_WHITE,
				 brightness[EC_LED_COLOR_WHITE]);

	return set_color(led_id, LED_OFF, 0);
}

static void update_led(enum ec_led_id led_id, bool is_active_charge_port,
		       int duty, int tick)
{
	enum charge_state power_state = charge_get_state();

	if (power_state == PWR_STATE_IDLE) {
		/* Factory mode: blinking white (2sec on + 2sec off) */
		set_color(led_id, (tick % 8 < 4) ? EC_LED_COLOR_WHITE : LED_OFF,
			  duty);
	} else if (power_state == PWR_STATE_ERROR) {
		/* Battery error: blinking amber (1sec on + 1sec off) */
		set_color(led_id, (tick % 4 < 2) ? EC_LED_COLOR_AMBER : LED_OFF,
			  duty);
	} else if (is_active_charge_port) {
		/*
		 * Active charge port: amber when charging, white if fully
		 * charged.
		 */
		if (power_state == PWR_STATE_CHARGE)
			set_color(led_id, EC_LED_COLOR_AMBER, duty);
		else
			set_color(led_id, EC_LED_COLOR_WHITE, duty);
	} else {
		/*
		 * Non-active port:
		 * Solid white in S0, blinking amber (3sec on + 1sec off) in S3,
		 * and LED off in S5
		 */
		if (chipset_in_state(CHIPSET_STATE_ON))
			set_color(led_id, EC_LED_COLOR_WHITE, duty);
		else if (chipset_in_state(CHIPSET_STATE_ANY_SUSPEND))
			set_color(
				led_id,
				(tick % 8 < 6) ? EC_LED_COLOR_AMBER : LED_OFF,
				duty);
		else
			set_color(led_id, LED_OFF, 0);

	}
}

static void led_tick(void)
{
	static int tick;
	int port = charge_manager_get_active_charge_port();

	++tick;
	/* Pick duty 1 and 50 respectively to have same brightness */
	if (led_auto_control_is_enabled(EC_LED_ID_LEFT_LED))
		update_led(EC_LED_ID_LEFT_LED, port == 0, 1, tick);
	if (led_auto_control_is_enabled(EC_LED_ID_RIGHT_LED))
		update_led(EC_LED_ID_RIGHT_LED, port == 1, 50, tick);
	/* Turn off unused LEDs */
	if (led_auto_control_is_enabled(EC_LED_ID_POWER_LED))
		set_color(EC_LED_ID_POWER_LED, LED_OFF, 0);
	if (led_auto_control_is_enabled(EC_LED_ID_BATTERY_LED))
		set_color(EC_LED_ID_BATTERY_LED, LED_OFF, 0);
}
DECLARE_HOOK(HOOK_TICK, led_tick, HOOK_PRIO_DEFAULT);
DECLARE_HOOK(HOOK_INIT, led_tick, HOOK_PRIO_DEFAULT);
