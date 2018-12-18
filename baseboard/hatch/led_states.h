/* Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Common functions for stateful LEDs (charger)
 */

#ifndef __CROS_EC_BASEBOARD_LED_H
#define __CROS_EC_BASEBOARD_LED_H

#include "ec_commands.h"

#define LED_INDEFINITE	UINT8_MAX
#define LED_ONE_SEC	(1000 / HOOK_TICK_INTERVAL_MS)
#define LED_OFF         EC_LED_COLOR_COUNT

/*
 * All LED states should have one phase defined,
 * and an additional phase can be defined for blinking
 */
enum led_phase {
	LED_PHASE_0,
	LED_PHASE_1,
	LED_NUM_PHASES
};

/*
 * STATE_CHARGING_LVL_1 is when 0 <= charge_percentage < led_charge_level_1
 * STATE_CHARGING_LVL_2 is when led_chg_level_1 <= chg_% < led_charge_level_2
 * STATE_CHARGING_FULL_CHARGE is when led_charge_level_2 <= charge_% < 100
 */
enum led_states {
	STATE_CHARGING_LVL_1,
	STATE_CHARGING_LVL_2,
	STATE_CHARGING_FULL_CHARGE,
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


/* Charging LED state table - defined in board's led.c */
extern struct led_descriptor
			led_bat_state_table[LED_NUM_STATES][LED_NUM_PHASES];

/* Charging LED state level 1 - defined in board's led.c */
extern const int led_charge_lvl_1;

/* Charging LED state level 2 - defined in board's led.c */
extern const int led_charge_lvl_2;

/**
 * Set battery LED color - defined in board's led.c
 *
 * @param color		Color to set on battery LED
 *
 */
void led_set_color_battery(enum ec_led_colors color);

#endif /* __CROS_EC_BASEBOARD_LED_H */
