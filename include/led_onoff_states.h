/* Copyright 2018 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Common functions for stateful LEDs (charger and power)
 */

#ifndef __CROS_EC_ONOFFSTATES_LED_H
#define __CROS_EC_ONOFFSTATES_LED_H

#include "ec_commands.h"

#ifdef __cplusplus
extern "C" {
#endif

#define LED_INDEFINITE UINT8_MAX
#define LED_ONE_SEC (1000 / HOOK_TICK_INTERVAL_MS)
#define LED_OFF EC_LED_COLOR_COUNT

/*
 * All LED states should have one phase defined,
 * and an additional phase can be defined for blinking
 */
enum led_phase { LED_PHASE_0, LED_PHASE_1, LED_NUM_PHASES };

/*
 * STATE_CHARGING_LVL_1 is when 0 <= charge_percentage < led_charge_level_1
 * STATE_CHARGING_LVL_2 is when led_charge_level_1 <=
 * charge_percentage < led_charge_level_2.
 * STATE_CHARGING_FULL_CHARGE is when
 * led_charge_level_2 <= charge_percentage < 100.
 *
 * STATE_CHARGING_FULL_S5 is optional and state machine will fall back to
 *	FULL_CHARGE if not defined
 */
enum led_states {
	STATE_CHARGING_LVL_1,
	STATE_CHARGING_LVL_2,
	STATE_CHARGING_FULL_CHARGE,
	STATE_CHARGING_FULL_S5,
	STATE_DISCHARGE_S0,
	STATE_DISCHARGE_S0_BAT_LOW,
	STATE_DISCHARGE_S3,
	STATE_DISCHARGE_S5,
	STATE_BATTERY_ERROR,
	STATE_FACTORY_TEST,
	LED_NUM_STATES
};

struct led_descriptor {
	enum ec_led_colors color;
	uint8_t time;
};

extern const int led_charge_lvl_1;
extern const int led_charge_lvl_2;

enum pwr_led_states {
	PWR_LED_STATE_ON,
	PWR_LED_STATE_SUSPEND_AC,
	PWR_LED_STATE_SUSPEND_NO_AC,
	PWR_LED_STATE_OFF,
	PWR_LED_STATE_OFF_LOW_POWER, /* Falls back to OFF if not defined */
	PWR_LED_NUM_STATES
};

/**
 * Set battery LED color - defined in board's led.c if supported, along with:
 *	- led_bat_state_table
 *	- led_charge_lvl_1
 *	- led_charge_lvl_2
 *
 * @param color		Color to set on battery LED
 *
 */
__override_proto void led_set_color_battery(enum ec_led_colors color);

/**
 * Set power LED color - defined in board's led.c if supported, along with:
 *	- led_pwr_state_table
 */
__override_proto void led_set_color_power(enum ec_led_colors color);

__override_proto enum led_states
board_get_led_state(enum led_states desired_state);

#ifdef __cplusplus
}
#endif

#endif /* __CROS_EC_ONOFFSTATES_LED_H */
