/* Copyright 2017 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Power and battery LED control for Fizz
 */

#include "chipset.h"
#include "console.h"
#include "ec_commands.h"
#include "gpio.h"
#include "hooks.h"
#include "led_common.h"
#include "util.h"

static int led_debug;

enum led_color {
	LED_OFF = 0,
	LED_RED,
	LED_GREEN,
	LED_AMBER,

	/* Number of colors, not a color itself */
	LED_COLOR_COUNT
};

static int led_set_color_power(enum led_color color)
{
	int green = 0;
	int red = 0;

	switch (color) {
	case LED_OFF:
		break;
	case LED_GREEN:
		green = 1;
		break;
	case LED_RED:
		red = 1;
		break;
	case LED_AMBER:
		green = 1;
		red = 1;
		break;
	default:
		return EC_ERROR_UNKNOWN;
	}

	gpio_set_level(GPIO_PWR_GRN_LED, green);
	gpio_set_level(GPIO_PWR_RED_LED, red);

	return EC_SUCCESS;
}

static int led_set_color(enum ec_led_id id, enum led_color color)
{
	switch (id) {
	case EC_LED_ID_POWER_LED:
		return led_set_color_power(color);
	default:
		return EC_ERROR_UNKNOWN;
	}
}

static void led_set_power(void)
{
	if (chipset_in_state(CHIPSET_STATE_ON)) {
		led_set_color(EC_LED_ID_POWER_LED, LED_GREEN);
		return;
	}
}

/**
 * Called by hook task every 250 ms
 */
static void led_tick(void)
{
	if (led_debug)
		return;
	led_set_power();
}
DECLARE_HOOK(HOOK_TICK, led_tick, HOOK_PRIO_DEFAULT);

static int command_led(int argc, char **argv)
{
	enum ec_led_id id = EC_LED_ID_POWER_LED;

	if (argc < 2)
		return EC_ERROR_PARAM_COUNT;

	if (!strcasecmp(argv[1], "debug")) {
		led_debug ^= 1;
		ccprintf("led_debug %s\n", led_debug ? "on" : "off");
	} else if (!strcasecmp(argv[1], "off")) {
		led_set_color(id, LED_OFF);
	} else if (!strcasecmp(argv[1], "red")) {
		led_set_color(id, LED_RED);
	} else if (!strcasecmp(argv[1], "green")) {
		led_set_color(id, LED_GREEN);
	} else if (!strcasecmp(argv[1], "amber")) {
		led_set_color(id, LED_AMBER);
	} else {
		/* maybe handle charger_discharge_on_ac() too? */
		return EC_ERROR_PARAM1;
	}
	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(led, command_led,
			"[debug|red|green|amber|off]",
			"Turn on/off LED");
