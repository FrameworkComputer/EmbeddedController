/* Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Battery LED control for Kukui board.
 */
#include "charge_state.h"
#include "driver/charger/rt946x.h"
#include "hooks.h"
#include "led_common.h"
#include "led_onoff_states.h"
#include "ec_commands.h"

#define LED_RED		MT6370_LED_ID1
#define LED_GREEN	MT6370_LED_ID2
#define LED_WHITE	MT6370_LED_ID3

#define LED_MASK_OFF	0
#define LED_MASK_RED	MT6370_MASK_RGB_ISNK1DIM_EN
#define LED_MASK_GREEN	MT6370_MASK_RGB_ISNK2DIM_EN
#define LED_MASK_WHITE	MT6370_MASK_RGB_ISNK3DIM_EN

const int led_charge_lvl_1 = 5;
const int led_charge_lvl_2 = 97;

struct led_descriptor led_bat_state_table[LED_NUM_STATES][LED_NUM_PHASES] = {
	[STATE_CHARGING_LVL_1]	     = {{EC_LED_COLOR_RED, LED_INDEFINITE} },
	[STATE_CHARGING_LVL_2]	     = {{EC_LED_COLOR_AMBER, LED_INDEFINITE} },
	[STATE_CHARGING_FULL_CHARGE] = {{EC_LED_COLOR_GREEN,  LED_INDEFINITE} },
	[STATE_DISCHARGE_S0]	     = {{LED_OFF,  LED_INDEFINITE} },
	[STATE_DISCHARGE_S3]	     = {{LED_OFF,  LED_INDEFINITE} },
	[STATE_DISCHARGE_S5]         = {{LED_OFF,  LED_INDEFINITE} },
	[STATE_BATTERY_ERROR]        = {{EC_LED_COLOR_RED,  1 * LED_ONE_SEC},
					{LED_OFF,	    1 * LED_ONE_SEC} },
	[STATE_FACTORY_TEST]         = {{EC_LED_COLOR_RED,   2 * LED_ONE_SEC},
					{EC_LED_COLOR_GREEN, 2 * LED_ONE_SEC} },
};

const struct led_descriptor
		led_pwr_state_table[PWR_LED_NUM_STATES][LED_NUM_PHASES] = {
	[PWR_LED_STATE_ON]           = {{EC_LED_COLOR_WHITE, LED_INDEFINITE} },
	[PWR_LED_STATE_SUSPEND_AC]   = {{EC_LED_COLOR_WHITE, 3 * LED_ONE_SEC},
					{LED_OFF,	   LED_ONE_SEC / 2} },
	[PWR_LED_STATE_SUSPEND_NO_AC] = {{LED_OFF, LED_INDEFINITE} },
	[PWR_LED_STATE_OFF]           = {{LED_OFF, LED_INDEFINITE} },
};

const enum ec_led_id supported_led_ids[] = {
	EC_LED_ID_POWER_LED,
	EC_LED_ID_BATTERY_LED
};

const int supported_led_ids_count = ARRAY_SIZE(supported_led_ids);

static int led_mask = LED_MASK_OFF;

static void led_set_color(int mask)
{
	static int new_mask = LED_MASK_OFF;

	if (new_mask == mask)
		return;
	else
		new_mask = mask;

	mt6370_led_set_color(led_mask);
}

void led_set_color_power(enum ec_led_colors color)
{
	if (color == EC_LED_COLOR_WHITE)
		led_mask |= LED_MASK_WHITE;
	else
		led_mask &= ~LED_MASK_WHITE;
	led_set_color(led_mask);
}

void led_set_color_battery(enum ec_led_colors color)
{
	switch (color) {
	case EC_LED_COLOR_RED:
		led_mask |= LED_MASK_RED;
		led_mask &= ~LED_MASK_GREEN;
		break;
	case EC_LED_COLOR_AMBER:
		led_mask |= LED_MASK_RED;
		led_mask |= LED_MASK_GREEN;
		break;
	case EC_LED_COLOR_GREEN:
		led_mask &= ~LED_MASK_RED;
		led_mask |= LED_MASK_GREEN;
		break;
	default: /* LED_OFF and other unsupported colors */
		led_mask &= ~LED_MASK_RED;
		led_mask &= ~LED_MASK_GREEN;
		break;
	}
	led_set_color(led_mask);
}

void led_get_brightness_range(enum ec_led_id led_id, uint8_t *brightness_range)
{
	if (led_id == EC_LED_ID_BATTERY_LED) {
		brightness_range[EC_LED_COLOR_RED] = 1;
		brightness_range[EC_LED_COLOR_AMBER] = 1;
		brightness_range[EC_LED_COLOR_GREEN] = 1;
	} else if (led_id == EC_LED_ID_POWER_LED) {
		brightness_range[EC_LED_COLOR_WHITE] = 1;
	}
}

int led_set_brightness(enum ec_led_id led_id, const uint8_t *brightness)
{
	if (led_id == EC_LED_ID_BATTERY_LED) {
		if (brightness[EC_LED_COLOR_RED] != 0)
			led_set_color_battery(EC_LED_COLOR_RED);
		else if (brightness[EC_LED_COLOR_AMBER] != 0)
			led_set_color_battery(EC_LED_COLOR_AMBER);
		else if (brightness[EC_LED_COLOR_GREEN] != 0)
			led_set_color_battery(EC_LED_COLOR_GREEN);
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

static void kodama_led_init(void)
{
	const enum mt6370_led_dim_mode dim = MT6370_LED_DIM_MODE_PWM;
	const enum mt6370_led_pwm_freq freq = MT6370_LED_PWM_FREQ1000;

	mt6370_led_set_color(LED_MASK_RED | LED_MASK_GREEN | LED_MASK_WHITE);
	mt6370_led_set_dim_mode(LED_RED, dim);
	mt6370_led_set_dim_mode(LED_GREEN, dim);
	mt6370_led_set_dim_mode(LED_WHITE, dim);
	mt6370_led_set_pwm_frequency(LED_RED, freq);
	mt6370_led_set_pwm_frequency(LED_GREEN, freq);
	mt6370_led_set_pwm_frequency(LED_WHITE, freq);
	mt6370_led_set_pwm_dim_duty(LED_RED, 12);
	mt6370_led_set_pwm_dim_duty(LED_GREEN, 31);
	mt6370_led_set_pwm_dim_duty(LED_WHITE, 12);
	mt6370_led_set_brightness(LED_MASK_RED, 7);
	mt6370_led_set_brightness(LED_MASK_GREEN, 7);
	mt6370_led_set_brightness(LED_MASK_WHITE, 7);
}
DECLARE_HOOK(HOOK_INIT, kodama_led_init, HOOK_PRIO_DEFAULT);
