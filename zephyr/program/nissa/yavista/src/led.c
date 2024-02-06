/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "battery.h"
#include "charge_manager.h"
#include "charge_state.h"
#include "chipset.h"
#include "ec_commands.h"
#include "gpio.h"
#include "hooks.h"
#include "host_command.h"
#include "led_common.h"

#include <stdint.h>

#define BAT_LED_ON 0
#define BAT_LED_OFF 1

#define BATT_LOW_BCT 10

#define LED_TICKS_PER_CYCLE 4
#define LED_TICKS_PER_CYCLE_S3 4
#define LED_ON_TICKS 2
#define POWER_LED_ON_S3_TICKS 2

const enum ec_led_id supported_led_ids[] = { EC_LED_ID_LEFT_LED,
					     EC_LED_ID_RIGHT_LED };

const int supported_led_ids_count = ARRAY_SIZE(supported_led_ids);

enum led_color {
	LED_OFF = 0,
	LED_AMBER,
	LED_WHITE,
	LED_COLOR_COUNT /* Number of colors, not a color itself */
};

enum led_port { LEFT_PORT = 0, RIGHT_PORT };

static void led_set_color_battery(int port, enum led_color color)
{
	const struct gpio_dt_spec *amber_led, *white_led;

	if (port == LEFT_PORT) {
		amber_led = GPIO_DT_FROM_NODELABEL(gpio_c0_charger_led_amber_l);
		white_led = GPIO_DT_FROM_NODELABEL(gpio_c0_charger_led_white_l);
	} else if (port == RIGHT_PORT) {
		amber_led = GPIO_DT_FROM_NODELABEL(gpio_c1_charger_led_amber_l);
		white_led = GPIO_DT_FROM_NODELABEL(gpio_c1_charger_led_white_l);
	}

	switch (color) {
	case LED_WHITE:
		gpio_pin_set_dt(white_led, BAT_LED_ON);
		gpio_pin_set_dt(amber_led, BAT_LED_OFF);
		break;
	case LED_AMBER:
		gpio_pin_set_dt(white_led, BAT_LED_OFF);
		gpio_pin_set_dt(amber_led, BAT_LED_ON);
		break;
	case LED_OFF:
		gpio_pin_set_dt(white_led, BAT_LED_OFF);
		gpio_pin_set_dt(amber_led, BAT_LED_OFF);
		break;
	default:
		break;
	}
}

void led_get_brightness_range(enum ec_led_id led_id, uint8_t *brightness_range)
{
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
	int port = charge_manager_get_active_charge_port();

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
	static int suspend_ticks;

	battery_ticks++;

	/*
	 * Override battery LEDs for Yavista, Yavista is non-power LED
	 * design, blinking both two side battery white LEDs to indicate
	 * system suspend with non-charging state.
	 */
	if (chipset_in_state(CHIPSET_STATE_ANY_SUSPEND) &&
	    led_pwr_get_state() != LED_PWRS_CHARGE) {
		suspend_ticks++;

		led_set_color_battery(RIGHT_PORT,
				      suspend_ticks % LED_TICKS_PER_CYCLE_S3 <
						      POWER_LED_ON_S3_TICKS ?
					      LED_WHITE :
					      LED_OFF);
		led_set_color_battery(LEFT_PORT,
				      suspend_ticks % LED_TICKS_PER_CYCLE_S3 <
						      POWER_LED_ON_S3_TICKS ?
					      LED_WHITE :
					      LED_OFF);
		return;
	}

	suspend_ticks = 0;

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

/* Called by hook task every TICK(IT83xx 500ms) */
static void led_tick(void)
{
	led_set_battery();
}
DECLARE_HOOK(HOOK_TICK, led_tick, HOOK_PRIO_DEFAULT);
