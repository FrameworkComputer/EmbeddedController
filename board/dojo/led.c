/* Copyright 2022 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "battery.h"
#include "charge_manager.h"
#include "charge_state.h"
#include "chipset.h"
#include "ec_commands.h"
#include "hooks.h"
#include "host_command.h"
#include "led_common.h"
#include "pwm.h"
#include "system.h"
#include "util.h"

/* Times of tick per 1 second */
#define TIMES_TICK_ONE_SEC (1000 / HOOK_TICK_INTERVAL_MS)
/* Times of tick per half second */
#define TIMES_TICK_HALF_SEC (500 / HOOK_TICK_INTERVAL_MS)

#define BAT_LED_ON 1
#define BAT_LED_OFF 0

#define PWR_LED_ON 1
#define PWR_LED_OFF 0

const enum ec_led_id supported_led_ids[] = {
	EC_LED_ID_RIGHT_LED,
	EC_LED_ID_LEFT_LED,
	EC_LED_ID_POWER_LED,
};

const int supported_led_ids_count = ARRAY_SIZE(supported_led_ids);

enum led_color {
	LED_OFF = 0,
	LED_AMBER,
	LED_WHITE,
	LED_COLOR_COUNT  /* Number of colors, not a color itself */
};

enum led_port {
	RIGHT_PORT = 0,
	LEFT_PORT,
};

static void battery_led_set_color(enum led_port port, enum led_color color)
{
	pwm_enable(port ? PWM_CH_LED_C1_AMBER : PWM_CH_LED_C0_AMBER,
	           (color == LED_AMBER) ? BAT_LED_ON : BAT_LED_OFF);
	pwm_enable(port ? PWM_CH_LED_C1_WHITE : PWM_CH_LED_C0_WHITE,
	           (color == LED_WHITE) ? BAT_LED_ON : BAT_LED_OFF);
}

static void power_led_set_color(enum led_color color)
{
	pwm_enable(PWM_CH_LED_PWR,
	           (color == LED_WHITE) ? PWR_LED_ON : PWR_LED_OFF);
}

void led_get_brightness_range(enum ec_led_id led_id, uint8_t *brightness_range)
{
	switch (led_id) {
	case EC_LED_ID_RIGHT_LED:
		brightness_range[EC_LED_COLOR_AMBER] = 1;
		brightness_range[EC_LED_COLOR_WHITE] = 1;
		break;
	case EC_LED_ID_LEFT_LED:
		brightness_range[EC_LED_COLOR_AMBER] = 1;
		brightness_range[EC_LED_COLOR_WHITE] = 1;
		break;
	case EC_LED_ID_POWER_LED:
		brightness_range[EC_LED_COLOR_AMBER] = 1;
		brightness_range[EC_LED_COLOR_WHITE] = 1;
		break;
	default:
		break;
	}
}

