/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Power and battery LED control for Bellis
 */
#include "common.h"
#include "ioexpander.h"
#include "driver/ioexpander/it8801.h"
#include "ec_commands.h"
#include "led_common.h"
#include "led_onoff_states.h"

#define LED_ON_LVL 0
#define LED_OFF_LVL 1

__override const int led_charge_lvl_1 = 5;
__override const int led_charge_lvl_2 = 97;

__override struct led_descriptor
	led_bat_state_table[LED_NUM_STATES][LED_NUM_PHASES] = {
		[STATE_CHARGING_LVL_1] = { { EC_LED_COLOR_RED,
					     LED_INDEFINITE } },
		[STATE_CHARGING_LVL_2] = { { EC_LED_COLOR_AMBER,
					     LED_INDEFINITE } },
		[STATE_CHARGING_FULL_CHARGE] = { { EC_LED_COLOR_GREEN,
						   LED_INDEFINITE } },
		[STATE_DISCHARGE_S0] = { { LED_OFF, LED_INDEFINITE } },
		[STATE_DISCHARGE_S3] = { { LED_OFF, LED_INDEFINITE } },
		[STATE_DISCHARGE_S5] = { { LED_OFF, LED_INDEFINITE } },
		[STATE_BATTERY_ERROR] = { { EC_LED_COLOR_RED, 1 * LED_ONE_SEC },
					  { LED_OFF, 1 * LED_ONE_SEC } },
		[STATE_FACTORY_TEST] = { { EC_LED_COLOR_RED, 2 * LED_ONE_SEC },
					 { EC_LED_COLOR_GREEN,
					   2 * LED_ONE_SEC } },
	};

__override const struct led_descriptor
	led_pwr_state_table[PWR_LED_NUM_STATES][LED_NUM_PHASES] = {
		[PWR_LED_STATE_ON] = { { EC_LED_COLOR_WHITE, LED_INDEFINITE } },
		[PWR_LED_STATE_SUSPEND_AC] = { { EC_LED_COLOR_WHITE,
						 1 * LED_ONE_SEC },
					       { LED_OFF, 3 * LED_ONE_SEC } },
		[PWR_LED_STATE_SUSPEND_NO_AC] = { { EC_LED_COLOR_WHITE,
						    1 * LED_ONE_SEC },
						  { LED_OFF,
						    3 * LED_ONE_SEC } },
		[PWR_LED_STATE_OFF] = { { LED_OFF, LED_INDEFINITE } },
	};

const enum ec_led_id supported_led_ids[] = { EC_LED_ID_BATTERY_LED,
					     EC_LED_ID_POWER_LED };
const int supported_led_ids_count = ARRAY_SIZE(supported_led_ids);

__override void led_set_color_battery(enum ec_led_colors color)
{
	switch (color) {
	case EC_LED_COLOR_GREEN:
		ioex_set_level(IOEX_BAT_LED_GREEN_FULL_L, LED_ON_LVL);
		ioex_set_level(IOEX_BAT_LED_RED_L, LED_OFF_LVL);
		break;
	case EC_LED_COLOR_AMBER:
		ioex_set_level(IOEX_BAT_LED_GREEN_FULL_L, LED_ON_LVL);
		ioex_set_level(IOEX_BAT_LED_RED_L, LED_ON_LVL);
		break;
	case EC_LED_COLOR_RED:
		ioex_set_level(IOEX_BAT_LED_GREEN_FULL_L, LED_OFF_LVL);
		ioex_set_level(IOEX_BAT_LED_RED_L, LED_ON_LVL);
		break;
	default: /* LED_OFF and other unsupported colors */
		ioex_set_level(IOEX_BAT_LED_GREEN_FULL_L, LED_OFF_LVL);
		ioex_set_level(IOEX_BAT_LED_RED_L, LED_OFF_LVL);
		break;
	}
}

__override void led_set_color_power(enum ec_led_colors color)
{
	switch (color) {
	case EC_LED_COLOR_WHITE:
		ioex_set_level(IOEX_PWR_LED_WHITE_L, LED_ON_LVL);
		break;
	default:
		ioex_set_level(IOEX_PWR_LED_WHITE_L, LED_OFF_LVL);
		break;
	}
}

void led_get_brightness_range(enum ec_led_id led_id, uint8_t *brightness_range)
{
	if (led_id == EC_LED_ID_BATTERY_LED) {
		brightness_range[EC_LED_COLOR_GREEN] = 1;
		brightness_range[EC_LED_COLOR_AMBER] = 1;
		brightness_range[EC_LED_COLOR_RED] = 1;
	} else if (led_id == EC_LED_ID_POWER_LED) {
		brightness_range[EC_LED_COLOR_WHITE] = 1;
	}
}

int led_set_brightness(enum ec_led_id led_id, const uint8_t *brightness)
{
	if (led_id == EC_LED_ID_BATTERY_LED) {
		if (brightness[EC_LED_COLOR_GREEN] != 0)
			led_set_color_battery(EC_LED_COLOR_GREEN);
		else if (brightness[EC_LED_COLOR_AMBER] != 0)
			led_set_color_battery(EC_LED_COLOR_AMBER);
		else if (brightness[EC_LED_COLOR_RED] != 0)
			led_set_color_battery(EC_LED_COLOR_RED);
		else
			led_set_color_battery(LED_OFF);
	} else if (led_id == EC_LED_ID_POWER_LED) {
		if (brightness[EC_LED_COLOR_WHITE] != 0)
			led_set_color_power(EC_LED_COLOR_WHITE);
		else
			led_set_color_power(LED_OFF);
	} else {
		return EC_ERROR_INVAL;
	}
	return EC_SUCCESS;
}
