/* Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "ec_commands.h"
#include "gpio.h"
#include "led_common.h"
#include "led_onoff_states.h"

#define LED_OFF_LVL	1
#define LED_ON_LVL	0

const int led_charge_lvl_1;
const int led_charge_lvl_2 = 100;

struct led_descriptor led_bat_state_table[LED_NUM_STATES][LED_NUM_PHASES] = {
	[STATE_CHARGING_LVL_1]       = {{EC_LED_COLOR_GREEN,  2 * LED_ONE_SEC},
					{EC_LED_COLOR_RED, 2 * LED_ONE_SEC} },
	[STATE_CHARGING_LVL_2]	     = {{EC_LED_COLOR_RED, LED_INDEFINITE} },
	[STATE_CHARGING_FULL_CHARGE] = {{EC_LED_COLOR_GREEN,  LED_INDEFINITE} },
	[STATE_DISCHARGE_S0]	     = {{EC_LED_COLOR_GREEN,  LED_INDEFINITE} },
	[STATE_DISCHARGE_S3]	     = {{EC_LED_COLOR_RED, 1 * LED_ONE_SEC},
					{LED_OFF,            3 * LED_ONE_SEC} },
	[STATE_DISCHARGE_S5]         = {{LED_OFF,            LED_INDEFINITE} },
	[STATE_BATTERY_ERROR]        = {{EC_LED_COLOR_GREEN,  2 * LED_ONE_SEC},
					{EC_LED_COLOR_RED, 2 * LED_ONE_SEC} },
	[STATE_FACTORY_TEST]         = {{EC_LED_COLOR_GREEN,  LED_INDEFINITE} },
};

BUILD_ASSERT(ARRAY_SIZE(led_bat_state_table) == LED_NUM_STATES);

const struct led_descriptor
			led_pwr_state_table[PWR_LED_NUM_STATES][LED_NUM_PHASES] = {
	[PWR_LED_STATE_ON]           = {{EC_LED_COLOR_WHITE, LED_INDEFINITE} },
	[PWR_LED_STATE_SUSPEND_AC]   = {{EC_LED_COLOR_WHITE, 3 * LED_ONE_SEC},
				{LED_OFF,          0.5 * LED_ONE_SEC} },
	[PWR_LED_STATE_SUSPEND_NO_AC] = {{LED_OFF, LED_INDEFINITE} },
	[PWR_LED_STATE_OFF]           = {{LED_OFF, LED_INDEFINITE} },
};

BUILD_ASSERT(ARRAY_SIZE(led_pwr_state_table) == PWR_LED_NUM_STATES);

const enum ec_led_id supported_led_ids[] = {
	EC_LED_ID_BATTERY_LED,
	EC_LED_ID_POWER_LED
};

const int supported_led_ids_count = ARRAY_SIZE(supported_led_ids);

void led_set_color_power(enum ec_led_colors color)
{
	if (color == EC_LED_COLOR_WHITE)
		gpio_set_level(GPIO_LED3_PWM, LED_ON_LVL);
	else
		/* LED_OFF and unsupported colors */
		gpio_set_level(GPIO_LED3_PWM, LED_OFF_LVL);
}

void led_set_color_battery(enum ec_led_colors color)
{
	switch (color) {
	case EC_LED_COLOR_GREEN:
		gpio_set_level(GPIO_LED_FULL_L, LED_ON_LVL);
		gpio_set_level(GPIO_LED_CHRG_L, LED_OFF_LVL);
		break;
	case EC_LED_COLOR_RED:
		gpio_set_level(GPIO_LED_FULL_L, LED_OFF_LVL);
		gpio_set_level(GPIO_LED_CHRG_L, LED_ON_LVL);
		break;
	default: /* LED_OFF and other unsupported colors */
		gpio_set_level(GPIO_LED_FULL_L, LED_OFF_LVL);
		gpio_set_level(GPIO_LED_CHRG_L, LED_OFF_LVL);
		break;
	}
}

void led_get_brightness_range(enum ec_led_id led_id, uint8_t *brightness_range)
{
	if (led_id == EC_LED_ID_BATTERY_LED) {
		brightness_range[EC_LED_COLOR_GREEN] = 1;
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
		else if (brightness[EC_LED_COLOR_RED] != 0)
			led_set_color_battery(EC_LED_COLOR_RED);
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
