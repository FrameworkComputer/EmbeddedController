/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Battery LED and Power LED control for jerry
 */

#include "gpio.h"
#include "hooks.h"
#include "battery.h"
#include "charge_state.h"
#include "chipset.h"
#include "led_common.h"
#include "util.h"

const enum ec_led_id supported_led_ids[] = {
	EC_LED_ID_BATTERY_LED,
	EC_LED_ID_POWER_LED,
};

const int supported_led_ids_count = ARRAY_SIZE(supported_led_ids);

enum led_color {
	LED_GREEN = 0,
	LED_ORANGE,
	LED_COLOR_COUNT  /* Number of colors, not a color itself */
};

static int bat_led_set(enum led_color color, int on)
{
	switch (color) {
	case LED_GREEN:
		gpio_set_level(GPIO_BAT_LED1, on ? 0 : 1);
		break;
	case LED_ORANGE:
		gpio_set_level(GPIO_BAT_LED0, on ? 0 : 1);
		break;
	default:
		return EC_ERROR_UNKNOWN;
	}
	return EC_SUCCESS;
}

static int pwr_led_set(int on)
{
	gpio_set_level(GPIO_POWER_LED, on ? 0 : 1);
	return EC_SUCCESS;
}

void led_get_brightness_range(enum ec_led_id led_id, uint8_t *brightness_range)
{
	/* Ignoring led_id as both leds support the same colors */
	brightness_range[EC_LED_COLOR_GREEN] = 1;
	brightness_range[EC_LED_COLOR_YELLOW] = 1;
}

int led_set_brightness(enum ec_led_id led_id, const uint8_t *brightness)
{
	switch (led_id) {
	case EC_LED_ID_BATTERY_LED:
		if (brightness[EC_LED_COLOR_GREEN] != 0) {
			bat_led_set(LED_GREEN, 1);
			bat_led_set(LED_ORANGE, 0);
		} else if (brightness[EC_LED_COLOR_YELLOW] != 0) {
			bat_led_set(LED_GREEN, 1);
			bat_led_set(LED_ORANGE, 1);
		} else {
			bat_led_set(LED_GREEN, 0);
			bat_led_set(LED_ORANGE, 0);
		}
		break;
	case EC_LED_ID_POWER_LED:
		pwr_led_set(brightness[EC_LED_COLOR_BLUE]);
		break;
	default:
		return EC_ERROR_UNKNOWN;

	}
	return EC_SUCCESS;
}

static void jerry_led_set_power(void)
{
	static int power_second;

	power_second++;

	/* PWR LED behavior:
	 * Power on: Green
	 * Suspend: Green in breeze mode ( 1 sec on/ 3 sec off)
	 * Power off: OFF
	 */
	if (chipset_in_state(CHIPSET_STATE_ANY_OFF))
		pwr_led_set(0);
	else if (chipset_in_state(CHIPSET_STATE_ON))
		pwr_led_set(1);
	else if (chipset_in_state(CHIPSET_STATE_SUSPEND))
		pwr_led_set((power_second & 3) ? 0 : 1);
}


static void jerry_led_set_battery(void)
{
	static int battery_second;

	battery_second++;

	/* BAT LED behavior:
	 * Fully charged / idle: Off
	 * Under charging: Orange
	 * Battery low (10%): Orange in breeze mode (1 sec on, 3 sec off)
	 * Battery critical low (less than 3%) or abnormal battery
	 *     situation: Orange in blinking mode (1 sec on, 1 sec off)
	 * Using battery or not connected to AC power: OFF
	 */
	switch (charge_get_state()) {
	case PWR_STATE_CHARGE:
		bat_led_set(LED_ORANGE, 1);
		break;
	case PWR_STATE_CHARGE_NEAR_FULL:
		bat_led_set(LED_ORANGE, 1);
		break;
	case PWR_STATE_DISCHARGE:
		if (charge_get_percent() < 3)
			bat_led_set(LED_ORANGE, (battery_second & 1) ? 0 : 1);
		else if (charge_get_percent() < 10)
			bat_led_set(LED_ORANGE, (battery_second & 3) ? 0 : 1);
		else
			bat_led_set(LED_ORANGE, 0);
		break;
	case PWR_STATE_ERROR:
		bat_led_set(LED_ORANGE, (battery_second & 1) ? 0 : 1);
		break;
	case PWR_STATE_IDLE: /* External power connected in IDLE. */
		bat_led_set(LED_ORANGE, 0);
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
		jerry_led_set_power();
	if (led_auto_control_is_enabled(EC_LED_ID_BATTERY_LED))
		jerry_led_set_battery();
}
DECLARE_HOOK(HOOK_SECOND, led_second, HOOK_PRIO_DEFAULT);

