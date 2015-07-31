/* Copyright 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Battery LED and Power LED control for LLAMA Board.
 */

#include "battery.h"
#include "charge_state.h"
#include "chipset.h"
#include "gpio.h"
#include "hooks.h"
#include "led_common.h"
#include "util.h"
#include "system.h"

const enum ec_led_id supported_led_ids[] = {
	EC_LED_ID_BATTERY_LED
};

const int supported_led_ids_count = ARRAY_SIZE(supported_led_ids);

enum led_color {
	BAT_LED_GREEN = 0,
	BAT_LED_ORANGE,
	BAT_LED_RED,
	BAT_LED_AMBER,
	PWR_LED_GREEN,
	PWR_LED_ORANGE,
	LED_COLOR_COUNT		/* Number of colors, not a color itself */
};

static int bat_led_set(enum led_color color, int on)
{
	switch (color) {
	case BAT_LED_GREEN:
		gpio_set_level(GPIO_BAT_LED0, on ? 0 : 1); /* BAT_LED_GREEN */
		break;
	case BAT_LED_ORANGE:
		/* for rev2 or before */
		gpio_set_level(GPIO_BAT_LED1, on ? 0 : 1); /* BAT_LED_ORANGE */
		break;
	case BAT_LED_RED:
		/* for rev3 or later */
		gpio_set_level(GPIO_BAT_LED1, on ? 0 : 1); /* BAT_LED_RED */
		break;
	case BAT_LED_AMBER:
		/* for rev3 or later */
		gpio_set_level(GPIO_BAT_LED0, on ? 0 : 1); /* BAT_LED_AMBER */
		gpio_set_level(GPIO_BAT_LED1, on ? 0 : 1);
		break;
	case PWR_LED_GREEN:
		gpio_set_level(GPIO_PWR_LED0, on ? 0 : 1); /* PWR_LED_GREEN */
		break;
	case PWR_LED_ORANGE:
		gpio_set_level(GPIO_PWR_LED1, on ? 0 : 1); /* PWR_LED_ORANGE */
		break;
	default:
		return EC_ERROR_UNKNOWN;
	}
	return EC_SUCCESS;
}

void led_get_brightness_range(enum ec_led_id led_id, uint8_t *brightness_range)
{
	/* Ignoring led_id as both leds support the same colors */
	brightness_range[EC_LED_COLOR_RED] = 1;
	brightness_range[EC_LED_COLOR_GREEN] = 1;
	brightness_range[EC_LED_COLOR_YELLOW] = 1;
}

int led_set_brightness(enum ec_led_id led_id, const uint8_t *brightness)
{
	if (EC_LED_ID_BATTERY_LED == led_id) {
		if (brightness[EC_LED_COLOR_GREEN] != 0) {
			bat_led_set(BAT_LED_GREEN, 1);
			bat_led_set(BAT_LED_ORANGE, 0);
		} else if (brightness[EC_LED_COLOR_YELLOW] != 0) {
			bat_led_set(BAT_LED_GREEN, 1);
			bat_led_set(BAT_LED_ORANGE, 1);
		} else if (brightness[EC_LED_COLOR_RED] != 0) {
			bat_led_set(BAT_LED_GREEN, 0);
			bat_led_set(BAT_LED_RED, 1);
		} else {
			bat_led_set(BAT_LED_GREEN, 0);
			bat_led_set(BAT_LED_ORANGE, 0);
		}
		return EC_SUCCESS;
	} else if (EC_LED_ID_POWER_LED == led_id) {
		if (brightness[EC_LED_COLOR_GREEN] != 0) {
			bat_led_set(PWR_LED_GREEN, 1);
			bat_led_set(PWR_LED_ORANGE, 0);
		} else if (brightness[EC_LED_COLOR_YELLOW] != 0) {
			bat_led_set(PWR_LED_GREEN, 1);
			bat_led_set(PWR_LED_ORANGE, 1);
		} else {
			bat_led_set(PWR_LED_GREEN, 0);
			bat_led_set(PWR_LED_ORANGE, 0);
		}
		return EC_SUCCESS;
	} else {
		return EC_ERROR_UNKNOWN;
	}
}

