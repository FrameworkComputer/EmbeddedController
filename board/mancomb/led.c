/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Power and battery LED control for Mancomb
 */

#include "ec_commands.h"
#include "gpio.h"
#include "led_common.h"
#include "led_onoff_states.h"
#include "pwm.h"

#define LED_OFF_LVL	1
#define LED_ON_LVL	0

#define CPRINTS(format, args...) cprints(CC_PWM, format, ## args)

__override const struct led_descriptor
		led_pwr_state_table[PWR_LED_NUM_STATES][LED_NUM_PHASES] = {
	[PWR_LED_STATE_ON]           = {{EC_LED_COLOR_GREEN, LED_INDEFINITE} },
	[PWR_LED_STATE_SUSPEND_AC]   = {{EC_LED_COLOR_YELLOW, 1 * LED_ONE_SEC},
					{LED_OFF,	   1 * LED_ONE_SEC} },
	[PWR_LED_STATE_OFF]           = {{LED_OFF, LED_INDEFINITE} },
	[PWR_LED_STATE_OFF_LOW_POWER] = {{EC_LED_COLOR_RED, 1 * LED_ONE_SEC},
					 {LED_OFF,	    1 * LED_ONE_SEC} },
};

const enum ec_led_id supported_led_ids[] = {
	EC_LED_ID_POWER_LED,
};

const int supported_led_ids_count = ARRAY_SIZE(supported_led_ids);

__override void led_set_color_power(enum ec_led_colors color)
{
	switch (color) {
	case EC_LED_COLOR_GREEN:
		pwm_enable(PWM_CH_LED1, LED_OFF_LVL);
		pwm_enable(PWM_CH_LED2, LED_ON_LVL);
		break;
	case EC_LED_COLOR_RED:
		pwm_enable(PWM_CH_LED1, LED_ON_LVL);
		pwm_enable(PWM_CH_LED2, LED_OFF_LVL);
		break;
	case EC_LED_COLOR_YELLOW:
		pwm_enable(PWM_CH_LED1, LED_ON_LVL);
		pwm_enable(PWM_CH_LED2, LED_ON_LVL);
		break;
	case LED_OFF:
		pwm_enable(PWM_CH_LED1, LED_OFF_LVL);
		pwm_enable(PWM_CH_LED2, LED_OFF_LVL);
		break;
	default: /* Unsupported colors */
		CPRINTS("Unsupported LED color: %d", color);
		pwm_enable(PWM_CH_LED1, LED_OFF_LVL);
		pwm_enable(PWM_CH_LED2, LED_OFF_LVL);
		break;
	}
}

void led_get_brightness_range(enum ec_led_id led_id, uint8_t *brightness_range)
{
	if (led_id == EC_LED_ID_POWER_LED) {
		brightness_range[EC_LED_COLOR_RED] = 1;
		brightness_range[EC_LED_COLOR_GREEN] = 1;
		brightness_range[EC_LED_COLOR_YELLOW] = 1;
	}
}

int led_set_brightness(enum ec_led_id led_id, const uint8_t *brightness)
{
	if (led_id == EC_LED_ID_POWER_LED) {
		if (brightness[EC_LED_COLOR_RED] != 0)
			led_set_color_power(EC_LED_COLOR_RED);
		else if (brightness[EC_LED_COLOR_GREEN] != 0)
			led_set_color_power(EC_LED_COLOR_GREEN);
		else if (brightness[EC_LED_COLOR_YELLOW] != 0)
			led_set_color_power(EC_LED_COLOR_YELLOW);
		else
			led_set_color_power(LED_OFF);
	} else {
		CPRINTS("Unsuppored LED set: %d", led_id);
		return EC_ERROR_INVAL;
	}

	return EC_SUCCESS;
}

