/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Battery LED and Power LED control for Big
 */

#include "gpio.h"
#include "hooks.h"
#include "battery.h"
#include "charge_state.h"
#include "chipset.h"
#include "led_common.h"
#include "util.h"

const enum ec_led_id supported_led_ids[] = {
	EC_LED_ID_BATTERY_LED, EC_LED_ID_POWER_LED};

const int supported_led_ids_count = ARRAY_SIZE(supported_led_ids);

enum led_color {
	LED_OFF = 0,
	LED_BLUE,
	LED_ORANGE,
	LED_COLOR_COUNT  /* Number of colors, not a color itself */
};

static int bat_led_set_color(enum led_color color)
{
	switch (color) {
	case LED_OFF:
		gpio_set_level(GPIO_CHARGING, 0);
		gpio_set_level(GPIO_BAT_LED1, 0);
		break;
	case LED_BLUE:
		gpio_set_level(GPIO_CHARGING, 0);
		gpio_set_level(GPIO_BAT_LED1, 1);
		break;
	case LED_ORANGE:
		gpio_set_level(GPIO_CHARGING, 1);
		gpio_set_level(GPIO_BAT_LED1, 0);
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
		gpio_set_level(GPIO_LED_POWER_L, 0);
		gpio_set_level(GPIO_PWR_LED0, 0);
		break;
	case LED_BLUE:
		gpio_set_level(GPIO_LED_POWER_L, 1);
		gpio_set_level(GPIO_PWR_LED0, 0);
		break;
	case LED_ORANGE:
		gpio_set_level(GPIO_LED_POWER_L, 0);
		gpio_set_level(GPIO_PWR_LED0, 1);
		break;
	default:
		return EC_ERROR_UNKNOWN;
	}
	return EC_SUCCESS;
}

void led_get_brightness_range(enum ec_led_id led_id, uint8_t *brightness_range)
{
	/* Ignoring led_id as both leds support the same colors */
	brightness_range[EC_LED_COLOR_BLUE] = 1;
	brightness_range[EC_LED_COLOR_YELLOW] = 1;
}

int led_set_brightness(enum ec_led_id led_id, const uint8_t *brightness)
{
	switch (led_id) {
	case EC_LED_ID_BATTERY_LED:
		if (brightness[EC_LED_COLOR_BLUE] != 0)
			bat_led_set_color(LED_BLUE);
		else if (brightness[EC_LED_COLOR_YELLOW] != 0)
			bat_led_set_color(LED_ORANGE);
		else
			bat_led_set_color(LED_OFF);
		break;
	case EC_LED_ID_POWER_LED:
		if (brightness[EC_LED_COLOR_BLUE] != 0)
			pwr_led_set_color(LED_BLUE);
		else if (brightness[EC_LED_COLOR_YELLOW] != 0)
			pwr_led_set_color(LED_ORANGE);
		else
			pwr_led_set_color(LED_OFF);
		break;
	default:
		return EC_ERROR_UNKNOWN;
	}
	return EC_SUCCESS;
}

static void big_led_set_power(void)
{
	static int power_second;

	power_second++;

	/* PWR LED behavior:
	 * Power on: Blue
	 * Suspend: Orange in breeze mode ( 1 sec on/ 3 sec off)
	 * Power off: OFF
	 */
	if (chipset_in_state(CHIPSET_STATE_ANY_OFF))
		pwr_led_set_color(LED_OFF);
	else if (chipset_in_state(CHIPSET_STATE_ON))
		pwr_led_set_color(LED_BLUE);
	else if (chipset_in_state(CHIPSET_STATE_SUSPEND))
		pwr_led_set_color((power_second & 3) ? LED_OFF : LED_ORANGE);
}

static void big_led_set_battery(void)
{
	static int battery_second;
	uint32_t chflags = charge_get_flags();

	battery_second++;

	/* BAT LED behavior:
	 * Fully charged / idle: Blue
	 * Force idle (for factory): 2 secs of blue, 2 secs of yellow
	 * Under charging: Orange
	 * Battery low (10%): Orange in breeze mode (1 sec on, 3 sec off)
	 * Battery critical low (less than 3%) or abnormal battery
	 *     situation: Orange in blinking mode (1 sec on, 1 sec off)
	 * Using battery or not connected to AC power: OFF
	 */
	switch (charge_get_state()) {
	case PWR_STATE_CHARGE:
		bat_led_set_color(LED_ORANGE);
		break;
	case PWR_STATE_DISCHARGE:
		if (charge_get_percent() < 3)
			bat_led_set_color((battery_second & 1)
					? LED_OFF : LED_ORANGE);
		else if (charge_get_percent() < 10)
			bat_led_set_color((battery_second & 3)
					? LED_OFF : LED_ORANGE);
		else
			bat_led_set_color(LED_OFF);
		break;
	case PWR_STATE_ERROR:
		bat_led_set_color((battery_second & 1) ? LED_OFF : LED_ORANGE);
		break;
	case PWR_STATE_CHARGE_NEAR_FULL:
		bat_led_set_color(LED_BLUE);
		break;
	case PWR_STATE_IDLE: /* External power connected in IDLE. */
		if (chflags & CHARGE_FLAG_FORCE_IDLE)
			bat_led_set_color(
				(battery_second & 0x2) ? LED_BLUE : LED_ORANGE);
		else
			bat_led_set_color(LED_BLUE);
		break;
	default:
		/* Other states don't alter LED behavior */
		break;
	}
}

/**  * Called by hook task every 1 sec  */
static void led_second(void)
{
	if (led_auto_control_is_enabled(EC_LED_ID_POWER_LED))
		big_led_set_power();
	if (led_auto_control_is_enabled(EC_LED_ID_BATTERY_LED))
		big_led_set_battery();
}
DECLARE_HOOK(HOOK_SECOND, led_second, HOOK_PRIO_DEFAULT);

