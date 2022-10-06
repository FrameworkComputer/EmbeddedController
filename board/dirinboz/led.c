/* Copyright 2020 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "battery.h"
#include "charge_manager.h"
#include "charge_state.h"
#include "chipset.h"
#include "cros_board_info.h"
#include "ec_commands.h"
#include "gpio.h"
#include "host_command.h"
#include "led_common.h"
#include "hooks.h"

#define LED_TICKS_PER_CYCLE 10
#define LED_ON_TICKS 5

const enum ec_led_id supported_led_ids[] = {
	EC_LED_ID_LEFT_LED,
	EC_LED_ID_RIGHT_LED,
};

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
	int amber_led, white_led;
	uint32_t board_ver = 0;
	int led_batt_on_lvl, led_batt_off_lvl;

	cbi_get_board_version(&board_ver);
	amber_led = (port == LEFT_PORT ? GPIO_LED_CHRG_L :
					 IOEX_C1_CHARGER_LED_AMBER_DB);
	white_led = (port == LEFT_PORT ? GPIO_LED_FULL_L :
					 IOEX_C1_CHARGER_LED_WHITE_DB);

	if ((board_ver >= 3) && (port == RIGHT_PORT)) {
		led_batt_on_lvl = 1;
		led_batt_off_lvl = 0;
	} else {
		led_batt_on_lvl = 0;
		led_batt_off_lvl = 1;
	}

	switch (color) {
	case LED_WHITE:
		gpio_or_ioex_set_level(white_led, led_batt_on_lvl);
		gpio_or_ioex_set_level(amber_led, led_batt_off_lvl);
		break;
	case LED_AMBER:
		gpio_or_ioex_set_level(white_led, led_batt_off_lvl);
		gpio_or_ioex_set_level(amber_led, led_batt_on_lvl);
		break;
	case LED_OFF:
		gpio_or_ioex_set_level(white_led, led_batt_off_lvl);
		gpio_or_ioex_set_level(amber_led, led_batt_off_lvl);
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
	static int battery_ticks;
	static int power_ticks;

	battery_ticks++;

	/*
	 * Override battery LEDs for Berknip, Berknip is non-power LED
	 * design, blinking both two side battery white LEDs to indicate
	 * system suspend with non-charging state.
	 */
	if (chipset_in_state(CHIPSET_STATE_SUSPEND | CHIPSET_STATE_STANDBY) &&
	    charge_get_state() != PWR_STATE_CHARGE) {
		power_ticks++;

		led_set_color_battery(RIGHT_PORT,
				      power_ticks & 0x4 ? LED_WHITE : LED_OFF);
		led_set_color_battery(LEFT_PORT,
				      power_ticks & 0x4 ? LED_WHITE : LED_OFF);
		return;
	}

	power_ticks = 0;

	switch (charge_get_state()) {
	case PWR_STATE_CHARGE:
		/* Always indicate when charging, even in suspend. */
		set_active_port_color(LED_AMBER);
		break;
	case PWR_STATE_DISCHARGE:
		if (led_auto_control_is_enabled(EC_LED_ID_RIGHT_LED)) {
			if (charge_get_percent() < 10)
				led_set_color_battery(
					RIGHT_PORT,
					(battery_ticks % LED_TICKS_PER_CYCLE <
					 LED_ON_TICKS) ?
						LED_WHITE :
						LED_OFF);
			else
				led_set_color_battery(RIGHT_PORT, LED_OFF);
		}

		if (led_auto_control_is_enabled(EC_LED_ID_LEFT_LED))
			led_set_color_battery(LEFT_PORT, LED_OFF);
		break;
	case PWR_STATE_ERROR:
		set_active_port_color((battery_ticks & 0x2) ? LED_WHITE :
							      LED_OFF);
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

/* Called by hook task every TICK */
static void led_tick(void)
{
	led_set_battery();
}
DECLARE_HOOK(HOOK_TICK, led_tick, HOOK_PRIO_DEFAULT);
