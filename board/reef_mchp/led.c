/* Copyright 2016 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Power and battery LED control for Reef
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

#define CRITICAL_LOW_BATTERY_PERCENTAGE 3
#define LOW_BATTERY_PERCENTAGE 10

#define LED_TOTAL_4SECS_TICKS 4
#define LED_TOTAL_2SECS_TICKS 2
#define LED_ON_1SEC_TICKS 1
#define LED_ON_2SECS_TICKS 2

const enum ec_led_id supported_led_ids[] = { EC_LED_ID_BATTERY_LED };

const int supported_led_ids_count = ARRAY_SIZE(supported_led_ids);

enum led_color {
	LED_OFF = 0,
	LED_BLUE,
	LED_AMBER,
	LED_COLOR_COUNT /* Number of colors, not a color itself */
};

static int led_set_color_battery(enum led_color color)
{
	switch (color) {
	case LED_OFF:
		gpio_set_level(GPIO_BAT_LED_BLUE, BAT_LED_OFF);
		gpio_set_level(GPIO_BAT_LED_AMBER, BAT_LED_OFF);
		break;
	case LED_BLUE:
		gpio_set_level(GPIO_BAT_LED_BLUE, BAT_LED_ON);
		gpio_set_level(GPIO_BAT_LED_AMBER, BAT_LED_OFF);
		break;
	case LED_AMBER:
		gpio_set_level(GPIO_BAT_LED_BLUE, BAT_LED_OFF);
		gpio_set_level(GPIO_BAT_LED_AMBER, BAT_LED_ON);
		break;
	default:
		return EC_ERROR_UNKNOWN;
	}
	return EC_SUCCESS;
}

void led_get_brightness_range(enum ec_led_id led_id, uint8_t *brightness_range)
{
	brightness_range[EC_LED_COLOR_BLUE] = 1;
	brightness_range[EC_LED_COLOR_AMBER] = 1;
}

static int led_set_color(enum ec_led_id led_id, enum led_color color)
{
	int rv;

	switch (led_id) {
	case EC_LED_ID_BATTERY_LED:
		rv = led_set_color_battery(color);
		break;
	default:
		return EC_ERROR_UNKNOWN;
	}
	return rv;
}

int led_set_brightness(enum ec_led_id led_id, const uint8_t *brightness)
{
	if (brightness[EC_LED_COLOR_BLUE] != 0)
		led_set_color(led_id, LED_BLUE);
	else if (brightness[EC_LED_COLOR_AMBER] != 0)
		led_set_color(led_id, LED_AMBER);
	else
		led_set_color(led_id, LED_OFF);

	return EC_SUCCESS;
}

static void led_set_battery(void)
{
	static int battery_ticks;
	static int suspend_ticks;

	switch (led_pwr_get_state()) {
	case LED_PWRS_CHARGE:
		led_set_color_battery(LED_AMBER);
		break;
	case LED_PWRS_DISCHARGE_FULL:
		if (extpower_is_present()) {
			led_set_color_battery(LED_BLUE);
			break;
		}
		__fallthrough;
	case LED_PWRS_DISCHARGE /* and LED_PWRS_DISCHARGE_FULL */:
		if (chipset_in_state(CHIPSET_STATE_ON)) {
			led_set_color_battery(LED_BLUE);
		} else if (chipset_in_state(CHIPSET_STATE_ANY_SUSPEND)) {
			/* Blink once every four seconds. */
			led_set_color_battery(
				(suspend_ticks % LED_TOTAL_4SECS_TICKS) <
						LED_ON_1SEC_TICKS ?
					LED_AMBER :
					LED_OFF);
		} else {
			led_set_color_battery(LED_OFF);
		}
		break;
	case LED_PWRS_ERROR:
		led_set_color_battery((battery_ticks % LED_TOTAL_2SECS_TICKS <
				       LED_ON_1SEC_TICKS) ?
					      LED_AMBER :
					      LED_OFF);
		break;
	case LED_PWRS_CHARGE_NEAR_FULL:
		led_set_color_battery(LED_BLUE);
		break;
	case LED_PWRS_IDLE: /* External power connected in IDLE */
		if (charge_get_led_flags() & CHARGE_LED_FLAG_FORCE_IDLE)
			led_set_color_battery(
				(battery_ticks % LED_TOTAL_4SECS_TICKS <
				 LED_ON_2SECS_TICKS) ?
					LED_AMBER :
					LED_BLUE);
		else
			led_set_color_battery(LED_BLUE);
		break;
	default:
		/* Other states don't alter LED behavior */
		break;
	}

	battery_ticks++;
	suspend_ticks++;
}

/* Called by hook task every 1 sec */
static void led_second(void)
{
	/*
	 * Reference board only has one LED, so overload it to act as both
	 * power LED and battery LED.
	 */
	if (led_auto_control_is_enabled(EC_LED_ID_BATTERY_LED))
		led_set_battery();
}
DECLARE_HOOK(HOOK_SECOND, led_second, HOOK_PRIO_DEFAULT);