static void oak_led_set_power(int board_version)
{
	static int power_second;

	power_second++;

	if (board_version < 3) {
		/* PWR LED behavior:
		 * Power on: Green
		 * Suspend: Green in breeze mode ( 1 sec on/ 3 sec off)
		 * Power off: OFF
		 */
		if (chipset_in_state(CHIPSET_STATE_ANY_OFF))
			bat_led_set(BAT_LED_GREEN, 0);
		else if (chipset_in_state(CHIPSET_STATE_ON))
			bat_led_set(BAT_LED_GREEN, 1);
		else if (chipset_in_state(CHIPSET_STATE_SUSPEND))
			bat_led_set(BAT_LED_GREEN, (power_second & 3) ? 0 : 1);
	} else {
		/*
		 * For Rev3 or later version:
		 * PWR LED behavior:
		 * Power on: Green ON
		 * Suspend: Orange in breeze mode ( 1 sec on/ 3 sec off)
		 * Power off: OFF
		 */
		if (chipset_in_state(CHIPSET_STATE_ANY_OFF)) {
			bat_led_set(PWR_LED_GREEN, 0);
			bat_led_set(PWR_LED_ORANGE, 0);
		} else if (chipset_in_state(CHIPSET_STATE_ON)) {
			bat_led_set(PWR_LED_GREEN, 1);
			bat_led_set(PWR_LED_ORANGE, 0);
		} else if (chipset_in_state(CHIPSET_STATE_SUSPEND)) {
			bat_led_set(PWR_LED_GREEN, 0);
			bat_led_set(PWR_LED_ORANGE, (power_second & 3) ? 0 : 1);
		}
	}
}

static void oak_led_set_battery(int board_version)
{
	static int battery_second;

	battery_second++;

	if (board_version < 3) {
		/* BAT LED behavior:
		 * Fully charged / idle: Off
		 * Under charging: Orange
		 * Battery low (10%): Orange in breeze mode(1 sec on, 3 sec off)
		 * Battery critical low (less than 3%) or abnormal battery
		 *     situation: Orange in blinking mode (1 sec on, 1 sec off)
		 * Using battery or not connected to AC power: OFF
		 */
		switch (charge_get_state()) {
		case PWR_STATE_CHARGE:
			bat_led_set(BAT_LED_ORANGE, 1);
			break;
		case PWR_STATE_CHARGE_NEAR_FULL:
			bat_led_set(BAT_LED_ORANGE, 1);
			break;
		case PWR_STATE_DISCHARGE:
			if (charge_get_percent() < 3)
				bat_led_set(BAT_LED_ORANGE,
					    (battery_second & 1) ? 0 : 1);
			else if (charge_get_percent() < 10)
				bat_led_set(BAT_LED_ORANGE,
					    (battery_second & 3) ? 0 : 1);
			else
				bat_led_set(BAT_LED_ORANGE, 0);
			break;
		case PWR_STATE_ERROR:
			bat_led_set(BAT_LED_ORANGE,
				    (battery_second & 1) ? 0 : 1);
			break;
		case PWR_STATE_IDLE:	/* External power connected in IDLE. */
			bat_led_set(BAT_LED_ORANGE, 0);
			break;
		default:
			/* Other states don't alter LED behavior */
			break;
		}
	} else {
		/*
		 * For Rev3 or later version:
		 * BAT LED behavior:
		 * - Fully charged / idle: Green ON
		 * - Charging: Amber ON (BAT_LED_RED && BAT_LED_GREEN)
		 * - Battery discharging capacity<10%, red blink
		 * - Battery error: Red ON
		 */
		switch (charge_get_state()) {
		case PWR_STATE_CHARGE:
			bat_led_set(BAT_LED_AMBER, 1);
			break;
		case PWR_STATE_CHARGE_NEAR_FULL:
			bat_led_set(BAT_LED_GREEN, 1);
			bat_led_set(BAT_LED_RED, 0);
			break;
		case PWR_STATE_DISCHARGE:
			bat_led_set(BAT_LED_GREEN, 0);
			if (charge_get_percent() < 3)
				bat_led_set(BAT_LED_RED,
					    (battery_second & 1) ? 0 : 1);
			else if (charge_get_percent() < 10)
				bat_led_set(BAT_LED_RED,
					    (battery_second & 3) ? 0 : 1);
			else
				bat_led_set(BAT_LED_RED, 0);
			break;
		case PWR_STATE_ERROR:
			bat_led_set(BAT_LED_RED, 1);
			break;
		case PWR_STATE_IDLE:	/* External power connected in IDLE. */
			bat_led_set(BAT_LED_GREEN, 1);
			bat_led_set(BAT_LED_RED, 0);
			break;
		default:
			/* Other states don't alter LED behavior */
			break;
		}
	}
}

/**
 * Called by hook task every 1 sec
 */
static void led_second(void)
{
	if (led_auto_control_is_enabled(EC_LED_ID_POWER_LED))
		oak_led_set_power(system_get_board_version());
	if (led_auto_control_is_enabled(EC_LED_ID_BATTERY_LED))
		oak_led_set_battery(system_get_board_version());
}
DECLARE_HOOK(HOOK_SECOND, led_second, HOOK_PRIO_DEFAULT);
