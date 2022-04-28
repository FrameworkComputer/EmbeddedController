/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <drivers/pwm.h>
#include <logging/log.h>

#include "board_led.h"
#include "common.h"
#include "led_common.h"
#include "led_onoff_states.h"
#include "util.h"

LOG_MODULE_REGISTER(board_led, LOG_LEVEL_ERR);

/*If we need pwm output in ITE chip power saving mode, then we should set
 * frequency <= 324Hz.
 */
#define BOARD_LED_PWM_PERIOD_NS BOARD_LED_HZ_TO_PERIOD_NS(324)

static const struct board_led_pwm_dt_channel board_led_power_white =
	BOARD_LED_PWM_DT_CHANNEL_INITIALIZER(DT_NODELABEL(led_power_white));
static const struct board_led_pwm_dt_channel board_led_battery_amber =
	BOARD_LED_PWM_DT_CHANNEL_INITIALIZER(DT_NODELABEL(led_battery_amber));
static const struct board_led_pwm_dt_channel board_led_battery_white =
	BOARD_LED_PWM_DT_CHANNEL_INITIALIZER(DT_NODELABEL(led_battery_white));

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

static void board_led_pwm_set_duty(const struct board_led_pwm_dt_channel *ch,
				   int percent)
{
	uint32_t pulse_ns;
	int rv;

	if (!device_is_ready(ch->dev)) {
		LOG_ERR("PWM device %s not ready", ch->dev->name);
		return;
	}

	pulse_ns = DIV_ROUND_NEAREST(BOARD_LED_PWM_PERIOD_NS * percent, 100);

	LOG_DBG("Board LED PWM %s set percent (%d), pulse %d",
		ch->dev->name, percent, pulse_ns);

	rv = pwm_set(ch->dev, ch->channel, BOARD_LED_PWM_PERIOD_NS, pulse_ns,
		     ch->flags);
	if (rv) {
		LOG_ERR("pwm_set() failed %s (%d)", ch->dev->name, rv);
	}
}

__override void led_set_color_battery(enum ec_led_colors color)
{
	switch (color) {
	case EC_LED_COLOR_AMBER:
		board_led_pwm_set_duty(&board_led_battery_amber, 100);
		board_led_pwm_set_duty(&board_led_battery_white, 0);
		break;
	case EC_LED_COLOR_WHITE:
		board_led_pwm_set_duty(&board_led_battery_amber, 0);
		board_led_pwm_set_duty(&board_led_battery_white, 100);
		break;
	default:
		board_led_pwm_set_duty(&board_led_battery_amber, 0);
		board_led_pwm_set_duty(&board_led_battery_white, 0);
		break;
	}
}

__override void led_set_color_power(enum ec_led_colors color)
{
	switch (color) {
	case EC_LED_COLOR_WHITE:
		board_led_pwm_set_duty(&board_led_power_white, 100);
		break;
	default:
		board_led_pwm_set_duty(&board_led_power_white, 0);
		break;
	}
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
		if (brightness[EC_LED_COLOR_AMBER] != 0) {
			led_set_color_battery(EC_LED_COLOR_AMBER);
		} else if (brightness[EC_LED_COLOR_WHITE] != 0) {
			led_set_color_battery(EC_LED_COLOR_WHITE);
		} else {
			led_set_color_battery(LED_OFF);
		}
	} else if (led_id == EC_LED_ID_POWER_LED) {
		if (brightness[EC_LED_COLOR_WHITE] != 0) {
			led_set_color_power(EC_LED_COLOR_WHITE);
		} else {
			led_set_color_power(LED_OFF);
		}
	}

	return EC_SUCCESS;
}
