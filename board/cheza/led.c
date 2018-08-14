/* Copyright 2018 The Chromium OS Authors. All rights reserved.
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
#include "gpio.h"
#include "hooks.h"
#include "host_command.h"
#include "led_common.h"
#include "system.h"
#include "util.h"

#define BAT_LED_ON 1
#define BAT_LED_OFF 0

const enum ec_led_id supported_led_ids[] = {
	EC_LED_ID_RIGHT_LED,
	EC_LED_ID_LEFT_LED,
};

const int supported_led_ids_count = ARRAY_SIZE(supported_led_ids);

enum led_color {
	LED_OFF = 0,
	LED_AMBER,
	LED_WHITE,
	LED_COLOR_COUNT  /* Number of colors, not a color itself */
};

static void side_led_set_color(int port, enum led_color color)
{
	gpio_set_level(port ? GPIO_CHG_LED_Y_C1 : GPIO_CHG_LED_Y_C0,
		(color == LED_AMBER) ? BAT_LED_ON : BAT_LED_OFF);
	gpio_set_level(port ? GPIO_CHG_LED_W_C1 : GPIO_CHG_LED_W_C0,
		(color == LED_WHITE) ? BAT_LED_ON : BAT_LED_OFF);
}

void led_get_brightness_range(enum ec_led_id led_id, uint8_t *brightness_range)
{
	brightness_range[EC_LED_COLOR_AMBER] = 1;
	brightness_range[EC_LED_COLOR_WHITE] = 1;
}

int led_set_brightness(enum ec_led_id led_id, const uint8_t *brightness)
{
	int port;

	switch (led_id) {
	case EC_LED_ID_RIGHT_LED:
		port = 0;
		break;
	case EC_LED_ID_LEFT_LED:
		port = 1;
		break;
	default:
		return EC_ERROR_PARAM1;
	}

	if (brightness[EC_LED_COLOR_WHITE] != 0)
		side_led_set_color(port, LED_WHITE);
	else if (brightness[EC_LED_COLOR_AMBER] != 0)
		side_led_set_color(port, LED_AMBER);
	else
		side_led_set_color(port, LED_OFF);

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
		side_led_set_color(0, (port == 0) ? color : LED_OFF);
	if (led_auto_control_is_enabled(EC_LED_ID_LEFT_LED))
		side_led_set_color(1, (port == 1) ? color : LED_OFF);
}

static void board_led_set_battery(void)
{
	static int battery_ticks;
	uint32_t chflags = charge_get_flags();

	battery_ticks++;

	switch (charge_get_state()) {
	case PWR_STATE_CHARGE:
		/* Always indicate when charging, even in suspend. */
		set_active_port_color(LED_AMBER);
		break;
	case PWR_STATE_DISCHARGE:
		if (led_auto_control_is_enabled(EC_LED_ID_RIGHT_LED)) {
			if (charge_get_percent() <= 10)
				side_led_set_color(0,
				   (battery_ticks & 0x4) ? LED_WHITE : LED_OFF);
			else
				side_led_set_color(0, LED_OFF);
		}

		if (led_auto_control_is_enabled(EC_LED_ID_LEFT_LED))
			side_led_set_color(1, LED_OFF);
		break;
	case PWR_STATE_ERROR:
		set_active_port_color((battery_ticks & 0x2) ?
				LED_WHITE : LED_OFF);
		break;
	case PWR_STATE_CHARGE_NEAR_FULL:
		set_active_port_color(LED_WHITE);
		break;
	case PWR_STATE_IDLE: /* External power connected in IDLE */
		if (chflags & CHARGE_FLAG_FORCE_IDLE)
			set_active_port_color((battery_ticks & 0x4) ?
					LED_AMBER : LED_OFF);
		else
			set_active_port_color(LED_WHITE);
		break;
	default:
		/* Other states don't alter LED behavior */
		break;
	}
}

/* Called by hook task every TICK */
static void led_tick(void)
{
	board_led_set_battery();
}
DECLARE_HOOK(HOOK_TICK, led_tick, HOOK_PRIO_DEFAULT);

void led_control(enum ec_led_id led_id, enum ec_led_state state)
{
	enum led_color color;

	if ((led_id != EC_LED_ID_RECOVERY_HW_REINIT_LED) &&
	    (led_id != EC_LED_ID_SYSRQ_DEBUG_LED))
		return;

	if (state == LED_STATE_RESET) {
		led_auto_control(EC_LED_ID_LEFT_LED, 1);
		led_auto_control(EC_LED_ID_RIGHT_LED, 1);
		board_led_set_battery();
		return;
	}

	color = state ? LED_WHITE : LED_OFF;

	led_auto_control(EC_LED_ID_LEFT_LED, 0);
	led_auto_control(EC_LED_ID_RIGHT_LED, 0);

	side_led_set_color(0, color);
	side_led_set_color(1, color);
}
