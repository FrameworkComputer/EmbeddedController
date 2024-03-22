/* Copyright 2017 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Power and battery LED control.
 */

#include "battery.h"
#include "charge_manager.h"
#include "charge_state.h"
#include "chipset.h"
#include "ec_commands.h"
#include "extpower.h"
#include "gpio.h"
#include "hooks.h"
#include "host_command.h"
#include "led_common.h"
#include "system.h"
#include "util.h"

#define BAT_LED_ON 1
#define BAT_LED_OFF 0

#define LED_TOTAL_TICKS 16
#define LED_ON_TICKS 8

const enum ec_led_id supported_led_ids[] = { EC_LED_ID_POWER_LED,
					     EC_LED_ID_BATTERY_LED };

const int supported_led_ids_count = ARRAY_SIZE(supported_led_ids);

enum led_color {
	LED_OFF = 0,
	LED_RED,
	LED_GREEN,
	LED_BLUE,

	/* Number of colors, not a color itself */
	LED_COLOR_COUNT
};

/**
 * Set LED color
 *
 * @param color         Enumerated color value
 */
static void set_color(enum led_color color)
{
	gpio_set_level(GPIO_POWER_LED, !(color == LED_BLUE));
	gpio_set_level(GPIO_LED_ACIN, !(color == LED_GREEN));
	gpio_set_level(GPIO_LED_CHARGE, !(color == LED_RED));
}

void led_get_brightness_range(enum ec_led_id led_id, uint8_t *brightness_range)
{
	brightness_range[EC_LED_COLOR_RED] = 1;
	brightness_range[EC_LED_COLOR_BLUE] = 1;
	brightness_range[EC_LED_COLOR_GREEN] = 1;
}

int led_set_brightness(enum ec_led_id led_id, const uint8_t *brightness)
{
	gpio_set_level(GPIO_POWER_LED, !brightness[EC_LED_COLOR_BLUE]);
	gpio_set_level(GPIO_LED_ACIN, !brightness[EC_LED_COLOR_GREEN]);
	gpio_set_level(GPIO_LED_CHARGE, !brightness[EC_LED_COLOR_RED]);

	return EC_SUCCESS;
}

static void nautilus_led_set_power_battery(void)
{
	static unsigned int power_ticks;
	enum led_color cur_led_color = LED_RED;
	enum led_pwr_state chg_state = led_pwr_get_state();
	int charge_percent = charge_get_percent();

	if (chipset_in_state(CHIPSET_STATE_ON)) {
		set_color(LED_BLUE);
		return;
	}

	/* Flash red on critical battery, which usually inhibits AP power-on. */
	if (battery_is_present() != BP_YES ||
	    charge_percent < CONFIG_CHARGER_MIN_BAT_PCT_FOR_POWER_ON) {
		set_color(((power_ticks++ % LED_TOTAL_TICKS) < LED_ON_TICKS) ?
				  LED_RED :
				  LED_OFF);
		return;
	}

	/* CHIPSET_STATE_OFF */
	switch (chg_state) {
	case LED_PWRS_DISCHARGE:
		if (extpower_is_present() &&
		    charge_percent >= CONFIG_BATT_HOST_FULL_FACTOR)
			cur_led_color = LED_GREEN;
		else
			cur_led_color = LED_OFF;
		break;
	case LED_PWRS_CHARGE:
		cur_led_color = LED_RED;
		break;
	case LED_PWRS_ERROR:
		cur_led_color =
			((power_ticks++ % LED_TOTAL_TICKS) < LED_ON_TICKS) ?
				LED_RED :
				LED_GREEN;
		break;
	case LED_PWRS_CHARGE_NEAR_FULL:
	case LED_PWRS_IDLE:
		cur_led_color = LED_OFF;
		break;
	case LED_PWRS_FORCED_IDLE:
		cur_led_color = LED_GREEN;
		break;
	default:
		cur_led_color = LED_RED;
		break;
	}

	set_color(cur_led_color);

	if (chg_state != LED_PWRS_ERROR)
		power_ticks = 0;
}

/**
 * Called by hook task every 250 ms
 */
static void led_tick(void)
{
	if (led_auto_control_is_enabled(EC_LED_ID_POWER_LED) &&
	    led_auto_control_is_enabled(EC_LED_ID_BATTERY_LED)) {
		nautilus_led_set_power_battery();
	}
}

DECLARE_HOOK(HOOK_TICK, led_tick, HOOK_PRIO_DEFAULT);
