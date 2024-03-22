/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Brya specific PWM LED settings: there are 2 LEDs on each side of the board,
 * each one can be controlled separately. The LED colors are blue or amber,
 * and the default behavior is tied to the charging process: both sides are
 * amber while charging the battery and blue when the battery is charged.
 */

#include "ec_commands.h"
#include "gpio.h"
#include "led_common.h"
#include "led_onoff_states.h"

#define LED_OFF_LVL 1
#define LED_ON_LVL 0

__override const int led_charge_lvl_1 = 10;

__override const int led_charge_lvl_2 = 100;

/* Omnigul: Note there is only LED for charge / power */
__override struct led_descriptor
	led_bat_state_table[LED_NUM_STATES][LED_NUM_PHASES] = {
		[STATE_CHARGING_LVL_1] = { { EC_LED_COLOR_AMBER,
					     LED_INDEFINITE } },
		[STATE_CHARGING_LVL_2] = { { EC_LED_COLOR_AMBER,
					     LED_INDEFINITE } },
		[STATE_CHARGING_FULL_CHARGE] = { { EC_LED_COLOR_BLUE,
						   LED_INDEFINITE } },
		[STATE_CHARGING_FULL_S5] = { { EC_LED_COLOR_BLUE,
					       LED_INDEFINITE } },
		[STATE_DISCHARGE_S0] = { { EC_LED_COLOR_BLUE,
					   LED_INDEFINITE } },
		[STATE_DISCHARGE_S3] = { { EC_LED_COLOR_AMBER,
					   1 * LED_ONE_SEC },
					 { LED_OFF, 3 * LED_ONE_SEC } },
		[STATE_DISCHARGE_S5] = { { LED_OFF, LED_INDEFINITE } },
		[STATE_BATTERY_ERROR] = { { EC_LED_COLOR_AMBER,
					    1 * LED_ONE_SEC },
					  { LED_OFF, 1 * LED_ONE_SEC } },
		[STATE_FACTORY_TEST] = { { EC_LED_COLOR_AMBER,
					   2 * LED_ONE_SEC },
					 { EC_LED_COLOR_BLUE,
					   2 * LED_ONE_SEC } },
	};
BUILD_ASSERT(ARRAY_SIZE(led_bat_state_table) == LED_NUM_STATES);

const enum ec_led_id supported_led_ids[] = {
	EC_LED_ID_BATTERY_LED,
};

const int supported_led_ids_count = ARRAY_SIZE(supported_led_ids);

__override void led_set_color_battery(enum ec_led_colors color)
{
	switch (color) {
	case EC_LED_COLOR_BLUE:
		gpio_set_level(GPIO_LED_1_PWR_BLUE_L, LED_ON_LVL);
		gpio_set_level(GPIO_LED_2_CHG_AMBER_L, LED_OFF_LVL);
		break;
	case EC_LED_COLOR_AMBER:
		gpio_set_level(GPIO_LED_1_PWR_BLUE_L, LED_OFF_LVL);
		gpio_set_level(GPIO_LED_2_CHG_AMBER_L, LED_ON_LVL);
		break;
	default: /* LED_OFF and other unsupported colors */
		gpio_set_level(GPIO_LED_1_PWR_BLUE_L, LED_OFF_LVL);
		gpio_set_level(GPIO_LED_2_CHG_AMBER_L, LED_OFF_LVL);
		break;
	}
}

void led_get_brightness_range(enum ec_led_id led_id, uint8_t *brightness_range)
{
	brightness_range[EC_LED_COLOR_BLUE] = 1;
	brightness_range[EC_LED_COLOR_AMBER] = 1;
}

int led_set_brightness(enum ec_led_id led_id, const uint8_t *brightness)
{
	if (brightness[EC_LED_COLOR_BLUE] != 0)
		led_set_color_battery(EC_LED_COLOR_BLUE);
	else if (brightness[EC_LED_COLOR_AMBER] != 0)
		led_set_color_battery(EC_LED_COLOR_AMBER);
	else
		led_set_color_battery(LED_OFF);

	return EC_SUCCESS;
}
