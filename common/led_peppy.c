/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Power and battery LED control for Peppy.
 */

#include "battery.h"
#include "charge_state.h"
#include "chipset.h"
#include "gpio.h"
#include "hooks.h"

#define LED_TOTAL_TICKS 16
#define LED_ON_TICKS 4

enum led_color {
	LED_OFF = 0,
	LED_BLUE,
	LED_AMBER,
	LED_COLOR_COUNT  /* Number of colors, not a color itself */
};

static int led_set_color(enum led_color color, enum gpio_signal gpio_led_blue_l,
			 enum gpio_signal gpio_led_amber_l)
{
	switch (color) {
	case LED_OFF:
		gpio_set_level(gpio_led_blue_l,  1);
		gpio_set_level(gpio_led_amber_l, 1);
		break;
	case LED_BLUE:
		gpio_set_level(gpio_led_blue_l,  0);
		gpio_set_level(gpio_led_amber_l, 1);
		break;
	case LED_AMBER:
		gpio_set_level(gpio_led_blue_l,  1);
		gpio_set_level(gpio_led_amber_l, 0);
		break;
	default:
		return EC_ERROR_UNKNOWN;
	}
	return EC_SUCCESS;
}

static int bat_led_set_color(enum led_color color)
{
	return led_set_color(color, GPIO_BAT_LED0_L, GPIO_BAT_LED1_L);
}

static int pwr_led_set_color(enum led_color color)
{
	return led_set_color(color, GPIO_PWR_LED0_L, GPIO_PWR_LED1_L);
}

/* Called by hook task every 250mSec */
static void led_tick(void)
{
	static int ticks;
	uint32_t chflags = charge_get_flags();

	ticks++;

	if (chipset_in_state(CHIPSET_STATE_ANY_OFF))
		pwr_led_set_color(LED_OFF);
	else if (chipset_in_state(CHIPSET_STATE_ON))
		pwr_led_set_color(LED_BLUE);
	else if (chipset_in_state(CHIPSET_STATE_SUSPEND))
		/* Blink once every four seconds. */
		pwr_led_set_color((ticks % LED_TOTAL_TICKS < LED_ON_TICKS) ?
			LED_AMBER : LED_OFF);

	switch (charge_get_state()) {
	case PWR_STATE_CHARGE:
		bat_led_set_color(LED_AMBER);
		break;
	case PWR_STATE_CHARGE_NEAR_FULL:
		bat_led_set_color(LED_BLUE);
		break;
	case PWR_STATE_DISCHARGE:
		bat_led_set_color(LED_OFF);
		break;
	case PWR_STATE_ERROR:
		bat_led_set_color((ticks & 0x2) ? LED_AMBER : LED_OFF);
		break;
	case PWR_STATE_IDLE: /* External power connected in IDLE state. */
		if (chflags & CHARGE_FLAG_FORCE_IDLE)
			bat_led_set_color((ticks & 0x4) ? LED_BLUE : LED_OFF);
		else
			bat_led_set_color(LED_BLUE);
		break;
	default:
		/* Other states don't alter LED behavior */
		break;
	}
}
DECLARE_HOOK(HOOK_TICK, led_tick, HOOK_PRIO_DEFAULT);

