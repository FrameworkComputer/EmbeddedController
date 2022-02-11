/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Guybrush specific PWM LED settings.
 */

#include "common.h"
#include "led_onoff_states.h"
#include "led_common.h"
#include "hooks.h"
#include "pwm.h"

/* Note PWM LEDs are active low */
#define LED_OFF_LVL	1
#define LED_ON_LVL	0

/* Define our PWM_CH values for zephyr */
#define PWM_CH_LED_CHRG PWM_CHANNEL(DT_NODELABEL(led_charge))
#define PWM_CH_LED_FULL PWM_CHANNEL(DT_NODELABEL(led_full))

#define CPRINTS(format, args...) cprints(CC_PWM, format, ## args)

__override const int led_charge_lvl_1 = 5;

__override const int led_charge_lvl_2 = 97;

__override struct led_descriptor
			led_bat_state_table[LED_NUM_STATES][LED_NUM_PHASES] = {
	[STATE_CHARGING_LVL_1]	     = {{EC_LED_COLOR_AMBER, LED_INDEFINITE} },
	[STATE_CHARGING_LVL_2]	     = {{EC_LED_COLOR_AMBER, LED_INDEFINITE} },
	[STATE_CHARGING_FULL_CHARGE] = {{EC_LED_COLOR_WHITE, LED_INDEFINITE} },
	[STATE_DISCHARGE_S0]	     = {{EC_LED_COLOR_WHITE, LED_INDEFINITE} },
	[STATE_DISCHARGE_S3]	     = {{EC_LED_COLOR_WHITE, 1 * LED_ONE_SEC},
					{LED_OFF,	     1 * LED_ONE_SEC} },
	[STATE_DISCHARGE_S5]         = {{LED_OFF,            LED_INDEFINITE} },
	[STATE_BATTERY_ERROR]        = {{EC_LED_COLOR_AMBER, 1 * LED_ONE_SEC},
					{LED_OFF,	     1 * LED_ONE_SEC} },
	[STATE_FACTORY_TEST]         = {{EC_LED_COLOR_AMBER, 2 * LED_ONE_SEC},
					{EC_LED_COLOR_WHITE, 2 * LED_ONE_SEC} },
};

const enum ec_led_id supported_led_ids[] = {
	EC_LED_ID_BATTERY_LED,
};

const int supported_led_ids_count = ARRAY_SIZE(supported_led_ids);

__override void led_set_color_battery(enum ec_led_colors color)
{
	switch (color) {
	case EC_LED_COLOR_AMBER:
		pwm_enable(PWM_CH_LED_CHRG, LED_ON_LVL);
		pwm_enable(PWM_CH_LED_FULL, LED_OFF_LVL);
		break;
	case EC_LED_COLOR_WHITE:
		pwm_enable(PWM_CH_LED_CHRG, LED_OFF_LVL);
		pwm_enable(PWM_CH_LED_FULL, LED_ON_LVL);
		break;
	case LED_OFF:
		pwm_enable(PWM_CH_LED_CHRG, LED_OFF_LVL);
		pwm_enable(PWM_CH_LED_FULL, LED_OFF_LVL);
		break;
	default: /* Unsupported colors */
		CPRINTS("Unsupported LED color: %d", color);
		pwm_enable(PWM_CH_LED_CHRG, LED_OFF_LVL);
		pwm_enable(PWM_CH_LED_FULL, LED_OFF_LVL);
		break;
	}
}

void led_get_brightness_range(enum ec_led_id led_id, uint8_t *brightness_range)
{
	if (led_id == EC_LED_ID_BATTERY_LED) {
		brightness_range[EC_LED_COLOR_AMBER] = 1;
		brightness_range[EC_LED_COLOR_WHITE] = 1;
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
	} else {
		CPRINTS("Unsupported LED set: %d", led_id);
		return EC_ERROR_INVAL;
	}

	return EC_SUCCESS;
}

static void pwm_led_duty_init(void)
{
	pwm_set_duty(PWM_CH_LED_CHRG, 100);
	pwm_set_duty(PWM_CH_LED_FULL, 100);
}
DECLARE_HOOK(HOOK_INIT, pwm_led_duty_init, HOOK_PRIO_INIT_PWM + 1);
