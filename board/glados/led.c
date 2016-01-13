/* Copyright 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Power and battery LED control for Glados.
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

#define CRITICAL_LOW_BATTERY_PERCENTAGE 3
#define LOW_BATTERY_PERCENTAGE 10

#define LED_TOTAL_4SECS_TICKS 4
#define LED_TOTAL_2SECS_TICKS 2
#define LED_ON_1SEC_TICKS 1
#define LED_ON_2SECS_TICKS 2

const enum ec_led_id supported_led_ids[] = {
			EC_LED_ID_BATTERY_LED};

const int supported_led_ids_count = ARRAY_SIZE(supported_led_ids);

enum led_color {
	LED_OFF = 0,
	LED_RED,
	LED_AMBER,
	LED_GREEN,
	LED_COLOR_COUNT  /* Number of colors, not a color itself */
};

static int bat_led_set_color(enum led_color color)
{
	switch (color) {
	case LED_OFF:
		gpio_set_level(GPIO_BAT_LED_RED, BAT_LED_OFF);
		gpio_set_level(GPIO_BAT_LED_GREEN, BAT_LED_OFF);
		break;
	case LED_RED:
		gpio_set_level(GPIO_BAT_LED_RED, BAT_LED_ON);
		gpio_set_level(GPIO_BAT_LED_GREEN, BAT_LED_OFF);
		break;
	case LED_AMBER:
		gpio_set_level(GPIO_BAT_LED_RED, BAT_LED_ON);
		gpio_set_level(GPIO_BAT_LED_GREEN, BAT_LED_ON);
		break;
	case LED_GREEN:
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
	brightness_range[EC_LED_COLOR_RED] = 1;
	brightness_range[EC_LED_COLOR_GREEN] = 1;
}

static int glados_led_set_color_battery(enum led_color color)
{
	return bat_led_set_color(color);
}

static int glados_led_set_color(enum ec_led_id led_id, enum led_color color)
{
	int rv;

	led_auto_control(led_id, 0);
	switch (led_id) {
	case EC_LED_ID_BATTERY_LED:
		rv = glados_led_set_color_battery(color);
		break;
	default:
		return EC_ERROR_UNKNOWN;
	}
	return rv;
}

int led_set_brightness(enum ec_led_id led_id, const uint8_t *brightness)
{
	if (brightness[EC_LED_COLOR_RED] != 0 &&
	    brightness[EC_LED_COLOR_GREEN] != 0)
	    glados_led_set_color(led_id, LED_AMBER);
	else if (brightness[EC_LED_COLOR_RED] != 0)
		glados_led_set_color(led_id, LED_RED);
	else if (brightness[EC_LED_COLOR_GREEN] != 0)
		glados_led_set_color(led_id, LED_GREEN);
	else
		glados_led_set_color(led_id, LED_OFF);

	return EC_SUCCESS;
}

static void glados_led_set_battery(void)
{
	static int battery_ticks;
	uint32_t chflags = charge_get_flags();

	battery_ticks++;

	/* BAT LED behavior:
	 * Same as the chromeos spec
	 * Green/Amber for CHARGE_FLAG_FORCE_IDLE
	 */
	switch (charge_get_state()) {
	case PWR_STATE_CHARGE:
		glados_led_set_color_battery(LED_AMBER);
		break;
	case PWR_STATE_DISCHARGE:
		/* Less than 3%, blink one second every two second */
		if (!chipset_in_state(CHIPSET_STATE_ANY_OFF) &&
			charge_get_percent() < CRITICAL_LOW_BATTERY_PERCENTAGE)
			glados_led_set_color_battery(
				(battery_ticks % LED_TOTAL_2SECS_TICKS <
				 LED_ON_1SEC_TICKS) ? LED_AMBER : LED_OFF);
		/* Less than 10%, blink one second every four seconds */
		else if (!chipset_in_state(CHIPSET_STATE_ANY_OFF) &&
			charge_get_percent() < LOW_BATTERY_PERCENTAGE)
			glados_led_set_color_battery(
				(battery_ticks % LED_TOTAL_4SECS_TICKS <
				 LED_ON_1SEC_TICKS) ? LED_AMBER : LED_OFF);
		else
			glados_led_set_color_battery(LED_OFF);
		break;
	case PWR_STATE_ERROR:
		glados_led_set_color_battery(
			(battery_ticks % LED_TOTAL_2SECS_TICKS <
			 LED_ON_1SEC_TICKS) ? LED_RED : LED_OFF);
		break;
	case PWR_STATE_CHARGE_NEAR_FULL:
		glados_led_set_color_battery(LED_GREEN);
		break;
	case PWR_STATE_IDLE: /* External power connected in IDLE */
		if (chflags & CHARGE_FLAG_FORCE_IDLE)
			glados_led_set_color_battery(
				(battery_ticks % LED_TOTAL_4SECS_TICKS <
				 LED_ON_2SECS_TICKS) ? LED_GREEN : LED_AMBER);
		else
			glados_led_set_color_battery(LED_GREEN);
		break;
	default:
		/* Other states don't alter LED behavior */
		break;
	}
}

/** * Called by hook task every 1 sec  */
static void led_second(void)
{
	if (led_auto_control_is_enabled(EC_LED_ID_BATTERY_LED))
		glados_led_set_battery();
}
DECLARE_HOOK(HOOK_SECOND, led_second, HOOK_PRIO_DEFAULT);
