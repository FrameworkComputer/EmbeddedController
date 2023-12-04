/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Battery LED control for Steelix
 */

#include "board_led.h"
#include "common.h"
#include "cros_cbi.h"
#include "led_common.h"
#include "led_onoff_states.h"
#include "util.h"

#include <zephyr/drivers/pwm.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(board_led, LOG_LEVEL_ERR);

#define BOARD_LED_PWM_PERIOD_NS BOARD_LED_HZ_TO_PERIOD_NS(100)

static const struct board_led_pwm_dt_channel board_led_battery_red =
	BOARD_LED_PWM_DT_CHANNEL_INITIALIZER(DT_NODELABEL(led_battery_red));
static const struct board_led_pwm_dt_channel board_led_battery_green =
	BOARD_LED_PWM_DT_CHANNEL_INITIALIZER(DT_NODELABEL(led_battery_green));
static const struct board_led_pwm_dt_channel board_led_power_white =
	BOARD_LED_PWM_DT_CHANNEL_INITIALIZER(DT_NODELABEL(led_power_white));

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
		[STATE_DISCHARGE_S0_BAT_LOW] = { { LED_OFF, LED_INDEFINITE } },
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
						 3 * LED_ONE_SEC },
					       { LED_OFF, 0.5 * LED_ONE_SEC } },
		[PWR_LED_STATE_SUSPEND_NO_AC] = { { EC_LED_COLOR_WHITE,
						    3 * LED_ONE_SEC },
						  { LED_OFF,
						    0.5 * LED_ONE_SEC } },
		[PWR_LED_STATE_OFF] = { { LED_OFF, LED_INDEFINITE } },
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
		LOG_ERR("device %s not ready", ch->dev->name);
		return;
	}

	pulse_ns = DIV_ROUND_NEAREST(BOARD_LED_PWM_PERIOD_NS * percent, 100);

	LOG_DBG("Board LED PWM %s set percent (%d), pulse %d", ch->dev->name,
		percent, pulse_ns);

	rv = pwm_set(ch->dev, ch->channel, BOARD_LED_PWM_PERIOD_NS, pulse_ns,
		     ch->flags);
	if (rv) {
		LOG_ERR("pwm_set() failed %s (%d)", ch->dev->name, rv);
	}
}

static bool device_is_clamshell(void)
{
	int ret;
	uint32_t val;

	ret = cros_cbi_get_fw_config(FORM_FACTOR, &val);
	if (ret != 0) {
		LOG_ERR("Error retrieving CBI FW_CONFIG field %d", FORM_FACTOR);
		return false;
	}

	return val == CLAMSHELL;
}

__override void led_set_color_battery(enum ec_led_colors color)
{
	switch (color) {
	case EC_LED_COLOR_RED:
		board_led_pwm_set_duty(&board_led_battery_red, 100);
		board_led_pwm_set_duty(&board_led_battery_green, 0);
		break;
	case EC_LED_COLOR_GREEN:
		board_led_pwm_set_duty(&board_led_battery_red, 0);
		board_led_pwm_set_duty(&board_led_battery_green, 100);
		break;
	case EC_LED_COLOR_AMBER:
		board_led_pwm_set_duty(&board_led_battery_red, 100);
		board_led_pwm_set_duty(&board_led_battery_green, 20);
		break;
	default: /* LED_OFF and other unsupported colors */
		board_led_pwm_set_duty(&board_led_battery_red, 0);
		board_led_pwm_set_duty(&board_led_battery_green, 0);
		break;
	}
}

__override void led_set_color_power(enum ec_led_colors color)
{
	if (device_is_clamshell()) {
		board_led_pwm_set_duty(&board_led_power_white, 0);
	} else {
		switch (color) {
		case EC_LED_COLOR_WHITE:
			board_led_pwm_set_duty(&board_led_power_white, 100);
			break;
		default:
			board_led_pwm_set_duty(&board_led_power_white, 0);
			break;
		}
	}
}

void led_get_brightness_range(enum ec_led_id led_id, uint8_t *brightness_range)
{
	if (led_id == EC_LED_ID_BATTERY_LED) {
		brightness_range[EC_LED_COLOR_RED] = 1;
		brightness_range[EC_LED_COLOR_GREEN] = 1;
		brightness_range[EC_LED_COLOR_AMBER] = 1;
	} else if (led_id == EC_LED_ID_POWER_LED) {
		if (device_is_clamshell()) {
			brightness_range[EC_LED_COLOR_WHITE] = 0;
		} else {
			brightness_range[EC_LED_COLOR_WHITE] = 1;
		}
	}
}

int led_set_brightness(enum ec_led_id led_id, const uint8_t *brightness)
{
	if (led_id == EC_LED_ID_BATTERY_LED) {
		if (brightness[EC_LED_COLOR_RED] != 0) {
			led_set_color_battery(EC_LED_COLOR_RED);
		} else if (brightness[EC_LED_COLOR_GREEN] != 0) {
			led_set_color_battery(EC_LED_COLOR_GREEN);
		} else if (brightness[EC_LED_COLOR_AMBER] != 0) {
			led_set_color_battery(EC_LED_COLOR_AMBER);
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

/* TODO(yllin): Port LED config to dts and drop this function */
__override void led_control(enum ec_led_id led_id, enum ec_led_state state)
{
	if ((led_id != EC_LED_ID_RECOVERY_HW_REINIT_LED) &&
	    (led_id != EC_LED_ID_SYSRQ_DEBUG_LED))
		return;

	if (state == LED_STATE_RESET) {
		led_auto_control(EC_LED_ID_BATTERY_LED, 1);
		return;
	}

	led_auto_control(EC_LED_ID_BATTERY_LED, 0);

	led_set_color_battery(state ? EC_LED_COLOR_RED : EC_LED_COLOR_INVALID);
}
