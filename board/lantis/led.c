/* Copyright 2020 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Lantis specific LED settings. */

#include "cbi_fw_config.h"
#include "charge_manager.h"
#include "charge_state.h"
#include "extpower.h"
#include "gpio.h"
#include "hooks.h"
#include "led_common.h"

#define BAT_LED_ON 0
#define BAT_LED_OFF 1

#define POWER_LED_ON 0
#define POWER_LED_OFF 1

const enum ec_led_id supported_led_ids[] = { EC_LED_ID_LEFT_LED,
					     EC_LED_ID_RIGHT_LED,
					     EC_LED_ID_POWER_LED };

const int supported_led_ids_count = ARRAY_SIZE(supported_led_ids);

enum led_color {
	LED_OFF = 0,
	LED_AMBER,
	LED_WHITE,
	LED_COLOR_COUNT /* Number of colors, not a color itself */
};

enum led_port { LEFT_PORT = 0, RIGHT_PORT };

static int led_set_color_battery(int port, enum led_color color)
{
	enum gpio_signal amber_led, white_led;

	amber_led = (port == RIGHT_PORT ? GPIO_BAT_LED_AMBER_C1 :
					  GPIO_BAT_LED_AMBER_C0);
	white_led = (port == RIGHT_PORT ? GPIO_BAT_LED_WHITE_C1 :
					  GPIO_BAT_LED_WHITE_C0);

	switch (color) {
	case LED_OFF:
		gpio_set_level(white_led, BAT_LED_OFF);
		gpio_set_level(amber_led, BAT_LED_OFF);
		break;
	case LED_WHITE:
		gpio_set_level(white_led, BAT_LED_ON);
		gpio_set_level(amber_led, BAT_LED_OFF);
		break;
	case LED_AMBER:
		gpio_set_level(white_led, BAT_LED_OFF);
		gpio_set_level(amber_led, BAT_LED_ON);
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
		gpio_set_level(GPIO_PWR_LED_WHITE_L, POWER_LED_OFF);
		break;
	case LED_WHITE:
		gpio_set_level(GPIO_PWR_LED_WHITE_L, POWER_LED_ON);
		break;
	default:
		return EC_ERROR_UNKNOWN;
	}
	return EC_SUCCESS;
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
	case EC_LED_ID_POWER_LED:
		brightness_range[EC_LED_COLOR_WHITE] = 1;
		break;
	default:
		break;
	}
}

static int led_set_color(enum ec_led_id led_id, enum led_color color)
{
	int rv;

	switch (led_id) {
	case EC_LED_ID_RIGHT_LED:
		rv = led_set_color_battery(RIGHT_PORT, color);
		break;
	case EC_LED_ID_LEFT_LED:
		rv = led_set_color_battery(LEFT_PORT, color);
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

/*
 * lantis use old led policy.
 * Use cbi fw_config to distinguish lantis from other boards.
 *		numeric_pad	tablet mode
 * lantis	N		N
 * landrid	Y		N
 * landia	N		Y
 */
static bool is_led_old_policy(void)
{
	if (get_cbi_fw_config_numeric_pad() == NUMERIC_PAD_ABSENT &&
	    get_cbi_fw_config_tablet_mode() == TABLET_MODE_ABSENT)
		return 1;
	else
		return 0;
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
	static int battery_ticks;
	static int power_ticks;

	battery_ticks++;

	/*
	 * Override battery LED for clamshell SKU, which doesn't have power
	 * LED, blinking battery white LED to indicate system suspend without
	 * charging.
	 */
	if (get_cbi_fw_config_tablet_mode() == TABLET_MODE_ABSENT) {
		if (chipset_in_state(CHIPSET_STATE_ANY_SUSPEND) &&
		    charge_get_state() != PWR_STATE_CHARGE) {
			power_ticks++;

			led_set_color_battery(RIGHT_PORT, power_ticks & 0x2 ?
								  LED_WHITE :
								  LED_OFF);
			led_set_color_battery(LEFT_PORT, power_ticks & 0x2 ?
								 LED_WHITE :
								 LED_OFF);
			return;
		}
	}

	power_ticks = 0;

	switch (charge_get_state()) {
	case PWR_STATE_CHARGE:
		set_active_port_color(LED_AMBER);
		break;
	case PWR_STATE_DISCHARGE_FULL:
		if (extpower_is_present()) {
			set_active_port_color(LED_WHITE);
			break;
		}
		/* Intentional fall-through */
	case PWR_STATE_DISCHARGE:
		/*
		 * Blink white/amber light (1 sec on, 1 sec off)
		 * when battery capacity is less than 10%
		 */
		if (charge_get_percent() < 10) {
			if (is_led_old_policy()) {
				led_set_color_battery(RIGHT_PORT,
						      (battery_ticks & 0x2) ?
							      LED_WHITE :
							      LED_OFF);
			} else {
				if (led_auto_control_is_enabled(
					    EC_LED_ID_RIGHT_LED))
					led_set_color_battery(
						RIGHT_PORT,
						(battery_ticks & 0x2) ?
							LED_AMBER :
							LED_OFF);
				if (led_auto_control_is_enabled(
					    EC_LED_ID_LEFT_LED))
					led_set_color_battery(
						LEFT_PORT,
						(battery_ticks & 0x2) ?
							LED_AMBER :
							LED_OFF);
			}
		} else {
			set_active_port_color(LED_OFF);
		}
		break;
	case PWR_STATE_ERROR:
		if (is_led_old_policy())
			set_active_port_color(
				(battery_ticks % 0x2) ? LED_WHITE : LED_OFF);
		else
			set_active_port_color(
				(battery_ticks % 0x2) ? LED_AMBER : LED_OFF);
		break;
	case PWR_STATE_CHARGE_NEAR_FULL:
		set_active_port_color(LED_WHITE);
		break;
	case PWR_STATE_IDLE: /* External power connected in IDLE */
		set_active_port_color(LED_WHITE);
		break;
	case PWR_STATE_FORCED_IDLE:
		set_active_port_color((battery_ticks & 0x2) ? LED_AMBER :
							      LED_OFF);
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
	else if (chipset_in_state(CHIPSET_STATE_ANY_SUSPEND))
		led_set_color_power((power_tick & 0x2) ? LED_WHITE : LED_OFF);
	else
		led_set_color_power(LED_OFF);
}

/* Called by hook task every TICK */
static void led_tick(void)
{
	if (led_auto_control_is_enabled(EC_LED_ID_POWER_LED))
		led_set_power();

	led_set_battery();
}
DECLARE_HOOK(HOOK_TICK, led_tick, HOOK_PRIO_DEFAULT);
