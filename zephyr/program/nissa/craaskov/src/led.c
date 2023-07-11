/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Battery LED control for nissa
 */
#include "common.h"
#include "ec_commands.h"
#include "led_common.h"
#include "led_onoff_states.h"
#include "led_pwm.h"

__override const int led_charge_lvl_1 = 5;
__override const int led_charge_lvl_2 = 97;
__override struct led_descriptor
	led_bat_state_table[LED_NUM_STATES][LED_NUM_PHASES] = {
		[STATE_CHARGING_LVL_1] = { { EC_LED_COLOR_AMBER,
					     LED_INDEFINITE } },
		[STATE_CHARGING_LVL_2] = { { EC_LED_COLOR_AMBER,
					     LED_INDEFINITE } },
		[STATE_CHARGING_FULL_CHARGE] = { { EC_LED_COLOR_BLUE,
						   LED_INDEFINITE } },
		[STATE_DISCHARGE_S0] = { { EC_LED_COLOR_BLUE,
					   LED_INDEFINITE } },
		[STATE_DISCHARGE_S0_BAT_LOW] = { { EC_LED_COLOR_AMBER,
						   LED_INDEFINITE } },
		[STATE_DISCHARGE_S3] = { { LED_OFF, LED_INDEFINITE } },
		[STATE_DISCHARGE_S5] = { { LED_OFF, LED_INDEFINITE } },
		[STATE_BATTERY_ERROR] = { { EC_LED_COLOR_AMBER,
					    1 * LED_ONE_SEC },
					  { LED_OFF, 1 * LED_ONE_SEC } },
		[STATE_FACTORY_TEST] = { { EC_LED_COLOR_AMBER,
					   2 * LED_ONE_SEC },
					 { EC_LED_COLOR_BLUE,
					   2 * LED_ONE_SEC } },
	};

__override void led_set_color_battery(enum ec_led_colors color)
{
	switch (color) {
	case EC_LED_COLOR_BLUE:
		set_pwm_led_color(PWM_LED0, EC_LED_COLOR_BLUE);
		break;
	case EC_LED_COLOR_AMBER:
		set_pwm_led_color(PWM_LED0, EC_LED_COLOR_AMBER);
		break;
	default: /* LED_OFF and other unsupported colors */
		set_pwm_led_color(PWM_LED0, -1);
		break;
	}
}
