/* Copyright 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Power and battery LED control for Cyan.
 */

#include "battery.h"
#include "charge_state.h"
#include "chipset.h"
#include "ec_commands.h"
#include "gpio.h"
#include "hooks.h"
#include "host_command.h"
#include "led_common.h"
#include "util.h"

#define LED_TOTAL_TICKS 16
#define LED_ON_TICKS 4

enum led_color {
	LED_OFF = 0,
	LED_BLUE,
	LED_AMBER,
	LED_PINK,

	LED_COLOR_COUNT  /* Number of colors, not a color itself */
};

const enum ec_led_id supported_led_ids[] = {
	EC_LED_ID_POWER_LED, EC_LED_ID_BATTERY_LED};

const int supported_led_ids_count = ARRAY_SIZE(supported_led_ids);

static int cyan_led_set_gpio(enum led_color color,
			      enum gpio_signal gpio_led_blue_l,
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
	case LED_PINK:
		gpio_set_level(gpio_led_blue_l,  0);
		gpio_set_level(gpio_led_amber_l, 0);
		break;
	default:
		return EC_ERROR_UNKNOWN;
	}
	return EC_SUCCESS;
}

static int cyan_led_set_color_battery(enum led_color color)
{
	return cyan_led_set_gpio(color, GPIO_BAT_LED0_L, GPIO_BAT_LED1_L);
}

static int cyan_led_set_color_power(enum led_color color)
{
	return cyan_led_set_gpio(color, GPIO_PWR_LED0_L, GPIO_PWR_LED1_L);
}

static int cyan_led_set_color(enum ec_led_id led_id, enum led_color color)
{
	int rv;

	led_auto_control(led_id, 0);
	switch (led_id) {
	case EC_LED_ID_BATTERY_LED:
		rv = cyan_led_set_color_battery(color);
		break;
	case EC_LED_ID_POWER_LED:
		rv = cyan_led_set_color_power(color);
		break;
	default:
		return EC_ERROR_UNKNOWN;
	}
	return rv;
}

int led_set_brightness(enum ec_led_id led_id, const uint8_t *brightness)
{
	if (brightness[EC_LED_COLOR_BLUE] != 0 &&
	    brightness[EC_LED_COLOR_YELLOW] != 0)
		cyan_led_set_color(led_id, LED_PINK);
	else if (brightness[EC_LED_COLOR_BLUE] != 0)
		cyan_led_set_color(led_id, LED_BLUE);
	else if (brightness[EC_LED_COLOR_YELLOW] != 0)
		cyan_led_set_color(led_id, LED_AMBER);
	else
		cyan_led_set_color(led_id, LED_OFF);

	return EC_SUCCESS;
}

void led_get_brightness_range(enum ec_led_id led_id, uint8_t *brightness_range)
{
	/* Ignoring led_id as both leds support the same colors */
	brightness_range[EC_LED_COLOR_BLUE] = 1;
	brightness_range[EC_LED_COLOR_YELLOW] = 1;
}

static void cyan_led_set_power(void)
{
	static int power_ticks;
	static int previous_state_suspend;

	power_ticks++;

	if (chipset_in_state(CHIPSET_STATE_SUSPEND)) {
		/* Reset ticks if entering suspend so LED turns amber
		 * as soon as possible. */
		if (!previous_state_suspend)
			power_ticks = 0;

		/* Blink once every four seconds. */
		cyan_led_set_color_power(
			(power_ticks % LED_TOTAL_TICKS < LED_ON_TICKS) ?
			LED_AMBER : LED_OFF);

		previous_state_suspend = 1;
		return;
	}

	previous_state_suspend = 0;

	if (chipset_in_state(CHIPSET_STATE_ANY_OFF))
		cyan_led_set_color_power(LED_OFF);
	else if (chipset_in_state(CHIPSET_STATE_ON))
		cyan_led_set_color_power(LED_BLUE);
}

static void cyan_led_set_battery(void)
{
	static int battery_ticks;
	uint32_t chflags = charge_get_flags();

	battery_ticks++;

	switch (charge_get_state()) {
	case PWR_STATE_CHARGE:
		cyan_led_set_color_battery(LED_AMBER);
		break;
	case PWR_STATE_CHARGE_NEAR_FULL:
		cyan_led_set_color_battery(LED_BLUE);
		break;
	case PWR_STATE_DISCHARGE:
		cyan_led_set_color_battery(LED_OFF);
		break;
	case PWR_STATE_ERROR:
		cyan_led_set_color_battery(
			(battery_ticks % LED_TOTAL_TICKS < LED_ON_TICKS) ?
			LED_AMBER : LED_OFF);
		break;
	case PWR_STATE_IDLE: /* External power connected in IDLE. */
		if (chflags & CHARGE_FLAG_FORCE_IDLE)
			cyan_led_set_color_battery(
				(battery_ticks & 0x4) ? LED_BLUE : LED_OFF);
		else
			cyan_led_set_color_battery(LED_BLUE);
		break;
	default:
		/* Other states don't alter LED behavior */
		break;
	}
}

/* Called by hook task every 250mSec */
static void led_tick(void)
{
	if (led_auto_control_is_enabled(EC_LED_ID_POWER_LED))
		cyan_led_set_power();

	if (led_auto_control_is_enabled(EC_LED_ID_BATTERY_LED))
		cyan_led_set_battery();
}
DECLARE_HOOK(HOOK_TICK, led_tick, HOOK_PRIO_DEFAULT);
