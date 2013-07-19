/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Power and battery LED control for Falco.
 */

#include "battery.h"
#include "charge_state.h"
#include "chipset.h"
#include "extpower.h"
#include "gpio.h"
#include "hooks.h"

enum led_color {
	LED_OFF = 0,
	LED_WHITE,
	LED_AMBER,
	LED_COLOR_COUNT  /* Number of colors, not a color itself */
};

static int bat_led_set_color(enum led_color color)
{
	switch (color) {
	case LED_OFF:
		gpio_set_level(GPIO_BAT_LED0, 1);
		gpio_set_level(GPIO_BAT_LED1, 1);
		break;
	case LED_WHITE:
		gpio_set_level(GPIO_BAT_LED0, 1);
		gpio_set_level(GPIO_BAT_LED1, 0);
		break;
	case LED_AMBER:
		gpio_set_level(GPIO_BAT_LED0, 0);
		gpio_set_level(GPIO_BAT_LED1, 1);
		break;
	default:
		return EC_ERROR_UNKNOWN;
	}
	return EC_SUCCESS;
}

static int pwr_led_set_color(enum led_color color)
{
	switch (color) {
	case LED_OFF:
		gpio_set_level(GPIO_PWR_LED_L, 1);
		break;
	case LED_WHITE:
		gpio_set_level(GPIO_PWR_LED_L, 0);
		break;
	default:
		return EC_ERROR_UNKNOWN;
	}
	return EC_SUCCESS;
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
		pwr_led_set_color(LED_WHITE);
	else if (chipset_in_state(CHIPSET_STATE_SUSPEND))
		pwr_led_set_color((ticks & 0x4) ? LED_WHITE : LED_OFF);

	/* Battery LED is solid white if AC connected, unless the battery is
	 * is charging or there is an error. */
	bat_led_set_color(extpower_is_present() ? LED_WHITE : LED_OFF);

	switch (charge_get_state()) {
	case PWR_STATE_CHARGE:
		bat_led_set_color(LED_AMBER);
		break;
	case PWR_STATE_DISCHARGE:
		if (charge_get_percent() <= BATTERY_LEVEL_LOW)
			bat_led_set_color((ticks & 0x4) ? LED_WHITE : LED_OFF);
		break;
	case PWR_STATE_ERROR:
		bat_led_set_color((ticks & 0x2) ? LED_WHITE : LED_OFF);
		break;
	case PWR_STATE_IDLE:
		if (chflags & CHARGE_FLAG_FORCE_IDLE)
			bat_led_set_color((ticks & 0x4) ? LED_AMBER : LED_OFF);
		break;
	default:
		/* Other states don't alter LED behavior */
		break;
	}
}
DECLARE_HOOK(HOOK_TICK, led_tick, HOOK_PRIO_DEFAULT);

