/* Copyright 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Power and battery LED control.
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

#define BAT_LED_ON 1
#define BAT_LED_OFF 0

const enum ec_led_id supported_led_ids[] = {
			EC_LED_ID_BATTERY_LED};

const int supported_led_ids_count = ARRAY_SIZE(supported_led_ids);

enum led_color {
	LED_OFF = 0,
	LED_AMBER,
	LED_WHITE,
	LED_COLOR_COUNT  /* Number of colors, not a color itself */
};

static int bat_led_set_color(enum led_color color)
{
	switch (color) {
	case LED_OFF:
		gpio_set_level(GPIO_BAT_LED_RED, BAT_LED_OFF);
		gpio_set_level(GPIO_BAT_LED_GREEN, BAT_LED_OFF);
		break;
	case LED_AMBER:
		gpio_set_level(GPIO_BAT_LED_RED, BAT_LED_ON);
		gpio_set_level(GPIO_BAT_LED_GREEN, BAT_LED_OFF);
		break;
	case LED_WHITE:
		gpio_set_level(GPIO_BAT_LED_RED, BAT_LED_OFF);
		gpio_set_level(GPIO_BAT_LED_GREEN, BAT_LED_ON);
		break;
	default:
		return EC_ERROR_UNKNOWN;
	}
	return EC_SUCCESS;
}

void led_get_brightness_range(enum ec_led_id led_id, uint8_t *brightness_range)
{
	brightness_range[EC_LED_COLOR_YELLOW] = 1;
	brightness_range[EC_LED_COLOR_WHITE] = 1;
}

int led_set_brightness(enum ec_led_id led_id, const uint8_t *brightness)
{
	switch (led_id) {
	case EC_LED_ID_BATTERY_LED:
		if (brightness[EC_LED_COLOR_WHITE] != 0)
			bat_led_set_color(LED_WHITE);
		else if (brightness[EC_LED_COLOR_YELLOW] != 0)
			bat_led_set_color(LED_AMBER);
		else
			bat_led_set_color(LED_OFF);
		break;
	default:
		break;
	}

	return EC_SUCCESS;
}

static void board_led_set_battery(void)
{
	static int battery_ticks;
	uint32_t chflags = charge_get_flags();
	static int power_ticks;
	static int previous_state_suspend;

	battery_ticks++;
	power_ticks++;

	if (chipset_in_state(CHIPSET_STATE_SUSPEND | CHIPSET_STATE_STANDBY)) {
		/*
		 * Reset ticks if entering suspend so LED turns white
		 * as soon as possible.
		 */
		if (!previous_state_suspend)
			power_ticks = 0;

		/* Blink once every one second. */
		bat_led_set_color((power_ticks & 0x4) ? LED_WHITE : LED_OFF);

		previous_state_suspend = 1;
		return;
	}
	previous_state_suspend = 0;

	switch (charge_get_state()) {
	case PWR_STATE_CHARGE:
		bat_led_set_color(LED_AMBER);
		break;
	case PWR_STATE_DISCHARGE:
		if (charge_get_percent() < 12)
			bat_led_set_color(
				(battery_ticks & 0x4) ? LED_WHITE : LED_OFF);
		else
			bat_led_set_color(LED_OFF);
		break;
	case PWR_STATE_ERROR:
		bat_led_set_color((battery_ticks & 0x2) ? LED_WHITE : LED_OFF);
		break;
	case PWR_STATE_CHARGE_NEAR_FULL:
		bat_led_set_color(LED_WHITE);
		break;
	case PWR_STATE_IDLE: /* External power connected in IDLE */
		if (chflags & CHARGE_FLAG_FORCE_IDLE)
			bat_led_set_color(
				(battery_ticks & 0x4) ? LED_AMBER : LED_OFF);
		else
			bat_led_set_color(LED_WHITE);
		break;
	default:
		/* Other states don't alter LED behavior */
		break;
	}
}

/* Called by hook task every TICK */
static void led_tick(void)
{
	if (led_auto_control_is_enabled(EC_LED_ID_BATTERY_LED))
		board_led_set_battery();
}
DECLARE_HOOK(HOOK_TICK, led_tick, HOOK_PRIO_DEFAULT);
