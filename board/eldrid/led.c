/* Copyright 2020 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "charge_manager.h"
#include "ec_commands.h"
#include "gpio.h"
#include "led_common.h"
#include "led_onoff_states.h"
#include "pwm.h"

#define BAT_LED_ON_LVL 0
#define BAT_LED_OFF_LVL 1

#define PWR_LED_ON_LVL 1
#define PWR_LED_OFF_LVL 0

/* LED_SIDESEL_4_L=1, MB BAT LED open
 * LED_SIDESEL_4_L=0, DB BAT LED open
 */
#define LED_SIDESEL_MB_PORT 0
#define LED_SIDESEL_DB_PORT 1

__override const int led_charge_lvl_1 = 5;

__override const int led_charge_lvl_2 = 95;

__override struct led_descriptor
	led_bat_state_table[LED_NUM_STATES][LED_NUM_PHASES] = {
		[STATE_CHARGING_LVL_1] = { { EC_LED_COLOR_AMBER,
					     LED_INDEFINITE } },
		[STATE_CHARGING_LVL_2] = { { EC_LED_COLOR_AMBER,
					     LED_INDEFINITE } },
		[STATE_CHARGING_FULL_CHARGE] = { { EC_LED_COLOR_WHITE,
						   LED_INDEFINITE } },
		[STATE_DISCHARGE_S0] = { { LED_OFF, LED_INDEFINITE } },
		[STATE_DISCHARGE_S0_BAT_LOW] = { { EC_LED_COLOR_WHITE,
						   1 * LED_ONE_SEC },
						 { LED_OFF, 1 * LED_ONE_SEC } },
		[STATE_DISCHARGE_S3] = { { LED_OFF, LED_INDEFINITE } },
		[STATE_DISCHARGE_S5] = { { LED_OFF, LED_INDEFINITE } },
		[STATE_BATTERY_ERROR] = { { EC_LED_COLOR_WHITE,
					    0.4 * LED_ONE_SEC },
					  { LED_OFF, 0.4 * LED_ONE_SEC } },
		[STATE_FACTORY_TEST] = { { EC_LED_COLOR_WHITE,
					   1 * LED_ONE_SEC },
					 { LED_OFF, 1 * LED_ONE_SEC } },
	};

__override const struct led_descriptor
	led_pwr_state_table[PWR_LED_NUM_STATES][LED_NUM_PHASES] = {
		[PWR_LED_STATE_ON] = { { EC_LED_COLOR_WHITE, LED_INDEFINITE } },
		[PWR_LED_STATE_SUSPEND_AC] = { { EC_LED_COLOR_WHITE,
						 1 * LED_ONE_SEC },
					       { LED_OFF, 1 * LED_ONE_SEC } },
		[PWR_LED_STATE_SUSPEND_NO_AC] = { { EC_LED_COLOR_WHITE,
						    1 * LED_ONE_SEC },
						  { LED_OFF,
						    6 * LED_ONE_SEC } },
		[PWR_LED_STATE_OFF] = { { LED_OFF, LED_INDEFINITE } },
	};

const enum ec_led_id supported_led_ids[] = {
	EC_LED_ID_BATTERY_LED,
	EC_LED_ID_POWER_LED,
};

const int supported_led_ids_count = ARRAY_SIZE(supported_led_ids);

__override void led_set_color_battery(enum ec_led_colors color)
{
	int port;
	int side_select_duty;

	if (led_auto_control_is_enabled(EC_LED_ID_BATTERY_LED)) {
		port = charge_manager_get_active_charge_port();
		switch (port) {
		case LED_SIDESEL_MB_PORT:
			side_select_duty = 0;
			break;
		case LED_SIDESEL_DB_PORT:
			side_select_duty = 100;
			break;
		default:
			/*
			 * We need to turn off led here since curr.ac won't
			 * update immediately but led will update every 200ms.
			 */
			side_select_duty = 50;
			color = LED_OFF;
		}

		pwm_set_duty(PWM_CH_LED4_SIDESEL, side_select_duty);
	}

	switch (color) {
	case EC_LED_COLOR_AMBER:
		gpio_set_level(GPIO_LED_1_L, BAT_LED_ON_LVL);
		gpio_set_level(GPIO_LED_2_L, BAT_LED_OFF_LVL);
		break;
	case EC_LED_COLOR_WHITE:
		gpio_set_level(GPIO_LED_1_L, BAT_LED_OFF_LVL);
		gpio_set_level(GPIO_LED_2_L, BAT_LED_ON_LVL);
		break;
	default: /* LED_OFF and other unsupported colors */
		gpio_set_level(GPIO_LED_1_L, BAT_LED_OFF_LVL);
		gpio_set_level(GPIO_LED_2_L, BAT_LED_OFF_LVL);
		break;
	}
}

__override void led_set_color_power(enum ec_led_colors color)
{
	if (color == EC_LED_COLOR_WHITE)
		gpio_set_level(GPIO_POWER_LED_GATE, PWR_LED_ON_LVL);
	else
		/* LED_OFF and unsupported colors */
		gpio_set_level(GPIO_POWER_LED_GATE, PWR_LED_OFF_LVL);
}

void led_get_brightness_range(enum ec_led_id led_id, uint8_t *brightness_range)
{
	if (led_id == EC_LED_ID_BATTERY_LED) {
		brightness_range[EC_LED_COLOR_AMBER] = 1;
		brightness_range[EC_LED_COLOR_WHITE] = 1;
	} else if (led_id == EC_LED_ID_POWER_LED) {
		brightness_range[EC_LED_COLOR_WHITE] = 1;
	}
}

int led_set_brightness(enum ec_led_id led_id, const uint8_t *brightness)
{
	if (led_id == EC_LED_ID_BATTERY_LED) {
		led_auto_control(led_id, 0);
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
