/* Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Power and battery LED control for Vell
 */

#include "battery.h"
#include "cbi.h"
#include "charge_manager.h"
#include "charge_state.h"
#include "ec_commands.h"
#include "gpio.h"
#include "hooks.h"
#include "host_command.h"
#include "led_common.h"
#include "task.h"
#include "util.h"

#include <stdint.h>

#define BATT_LOW_BCT 10

#define LED_TICK_INTERVAL_MS (500 * MSEC)
#define LED_CYCLE_TIME_MS (2000 * MSEC)
#define LED_TICKS_PER_CYCLE (LED_CYCLE_TIME_MS / LED_TICK_INTERVAL_MS)
#define LED_ON_TIME_MS (1000 * MSEC)
#define LED_ON_TICKS (LED_ON_TIME_MS / LED_TICK_INTERVAL_MS)

const enum ec_led_id supported_led_ids[] = { EC_LED_ID_LEFT_LED,
					     EC_LED_ID_RIGHT_LED };

const int supported_led_ids_count = ARRAY_SIZE(supported_led_ids);

enum led_color {
	LED_OFF = 0,
	LED_AMBER,
	LED_WHITE,
	LED_COLOR_COUNT /* Number of colors, not a color itself */
};

enum led_port { RIGHT_PORT = 0, LEFT_PORT };

uint8_t bat_led_on;
uint8_t bat_led_off;

static void led_init(void)
{
	if (get_board_id() < 2) {
		bat_led_on = 0;
		bat_led_off = 1;
	} else {
		bat_led_on = 1;
		bat_led_off = 0;
	}
}
DECLARE_HOOK(HOOK_INIT, led_init, HOOK_PRIO_DEFAULT);

static void led_set_color_battery(int port, enum led_color color)
{
	enum gpio_signal amber_led, white_led;

	amber_led = (port == RIGHT_PORT ? GPIO_RIGHT_LED_AMBER_L :
					  GPIO_LEFT_LED_AMBER_L);
	white_led = (port == RIGHT_PORT ? GPIO_RIGHT_LED_WHITE_L :
					  GPIO_LEFT_LED_WHITE_L);

	switch (color) {
	case LED_WHITE:
		gpio_set_level(white_led, bat_led_on);
		gpio_set_level(amber_led, bat_led_off);
		break;
	case LED_AMBER:
		gpio_set_level(white_led, bat_led_off);
		gpio_set_level(amber_led, bat_led_on);
		break;
	case LED_OFF:
		gpio_set_level(white_led, bat_led_off);
		gpio_set_level(amber_led, bat_led_off);
		break;
	default:
		break;
	}
}

void led_get_brightness_range(enum ec_led_id led_id, uint8_t *brightness_range)
{
	memset(brightness_range, '\0',
	       sizeof(*brightness_range) * EC_LED_COLOR_COUNT);
	switch (led_id) {
	case EC_LED_ID_LEFT_LED:
		brightness_range[EC_LED_COLOR_WHITE] = 1;
		brightness_range[EC_LED_COLOR_AMBER] = 1;
		break;
	case EC_LED_ID_RIGHT_LED:
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
	case EC_LED_ID_LEFT_LED:
		if (brightness[EC_LED_COLOR_WHITE] != 0)
			led_set_color_battery(LEFT_PORT, LED_WHITE);
		else if (brightness[EC_LED_COLOR_AMBER] != 0)
			led_set_color_battery(LEFT_PORT, LED_AMBER);
		else
			led_set_color_battery(LEFT_PORT, LED_OFF);
		break;
	case EC_LED_ID_RIGHT_LED:
		if (brightness[EC_LED_COLOR_WHITE] != 0)
			led_set_color_battery(RIGHT_PORT, LED_WHITE);
		else if (brightness[EC_LED_COLOR_AMBER] != 0)
			led_set_color_battery(RIGHT_PORT, LED_AMBER);
		else
			led_set_color_battery(RIGHT_PORT, LED_OFF);
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
	int usbc_port = charge_manager_get_active_charge_port();
	int port = 0;

	if ((usbc_port == USBC_PORT_C0) || (usbc_port == USBC_PORT_C1))
		port = RIGHT_PORT;
	else if ((usbc_port == USBC_PORT_C2) || (usbc_port == USBC_PORT_C3))
		port = LEFT_PORT;

	if (led_auto_control_is_enabled(EC_LED_ID_RIGHT_LED))
		led_set_color_battery(RIGHT_PORT,
				      (port == RIGHT_PORT) ? color : LED_OFF);
	if (led_auto_control_is_enabled(EC_LED_ID_LEFT_LED))
		led_set_color_battery(LEFT_PORT,
				      (port == LEFT_PORT) ? color : LED_OFF);
}

static void led_set_battery(void)
{
	static unsigned int battery_ticks;

	battery_ticks++;

	switch (led_pwr_get_state()) {
	case LED_PWRS_CHARGE:
		/* Always indicate when charging, even in suspend. */
		set_active_port_color(LED_AMBER);
		break;
	case LED_PWRS_DISCHARGE:
		/*
		 * Blinking amber LEDs slowly if battery is lower 10
		 * percentage.
		 */
		if (led_auto_control_is_enabled(EC_LED_ID_RIGHT_LED)) {
			if (charge_get_percent() < BATT_LOW_BCT)
				led_set_color_battery(
					RIGHT_PORT,
					(battery_ticks % LED_TICKS_PER_CYCLE <
					 LED_ON_TICKS) ?
						LED_AMBER :
						LED_OFF);
			else
				led_set_color_battery(RIGHT_PORT, LED_OFF);
		}

		if (led_auto_control_is_enabled(EC_LED_ID_LEFT_LED)) {
			if (charge_get_percent() < BATT_LOW_BCT)
				led_set_color_battery(
					LEFT_PORT,
					(battery_ticks % LED_TICKS_PER_CYCLE <
					 LED_ON_TICKS) ?
						LED_AMBER :
						LED_OFF);
			else
				led_set_color_battery(LEFT_PORT, LED_OFF);
		}
		break;
	case LED_PWRS_ERROR:
		if (led_auto_control_is_enabled(EC_LED_ID_RIGHT_LED)) {
			led_set_color_battery(
				RIGHT_PORT,
				(battery_ticks & 0x1) ? LED_AMBER : LED_OFF);
		}

		if (led_auto_control_is_enabled(EC_LED_ID_LEFT_LED)) {
			led_set_color_battery(LEFT_PORT, (battery_ticks & 0x1) ?
								 LED_AMBER :
								 LED_OFF);
		}
		break;
	case LED_PWRS_CHARGE_NEAR_FULL:
		set_active_port_color(LED_WHITE);
		break;
	case LED_PWRS_IDLE: /* External power connected in IDLE */
		set_active_port_color(LED_WHITE);
		break;
	case LED_PWRS_FORCED_IDLE:
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
			crec_usleep(LED_TICK_INTERVAL_MS - task_duration);
	}
}
