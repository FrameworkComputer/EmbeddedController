/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "ec_commands.h"
#include "gpio.h"
#include "led_common.h"
#include "led_onoff_states.h"
#include "chipset.h"
#include "driver/bc12/mt6360.h"

__override const int led_charge_lvl_1 = 5;
__override const int led_charge_lvl_2 = 95;

__override struct led_descriptor
		led_bat_state_table[LED_NUM_STATES][LED_NUM_PHASES] = {
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

__override const struct led_descriptor
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

__override void led_set_color_battery(enum ec_led_colors color)
{
	mt6360_led_set_brightness(MT6360_LED_RGB2, 50);
	mt6360_led_set_brightness(MT6360_LED_RGB3, 50);

	switch (color) {
	case EC_LED_COLOR_AMBER:
		mt6360_led_enable(MT6360_LED_RGB2, 0);
		mt6360_led_enable(MT6360_LED_RGB3, 1);
		break;
	case EC_LED_COLOR_WHITE:
		mt6360_led_enable(MT6360_LED_RGB2, 1);
		mt6360_led_enable(MT6360_LED_RGB3, 0);
		break;
	default: /* LED_OFF and other unsupported colors */
		mt6360_led_enable(MT6360_LED_RGB2, 0);
		mt6360_led_enable(MT6360_LED_RGB3, 0);
		break;
	}
}

__override void led_set_color_power(enum ec_led_colors color)
{
	mt6360_led_set_brightness(MT6360_LED_RGB1, 1);
	mt6360_led_enable(MT6360_LED_RGB1, color == EC_LED_COLOR_WHITE);
}

void led_get_brightness_range(enum ec_led_id led_id, uint8_t *brightness_range)
{
	if (led_id == EC_LED_ID_BATTERY_LED) {
		brightness_range[EC_LED_COLOR_AMBER] =
				MT6360_LED_BRIGHTNESS_MAX;
		brightness_range[EC_LED_COLOR_WHITE] =
				MT6360_LED_BRIGHTNESS_MAX;
	} else if (led_id == EC_LED_ID_POWER_LED) {
		brightness_range[EC_LED_COLOR_WHITE] =
				MT6360_LED_BRIGHTNESS_MAX;
	}
}

int led_set_brightness(enum ec_led_id led_id, const uint8_t *brightness)
{
	if (led_id == EC_LED_ID_BATTERY_LED) {
		if (brightness[EC_LED_COLOR_AMBER] != 0)
			led_set_color_battery(EC_LED_COLOR_AMBER);
		else if (brightness[EC_LED_COLOR_WHITE] != 0)
			led_set_color_battery(EC_LED_COLOR_WHITE);
		else
			led_set_color_battery(LED_OFF);
	} else if (led_id == EC_LED_ID_POWER_LED) {
		if (brightness[EC_LED_COLOR_WHITE] != 0)
			led_set_color_power(EC_LED_COLOR_WHITE);
		else
			led_set_color_power(LED_OFF);
	}

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
