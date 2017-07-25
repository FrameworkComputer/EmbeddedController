/* Copyright 2017 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Battery LED control for Nefario board.
 */

#include "battery.h"
#include "charge_state.h"
#include "chipset.h"
#include "hooks.h"
#include "led_common.h"
#include "pwm.h"
#include "util.h"

const enum ec_led_id supported_led_ids[] = { EC_LED_ID_BATTERY_LED };

const int supported_led_ids_count = ARRAY_SIZE(supported_led_ids);

enum led_color {
	LED_OFF = 0,
	LED_RED,
	LED_AMBER,
	LED_GREEN,
	LED_COLOR_COUNT  /* Number of colors, not a color itself */
};

/* Color v.s. brightness */
static const uint8_t color_brightness[LED_COLOR_COUNT][2] = {
	[LED_OFF]   = {100, 100},
	[LED_RED]   = {80,  100},
	[LED_AMBER] = {80,  80},
	[LED_GREEN] = {100, 80},
};

static void bat_led_set_color(enum led_color color)
{
	pwm_set_duty(PWM_CH_LED_RED, color_brightness[color][0]);
	pwm_set_duty(PWM_CH_LED_GREEN, color_brightness[color][1]);
}

static void nefario_led_set_battery(void)
{
	static int battery_second;
	uint32_t chflags = charge_get_flags();

	battery_second++;

	switch (charge_get_state()) {
	case PWR_STATE_CHARGE:
		bat_led_set_color(LED_AMBER);
		break;
	case PWR_STATE_DISCHARGE:
		if (charge_get_percent() < 3)
			bat_led_set_color((battery_second & 1)
					? LED_OFF : LED_AMBER);
		else if (charge_get_percent() < 10)
			bat_led_set_color((battery_second & 3)
					? LED_OFF : LED_AMBER);
		else if (charge_get_percent() >= BATTERY_LEVEL_NEAR_FULL &&
		    (chflags & CHARGE_FLAG_EXTERNAL_POWER))
			bat_led_set_color(LED_GREEN);
		else
			bat_led_set_color(LED_OFF);
		break;
	case PWR_STATE_ERROR:
		bat_led_set_color(LED_RED);
		break;
	case PWR_STATE_CHARGE_NEAR_FULL:
		bat_led_set_color(LED_GREEN);
		break;
	case PWR_STATE_IDLE: /* External power connected in IDLE. */
		if (chflags & CHARGE_FLAG_FORCE_IDLE)
			bat_led_set_color(
				(battery_second & 0x2) ? LED_GREEN : LED_AMBER);
		else
			bat_led_set_color(LED_GREEN);
		break;
	default:
		/* Other states don't alter LED behavior */
		break;
	}
}

void led_get_brightness_range(enum ec_led_id led_id, uint8_t *brightness_range)
{
	if (led_id == EC_LED_ID_BATTERY_LED) {
		brightness_range[EC_LED_COLOR_RED] = 100;
		brightness_range[EC_LED_COLOR_GREEN] = 100;
	}
}

int led_set_brightness(enum ec_led_id led_id, const uint8_t *brightness)
{
	if (led_id == EC_LED_ID_BATTERY_LED) {
		pwm_set_duty(PWM_CH_LED_RED, brightness[EC_LED_COLOR_RED]);
		pwm_set_duty(PWM_CH_LED_GREEN, brightness[EC_LED_COLOR_GREEN]);
		return EC_SUCCESS;
	}
	return EC_ERROR_UNKNOWN;
}

static void led_init(void)
{
	pwm_enable(PWM_CH_LED_RED, 1);
	pwm_enable(PWM_CH_LED_GREEN, 1);
	bat_led_set_color(LED_OFF);
}
/* After pwm_pin_init() */
DECLARE_HOOK(HOOK_INIT, led_init, HOOK_PRIO_DEFAULT);

/* Called by hook task every 1 sec */
static void led_second(void)
{
	if (led_auto_control_is_enabled(EC_LED_ID_BATTERY_LED))
		nefario_led_set_battery();
}
DECLARE_HOOK(HOOK_SECOND, led_second, HOOK_PRIO_DEFAULT);
