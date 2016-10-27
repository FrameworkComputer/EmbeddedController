/* Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Power and battery LED control for Snappy
 */

#include "battery.h"
#include "charge_state.h"
#include "chipset.h"
#include "ec_commands.h"
#include "extpower.h"
#include "gpio.h"
#include "hooks.h"
#include "host_command.h"
#include "led_common.h"
#include "util.h"

#define BAT_LED_ON 0
#define BAT_LED_OFF 1

const enum ec_led_id supported_led_ids[] = {
	EC_LED_ID_POWER_LED, EC_LED_ID_BATTERY_LED};

const int supported_led_ids_count = ARRAY_SIZE(supported_led_ids);

enum led_color {
	LED_OFF = 0,
	LED_AMBER,
	LED_WHITE,
	LED_COLOR_COUNT  /* Number of colors, not a color itself */
};

static int led_set_color_battery(enum led_color color)
{
	switch (color) {
	case LED_OFF:
		gpio_set_level(GPIO_BAT_LED_WHITE, BAT_LED_OFF);
		gpio_set_level(GPIO_BAT_LED_AMBER, BAT_LED_OFF);
		break;
	case LED_WHITE:
		gpio_set_level(GPIO_BAT_LED_WHITE, BAT_LED_ON);
		gpio_set_level(GPIO_BAT_LED_AMBER, BAT_LED_OFF);
		break;
	case LED_AMBER:
		gpio_set_level(GPIO_BAT_LED_WHITE, BAT_LED_OFF);
		gpio_set_level(GPIO_BAT_LED_AMBER, BAT_LED_ON);
		break;
	default:
		return EC_ERROR_UNKNOWN;
	}
	return EC_SUCCESS;
}

static int led_set_color_power(enum led_color color)
{
	switch (color) {
	case LED_OFF:
		gpio_set_level(GPIO_POWER_LED_WHITE, BAT_LED_OFF);
		break;
	case LED_WHITE:
		gpio_set_level(GPIO_POWER_LED_WHITE, BAT_LED_ON);
		break;
	default:
		return EC_ERROR_UNKNOWN;
	}
	return EC_SUCCESS;
}

void led_get_brightness_range(enum ec_led_id led_id, uint8_t *brightness_range)
{
	brightness_range[EC_LED_COLOR_WHITE] = 1;
	brightness_range[EC_LED_COLOR_AMBER] = 1;
}

static int led_set_color(enum ec_led_id led_id, enum led_color color)
{
	int rv;

	switch (led_id) {
	case EC_LED_ID_BATTERY_LED:
		rv = led_set_color_battery(color);
		break;
	case EC_LED_ID_POWER_LED:
		rv = led_set_color_power(color);
		break;
	default:
		return EC_ERROR_UNKNOWN;
	}
	return rv;
}

int led_set_brightness(enum ec_led_id led_id, const uint8_t *brightness)
{
	if (brightness[EC_LED_COLOR_WHITE] != 0)
		led_set_color(led_id, LED_WHITE);
	else if (brightness[EC_LED_COLOR_AMBER] != 0)
		led_set_color(led_id, LED_AMBER);
	else
		led_set_color(led_id, LED_OFF);

	return EC_SUCCESS;
}

static void led_set_battery(void)
{
	static int battery_ticks;
	static int power_ticks;
	uint32_t chflags = charge_get_flags();
	int remaining_capacity;
	int full_charge_capacity;
	int permillage;

	battery_ticks++;
	power_ticks++;

	remaining_capacity = *(int *)host_get_memmap(EC_MEMMAP_BATT_CAP);
	full_charge_capacity = *(int *)host_get_memmap(EC_MEMMAP_BATT_LFCC);
	permillage = !full_charge_capacity ? 0 :
		(1000 * remaining_capacity) / full_charge_capacity;

	switch (charge_get_state()) {
	case PWR_STATE_CHARGE:
		led_set_color_battery(LED_AMBER);
		break;
	case PWR_STATE_DISCHARGE:
		/*
		 * Blink white light (1 sec on, 1 sec off)
		 * when battery capacity is less than 10%
		 */
		if (permillage < 100)
			led_set_color_battery(
				(battery_ticks & 0x4) ? LED_WHITE : LED_OFF);
		else
			led_set_color_battery(LED_OFF);
		break;
	case PWR_STATE_ERROR:
		led_set_color_battery(
			(battery_ticks & 0x2) ? LED_WHITE : LED_OFF);
		break;
	case PWR_STATE_CHARGE_NEAR_FULL:
		led_set_color_battery(LED_WHITE);
		break;
	case PWR_STATE_IDLE: /* External power connected in IDLE */
		if (chflags & CHARGE_FLAG_FORCE_IDLE)
			led_set_color_battery(
				(battery_ticks & 0x4) ? LED_AMBER : LED_OFF);
		else
			led_set_color_battery(LED_WHITE);
		break;
	default:
		/* Other states don't alter LED behavior */
		break;
	}
}

static void led_set_power(void)
{
	static int power_tick;

	power_tick++;

	if (chipset_in_state(CHIPSET_STATE_ON))
		led_set_color_power(LED_WHITE);
	else if (chipset_in_state(CHIPSET_STATE_SUSPEND))
		led_set_color_power(
			(power_tick & 0x4) ? LED_WHITE : LED_OFF);
	else
		led_set_color_power(LED_OFF);
}

/* Called by hook task every TICK */
static void led_tick(void)
{
	if (led_auto_control_is_enabled(EC_LED_ID_BATTERY_LED))
		led_set_battery();
	if (led_auto_control_is_enabled(EC_LED_ID_POWER_LED))
		led_set_power();
}
DECLARE_HOOK(HOOK_TICK, led_tick, HOOK_PRIO_DEFAULT);
