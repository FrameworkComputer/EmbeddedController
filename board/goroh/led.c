/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "ec_commands.h"
#include "gpio.h"
#include "led_common.h"
#include "led_onoff_states.h"
#include "chipset.h"

const int led_charge_lvl_1 = 5;
const int led_charge_lvl_2 = 95;

struct led_descriptor led_bat_state_table[LED_NUM_STATES][LED_NUM_PHASES] = {
	[STATE_CHARGING_LVL_1]	     = {{EC_LED_COLOR_AMBER, LED_INDEFINITE} },
	[STATE_CHARGING_LVL_2]	     = {{EC_LED_COLOR_AMBER, LED_INDEFINITE} },
	[STATE_CHARGING_FULL_CHARGE] = {{EC_LED_COLOR_WHITE,  LED_INDEFINITE} },
	[STATE_DISCHARGE_S0]	     = {{LED_OFF,  LED_INDEFINITE} },
	[STATE_DISCHARGE_S0_BAT_LOW] = {{EC_LED_COLOR_AMBER,  1 * LED_ONE_SEC},
					{LED_OFF,	    3 * LED_ONE_SEC} },
	[STATE_DISCHARGE_S3]	     = {{LED_OFF,  LED_INDEFINITE} },
	[STATE_DISCHARGE_S5]         = {{LED_OFF,  LED_INDEFINITE} },
	[STATE_BATTERY_ERROR]        = {{EC_LED_COLOR_AMBER,  1 * LED_ONE_SEC},
					{LED_OFF,	    1 * LED_ONE_SEC} },
	[STATE_FACTORY_TEST]         = {{EC_LED_COLOR_WHITE,   2 * LED_ONE_SEC},
					{EC_LED_COLOR_AMBER, 2 * LED_ONE_SEC} },
};

const struct led_descriptor
		led_pwr_state_table[PWR_LED_NUM_STATES][LED_NUM_PHASES] = {
	[PWR_LED_STATE_ON]           = {{EC_LED_COLOR_WHITE, LED_INDEFINITE} },
	[PWR_LED_STATE_SUSPEND_AC]   = {{EC_LED_COLOR_WHITE, 1 * LED_ONE_SEC},
					{LED_OFF,	     3 * LED_ONE_SEC} },
	[PWR_LED_STATE_SUSPEND_NO_AC] = {{EC_LED_COLOR_WHITE, 1 * LED_ONE_SEC},
					{LED_OFF,	     3 * LED_ONE_SEC} },
	[PWR_LED_STATE_OFF]           = {{LED_OFF, LED_INDEFINITE} },
};


const enum ec_led_id supported_led_ids[] = {
	EC_LED_ID_BATTERY_LED,
	EC_LED_ID_POWER_LED,
};

const int supported_led_ids_count = ARRAY_SIZE(supported_led_ids);

void led_set_color_battery(enum ec_led_colors color)
{
}

void led_set_color_power(enum ec_led_colors color)
{
}

void led_get_brightness_range(enum ec_led_id led_id, uint8_t *brightness_range)
{
}

int led_set_brightness(enum ec_led_id led_id, const uint8_t *brightness)
{
	return EC_SUCCESS;
}
