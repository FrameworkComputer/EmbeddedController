/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Sundance specific PWM LED settings: there is one LED on the motherboard.
 * LED colors are white or amber. The default behavior is related to the
 * charging process. The amber light with AC adapter is on when the system is
 * in s0/s0ix/s5 state. When battery error, the LED will light up for 1 second
 * and then turn off for 2 seconds. When the system is in the s0 state and the
 * battery is fully charged, the whilt LED will lighten. In the case of battery
 * only, when the system is in the s0 state and battery is fully charged, the
 * white LED will light up. In other case, the LED status is off.
 */

#include "common.h"
#include "ec_commands.h"
#include "led_common.h"
#include "led_onoff_states.h"

#define LED_OFF_LVL 1
#define LED_ON_LVL 0

__override const int led_charge_lvl_1 = 0;
__override const int led_charge_lvl_2 = 100;

__override struct led_descriptor
	led_bat_state_table[LED_NUM_STATES][LED_NUM_PHASES] = {
		[STATE_CHARGING_LVL_1] = { { EC_LED_COLOR_AMBER,
					     LED_INDEFINITE } },
		[STATE_CHARGING_LVL_2] = { { EC_LED_COLOR_AMBER,
					     LED_INDEFINITE } },
		[STATE_CHARGING_FULL_CHARGE] = { { EC_LED_COLOR_WHITE,
						   LED_INDEFINITE } },
		[STATE_DISCHARGE_S0] = { { EC_LED_COLOR_WHITE,
					   LED_INDEFINITE } },
		[STATE_DISCHARGE_S3] = { { LED_OFF, LED_INDEFINITE } },
		[STATE_DISCHARGE_S5] = { { LED_OFF, LED_INDEFINITE } },
		[STATE_BATTERY_ERROR] = { { EC_LED_COLOR_AMBER,
					    1 * LED_ONE_SEC },
					  { LED_OFF, 2 * LED_ONE_SEC } },
		[STATE_FACTORY_TEST] = { { EC_LED_COLOR_AMBER,
					   2 * LED_ONE_SEC },
					 { EC_LED_COLOR_WHITE,
					   2 * LED_ONE_SEC } },
	};

const enum ec_led_id supported_led_ids[] = { EC_LED_ID_BATTERY_LED };

const int supported_led_ids_count = ARRAY_SIZE(supported_led_ids);

__override void led_set_color_battery(enum ec_led_colors color)
{
	switch (color) {
	case EC_LED_COLOR_AMBER:
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_led_1_odl),
				LED_ON_LVL);
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_led_2_odl),
				LED_OFF_LVL);
		break;
	case EC_LED_COLOR_WHITE:
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_led_1_odl),
				LED_OFF_LVL);
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_led_2_odl),
				LED_ON_LVL);
		break;
	default: /* LED_OFF and other unsupported colors */
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_led_1_odl),
				LED_OFF_LVL);
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_led_2_odl),
				LED_OFF_LVL);
		break;
	}
}

void led_get_brightness_range(enum ec_led_id led_id, uint8_t *brightness_range)
{
	if (led_id == EC_LED_ID_BATTERY_LED) {
		brightness_range[EC_LED_COLOR_WHITE] = 1;
		brightness_range[EC_LED_COLOR_AMBER] = 1;
	}
}

int led_set_brightness(enum ec_led_id led_id, const uint8_t *brightness)
{
	if (led_id == EC_LED_ID_BATTERY_LED) {
		if (brightness[EC_LED_COLOR_WHITE] != 0)
			led_set_color_battery(EC_LED_COLOR_WHITE);
		else if (brightness[EC_LED_COLOR_AMBER] != 0)
			led_set_color_battery(EC_LED_COLOR_AMBER);
		else
			led_set_color_battery(LED_OFF);
	}
	return EC_SUCCESS;
}
