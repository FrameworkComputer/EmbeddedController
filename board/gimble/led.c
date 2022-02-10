/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <stdint.h>

#include "charge_manager.h"
#include "common.h"
#include "compile_time_macros.h"
#include "ec_commands.h"
#include "gpio.h"
#include "led_common.h"
#include "led_onoff_states.h"
#include "pwm.h"
#include "util.h"

#define BAT_LED_ON_LVL 100
#define BAT_LED_OFF_LVL 0

#define PWR_LED_ON_LVL 1
#define PWR_LED_OFF_LVL 0

#define LED_SIDESEL_MB_PORT 0
#define LED_SIDESEL_DB_PORT 1

__override const int led_charge_lvl_1 = 5;
__override const int led_charge_lvl_2 = 95;

__override struct led_descriptor
			led_bat_state_table[LED_NUM_STATES][LED_NUM_PHASES] = {
	[STATE_CHARGING_LVL_1]	     = {{EC_LED_COLOR_AMBER, LED_INDEFINITE} },
	[STATE_CHARGING_LVL_2]	     = {{EC_LED_COLOR_AMBER, LED_INDEFINITE} },
	[STATE_CHARGING_FULL_CHARGE] = {{EC_LED_COLOR_WHITE, LED_INDEFINITE} },
	[STATE_DISCHARGE_S0]	     = {{LED_OFF,  LED_INDEFINITE} },
	[STATE_DISCHARGE_S0_BAT_LOW] = {{EC_LED_COLOR_AMBER, 1 * LED_ONE_SEC},
					{LED_OFF,	     1 * LED_ONE_SEC} },
	[STATE_DISCHARGE_S3]	     = {{LED_OFF,  LED_INDEFINITE} },
	[STATE_DISCHARGE_S5]         = {{LED_OFF,  LED_INDEFINITE} },
	[STATE_BATTERY_ERROR]        = {
					{EC_LED_COLOR_AMBER, 0.5 * LED_ONE_SEC},
					{LED_OFF,            0.5 * LED_ONE_SEC}
	},
	[STATE_FACTORY_TEST]         = {
					{EC_LED_COLOR_WHITE, 1 * LED_ONE_SEC},
					{LED_OFF,            1 * LED_ONE_SEC}
	},
};

__override const struct led_descriptor
		led_pwr_state_table[PWR_LED_NUM_STATES][LED_NUM_PHASES] = {
	[PWR_LED_STATE_ON]           =  {{EC_LED_COLOR_WHITE, LED_INDEFINITE} },
	[PWR_LED_STATE_SUSPEND_AC]   =  {{LED_OFF,  LED_INDEFINITE} },
	[PWR_LED_STATE_SUSPEND_NO_AC] =  {{LED_OFF,  LED_INDEFINITE} },
	[PWR_LED_STATE_OFF]           = {
		{LED_OFF,             LED_INDEFINITE} },
};


const enum ec_led_id supported_led_ids[] = {
	EC_LED_ID_BATTERY_LED,
	EC_LED_ID_POWER_LED,
};

const int supported_led_ids_count = ARRAY_SIZE(supported_led_ids);

__override void led_set_color_battery(enum ec_led_colors color)
{
	int port;

	/* There are four battery leds, LED1/LED2 are on MB side and
	 * LED3/LED4 are on DB side. All leds are OFF by default.
	 */
	int led1_duty, led2_duty, led3_duty, led4_duty;

	led1_duty = led2_duty = led3_duty = led4_duty = BAT_LED_OFF_LVL;

	/* Check which port is the charging port,
	 * and turn on the corresponding led.
	 */
	port = charge_manager_get_active_charge_port();
	switch (port) {
	case LED_SIDESEL_MB_PORT:
		switch (color) {
		case EC_LED_COLOR_AMBER:
			led1_duty = BAT_LED_ON_LVL;
			break;
		case EC_LED_COLOR_WHITE:
			led2_duty = BAT_LED_ON_LVL;
			break;
		default: /* LED_OFF and other unsupported colors */
			break;
		}
		break;
	case LED_SIDESEL_DB_PORT:
		switch (color) {
		case EC_LED_COLOR_AMBER:
			led3_duty = BAT_LED_ON_LVL;
			break;
		case EC_LED_COLOR_WHITE:
			led4_duty = BAT_LED_ON_LVL;
			break;
		default: /* LED_OFF and other unsupported colors */
			break;
		}
		break;
	default: /* Unknown charging port */
		break;
	}

	pwm_set_duty(PWM_CH_LED1, led1_duty);
	pwm_set_duty(PWM_CH_LED2, led2_duty);
	pwm_set_duty(PWM_CH_LED3, led3_duty);
	pwm_set_duty(PWM_CH_LED4, led4_duty);
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
