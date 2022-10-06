/* Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Battery LED control for Agah
 */

#include <stdint.h>

#include "battery.h"
#include "charge_manager.h"
#include "charge_state.h"
#include "chipset.h"
#include "ec_commands.h"
#include "gpio.h"
#include "host_command.h"
#include "led_common.h"
#include "task.h"

#define BAT_LED_ON 0
#define BAT_LED_OFF 1

#define BATT_LOW_BCT 10

#define LED_TICK_INTERVAL_MS (500 * MSEC)
#define LED_CYCLE_TIME_MS (2000 * MSEC)
#define LED_TICKS_PER_CYCLE (LED_CYCLE_TIME_MS / LED_TICK_INTERVAL_MS)
#define LED_ON_TIME_MS (1000 * MSEC)
#define LED_ON_TICKS (LED_ON_TIME_MS / LED_TICK_INTERVAL_MS)

const enum ec_led_id supported_led_ids[] = { EC_LED_ID_BATTERY_LED };

const int supported_led_ids_count = ARRAY_SIZE(supported_led_ids);

enum led_color {
	LED_OFF = 0,
	LED_AMBER,
	LED_WHITE,
	LED_COLOR_COUNT /* Number of colors, not a color itself */
};

static void led_set_color_battery(enum led_color color)
{
	enum gpio_signal amber_led, white_led;

	amber_led = GPIO_LED_1_L;
	white_led = GPIO_LED_2_L;

	switch (color) {
	case LED_WHITE:
		gpio_set_level(white_led, BAT_LED_ON);
		gpio_set_level(amber_led, BAT_LED_OFF);
		break;
	case LED_AMBER:
		gpio_set_level(white_led, BAT_LED_OFF);
		gpio_set_level(amber_led, BAT_LED_ON);
		break;
	case LED_OFF:
		gpio_set_level(white_led, BAT_LED_OFF);
		gpio_set_level(amber_led, BAT_LED_OFF);
		break;
	default:
		break;
	}
}

void led_get_brightness_range(enum ec_led_id led_id, uint8_t *brightness_range)
{
	switch (led_id) {
	case EC_LED_ID_BATTERY_LED:
		brightness_range[EC_LED_COLOR_WHITE] = 1;
		brightness_range[EC_LED_COLOR_AMBER] = 1;
		break;
	default:
		break;
	}
}

int led_set_brightness(enum ec_led_id led_id, const uint8_t *brightness)
{
	switch (led_id) {
	case EC_LED_ID_BATTERY_LED:
		if (brightness[EC_LED_COLOR_WHITE] != 0)
			led_set_color_battery(LED_WHITE);
		else if (brightness[EC_LED_COLOR_AMBER] != 0)
			led_set_color_battery(LED_AMBER);
		else
			led_set_color_battery(LED_OFF);
		break;
	default:
		return EC_ERROR_PARAM1;
	}

	return EC_SUCCESS;
}

/*
 * Set active charge port color to the parameter, turn off all others.
 * If no port is active (-1), turn off all LEDs.
 */
static void set_active_port_color(enum led_color color)
{
	if (led_auto_control_is_enabled(EC_LED_ID_BATTERY_LED))
		led_set_color_battery(color);
}

static void led_set_battery(void)
{
	static unsigned int battery_ticks;
	static unsigned int suspend_ticks;

	battery_ticks++;

	if (chipset_in_state(CHIPSET_STATE_ANY_SUSPEND) &&
	    charge_get_state() != PWR_STATE_CHARGE) {
		suspend_ticks++;

		led_set_color_battery(
			(suspend_ticks % LED_TICKS_PER_CYCLE < LED_ON_TICKS) ?
				LED_WHITE :
				LED_OFF);

		return;
	}

	switch (charge_get_state()) {
	case PWR_STATE_CHARGE:
		/* Always indicate when charging, even in suspend. */
		set_active_port_color(LED_AMBER);
		break;
	case PWR_STATE_DISCHARGE:
		/*
		 * Blinking amber LEDs slowly if battery is lower 10
		 * percentage.
		 */
		if (led_auto_control_is_enabled(EC_LED_ID_BATTERY_LED)) {
			if (charge_get_percent() < BATT_LOW_BCT)
				led_set_color_battery(
					(battery_ticks % LED_TICKS_PER_CYCLE <
					 LED_ON_TICKS) ?
						LED_AMBER :
						LED_OFF);
			else
				led_set_color_battery(LED_OFF);
		}

		break;
	case PWR_STATE_ERROR:
		if (led_auto_control_is_enabled(EC_LED_ID_BATTERY_LED)) {
			led_set_color_battery(
				(battery_ticks & 0x1) ? LED_AMBER : LED_OFF);
		}

		break;
	case PWR_STATE_CHARGE_NEAR_FULL:
		set_active_port_color(LED_WHITE);
		break;
	case PWR_STATE_IDLE: /* External power connected in IDLE */
		set_active_port_color(LED_WHITE);
		break;
	case PWR_STATE_FORCED_IDLE:
		set_active_port_color(
			(battery_ticks % LED_TICKS_PER_CYCLE < LED_ON_TICKS) ?
				LED_AMBER :
				LED_OFF);
		break;
	default:
		/* Other states don't alter LED behavior */
		break;
	}
}

void led_task(void *u)
{
	uint32_t start_time;
	uint32_t task_duration;

	while (1) {
		start_time = get_time().le.lo;

		led_set_battery();

		/* Compute time for this iteration */
		task_duration = get_time().le.lo - start_time;
		/*
		 * Compute wait time required to for next desired LED tick. If
		 * the duration exceeds the tick time, then don't sleep.
		 */
		if (task_duration < LED_TICK_INTERVAL_MS)
			usleep(LED_TICK_INTERVAL_MS - task_duration);
	}
}
