/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "chipset.h"
#include "common.h"
#include "gpio.h"
#include "led_common.h"
#include "led_onoff_states.h"
#include "pwm.h"

#define LED_OFF_LVL	0
#define LED_ON_LVL	1

__override const int led_charge_lvl_1 = 5;
__override const int led_charge_lvl_2 = 95;

__override struct led_descriptor
			led_bat_state_table[LED_NUM_STATES][LED_NUM_PHASES] = {
	[STATE_CHARGING_LVL_1]	     = {{EC_LED_COLOR_AMBER, LED_INDEFINITE} },
	[STATE_CHARGING_LVL_2]	     = {{EC_LED_COLOR_AMBER, LED_INDEFINITE} },
	[STATE_CHARGING_FULL_CHARGE] = {{EC_LED_COLOR_BLUE,  LED_INDEFINITE} },
	[STATE_CHARGING_FULL_S5]     = {{EC_LED_COLOR_BLUE,  LED_INDEFINITE} },
	[STATE_DISCHARGE_S0]	     = {{EC_LED_COLOR_BLUE,  LED_INDEFINITE} },
	[STATE_DISCHARGE_S0_BAT_LOW] = {{EC_LED_COLOR_BLUE,  LED_INDEFINITE} },
	[STATE_DISCHARGE_S3]	     = {{EC_LED_COLOR_AMBER,  1 * LED_ONE_SEC},
					{LED_OFF,           3 * LED_ONE_SEC} },
	[STATE_DISCHARGE_S5]         = {{LED_OFF,  LED_INDEFINITE} },
	[STATE_BATTERY_ERROR]        = {{EC_LED_COLOR_AMBER,  1 * LED_ONE_SEC},
					{LED_OFF,	    1 * LED_ONE_SEC} },
	[STATE_FACTORY_TEST]         = {{EC_LED_COLOR_BLUE,   2 * LED_ONE_SEC},
					{EC_LED_COLOR_AMBER, 2 * LED_ONE_SEC} },
};

const enum ec_led_id supported_led_ids[] = {
	EC_LED_ID_BATTERY_LED,
};
const int supported_led_ids_count = ARRAY_SIZE(supported_led_ids);

__override void led_set_color_battery(enum ec_led_colors color)
{
	switch (color) {
	case EC_LED_COLOR_AMBER:
		pwm_enable(PWM_CH_LED1, LED_ON_LVL);
		pwm_enable(PWM_CH_LED3, LED_OFF_LVL);
		break;
	case EC_LED_COLOR_BLUE:
		pwm_enable(PWM_CH_LED1, LED_OFF_LVL);
		pwm_enable(PWM_CH_LED3, LED_ON_LVL);
		break;
	default: /* LED_OFF and other unsupported colors */
		pwm_enable(PWM_CH_LED1, LED_OFF_LVL);
		pwm_enable(PWM_CH_LED3, LED_OFF_LVL);
		break;
	}
}

void led_get_brightness_range(enum ec_led_id led_id, uint8_t *brightness_range)
{
	brightness_range[EC_LED_COLOR_AMBER] = 1;
	brightness_range[EC_LED_COLOR_BLUE] = 1;
}

int led_set_brightness(enum ec_led_id led_id, const uint8_t *brightness)
{
	if (brightness[EC_LED_COLOR_AMBER] != 0)
		led_set_color_battery(EC_LED_COLOR_AMBER);
	else if (brightness[EC_LED_COLOR_BLUE] != 0)
		led_set_color_battery(EC_LED_COLOR_BLUE);
	else
		led_set_color_battery(LED_OFF);
	return EC_SUCCESS;
}

__override enum led_states board_led_get_state(enum led_states desired_state)
{
	if (desired_state == STATE_BATTERY_ERROR) {
		if (chipset_in_state(CHIPSET_STATE_ON))
			return desired_state;
		else if (chipset_in_state(CHIPSET_STATE_ANY_SUSPEND))
			return STATE_DISCHARGE_S3;
		else
			return STATE_DISCHARGE_S5;
	}
	return desired_state;
}