static int led_set_color(enum ec_led_id led_id, enum led_color color)
{
	switch (led_id) {
		case EC_LED_ID_RIGHT_LED:
			battery_led_set_color(RIGHT_PORT, color);
			break;
		case EC_LED_ID_LEFT_LED:
			battery_led_set_color(LEFT_PORT, color);
			break;
		case EC_LED_ID_POWER_LED:
			power_led_set_color(color);
			break;
		default:
			return EC_ERROR_UNKNOWN;
	}

	return EC_SUCCESS;
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
 * Set active charge port color to the parameter, turn off all others.
 * If no port is active (-1), turn off all LEDs.
 */
static void set_active_port_color(enum led_color color)
{
	int port = charge_manager_get_active_charge_port();

	if (led_auto_control_is_enabled(EC_LED_ID_RIGHT_LED))
		battery_led_set_color(RIGHT_PORT,
		                      (port == RIGHT_PORT) ? color : LED_OFF);
	if (led_auto_control_is_enabled(EC_LED_ID_LEFT_LED))
		battery_led_set_color(LEFT_PORT,
		                      (port == LEFT_PORT) ? color : LED_OFF);
}

static void board_led_set_battery(void)
{
	static int battery_ticks;
	int battery_led_blink_cycle;
	uint32_t chflags = charge_get_flags();

	battery_ticks++;

	switch (charge_get_state()) {
	case PWR_STATE_CHARGE:
		/* Always indicate when charging, even in suspend. */
		set_active_port_color(LED_AMBER);
		break;
	case PWR_STATE_DISCHARGE:
		if (charge_get_percent() <= 10) {
			battery_led_blink_cycle = battery_ticks %
			                          (2 * TIMES_TICK_ONE_SEC);
			if (led_auto_control_is_enabled(EC_LED_ID_RIGHT_LED))
				battery_led_set_color(RIGHT_PORT,
				                      (battery_led_blink_cycle <
				                      TIMES_TICK_ONE_SEC) ?
				                      LED_AMBER : LED_OFF);
			if (led_auto_control_is_enabled(EC_LED_ID_LEFT_LED))
				battery_led_set_color(LEFT_PORT,
				                      (battery_led_blink_cycle <
				                      TIMES_TICK_ONE_SEC) ?
				                      LED_AMBER : LED_OFF);
		} else {
			if (led_auto_control_is_enabled(EC_LED_ID_RIGHT_LED))
				battery_led_set_color(RIGHT_PORT, LED_OFF);
			if (led_auto_control_is_enabled(EC_LED_ID_LEFT_LED))
				battery_led_set_color(LEFT_PORT, LED_OFF);
		}
		break;
	case PWR_STATE_ERROR:
		battery_led_blink_cycle = battery_ticks % TIMES_TICK_ONE_SEC;
		if (led_auto_control_is_enabled(EC_LED_ID_RIGHT_LED))
			battery_led_set_color(RIGHT_PORT,
			                      (battery_led_blink_cycle <
			                      TIMES_TICK_HALF_SEC) ?
			                      LED_AMBER : LED_OFF);
		if (led_auto_control_is_enabled(EC_LED_ID_LEFT_LED))
			battery_led_set_color(LEFT_PORT,
			                      (battery_led_blink_cycle <
			                      TIMES_TICK_HALF_SEC) ?
			                      LED_AMBER : LED_OFF);
		break;
	case PWR_STATE_CHARGE_NEAR_FULL:
		set_active_port_color(LED_WHITE);
		break;
	case PWR_STATE_IDLE: /* External power connected in IDLE */
		if (chflags & CHARGE_FLAG_FORCE_IDLE) {
			battery_led_blink_cycle = battery_ticks %
			                          (2 * TIMES_TICK_ONE_SEC);
			set_active_port_color((battery_led_blink_cycle <
			                      TIMES_TICK_ONE_SEC) ?
			                      LED_AMBER : LED_OFF);
		} else
			set_active_port_color(LED_WHITE);
		break;
	default:
		/* Other states don't alter LED behavior */
		break;
	}
}

static void board_led_set_power(void)
{
	static int power_ticks;
	int power_led_blink_cycle;

	power_ticks++;

	if (chipset_in_state(CHIPSET_STATE_ON)) {
		power_led_set_color(LED_WHITE);
	} else if (chipset_in_state(CHIPSET_STATE_ANY_SUSPEND)) {
		power_led_blink_cycle = power_ticks % (2 * TIMES_TICK_ONE_SEC);
		power_led_set_color(power_led_blink_cycle < TIMES_TICK_ONE_SEC ?
		                    LED_WHITE : LED_OFF);
	} else {
		power_led_set_color(LED_OFF);
	}
}

/* Called by hook task every TICK */
static void led_tick(void)
{
	if(led_auto_control_is_enabled(EC_LED_ID_POWER_LED))
		board_led_set_power();

	board_led_set_battery();
}
DECLARE_HOOK(HOOK_TICK, led_tick, HOOK_PRIO_DEFAULT);
