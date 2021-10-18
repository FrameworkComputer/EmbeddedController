/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Power and battery LED control for madoo
 */

#include "charge_state.h"
#include "driver/tcpm/tcpci.h"
#include "ec_commands.h"
#include "gpio.h"
#include "led_common.h"
#include "led_onoff_states.h"
#include "hooks.h"
#include "system.h"

#define LED_OFF_LVL	1
#define LED_ON_LVL	0

__override const int led_charge_lvl_1;

__override const int led_charge_lvl_2 = 100;

/* madoo: Note there is only LED for charge / power */
__override struct led_descriptor
			led_bat_state_table[LED_NUM_STATES][LED_NUM_PHASES] = {
	[STATE_CHARGING_LVL_1]	     = {{EC_LED_COLOR_AMBER, LED_INDEFINITE} },
	[STATE_CHARGING_LVL_2]	     = {{EC_LED_COLOR_AMBER, LED_INDEFINITE} },
	[STATE_CHARGING_FULL_CHARGE] = {{EC_LED_COLOR_WHITE, LED_INDEFINITE} },
	[STATE_DISCHARGE_S0]	     = {{LED_OFF, LED_INDEFINITE} },
	[STATE_DISCHARGE_S0_BAT_LOW] = {{EC_LED_COLOR_AMBER, 1 * LED_ONE_SEC},
					{LED_OFF, 1 * LED_ONE_SEC} },
	/* STATE_DISCHARGE_S3 will changed if sku is clamshells */
	[STATE_DISCHARGE_S3]	     = {{LED_OFF, LED_INDEFINITE} },
	[STATE_DISCHARGE_S5]         = {{LED_OFF, LED_INDEFINITE} },
	[STATE_BATTERY_ERROR]        = {{EC_LED_COLOR_AMBER, 0.5 * LED_ONE_SEC},
					{LED_OFF, 0.5 * LED_ONE_SEC} },
};

__override const struct led_descriptor
		led_pwr_state_table[PWR_LED_NUM_STATES][LED_NUM_PHASES] = {
	[PWR_LED_STATE_ON]           = {{EC_LED_COLOR_WHITE, LED_INDEFINITE} },
	[PWR_LED_STATE_SUSPEND_AC]   = {{EC_LED_COLOR_WHITE, 1 * LED_ONE_SEC},
					{LED_OFF,	   1 * LED_ONE_SEC} },
	[PWR_LED_STATE_SUSPEND_NO_AC] = {{EC_LED_COLOR_WHITE, 1 * LED_ONE_SEC},
					{LED_OFF,	   1 * LED_ONE_SEC} },
	[PWR_LED_STATE_OFF]           = {{LED_OFF, LED_INDEFINITE} },
};

const enum ec_led_id supported_led_ids[] = {
	EC_LED_ID_BATTERY_LED,
	EC_LED_ID_POWER_LED
};

const int supported_led_ids_count = ARRAY_SIZE(supported_led_ids);

__override void led_set_color_power(enum ec_led_colors color)
{
	if (color == EC_LED_COLOR_WHITE)
		gpio_set_level(GPIO_PWR_LED_WHITE_L, LED_ON_LVL);
	else
		/* LED_OFF and unsupported colors */
		gpio_set_level(GPIO_PWR_LED_WHITE_L, LED_OFF_LVL);
}

/*
 * Turn off battery LED, if AC is present but battery is not charging.
 * It could be caused by battery's protection like OTP.
 */
int battery_safety_check(void)
{
	uint8_t data[6];
	int rv;

	/* ignore battery in error state because it has other behavior */
	if (charge_get_state() == PWR_STATE_ERROR)
		return false;

	/* turn off LED due to a safety fault */
	rv = sb_read_mfgacc(PARAM_SAFETY_STATUS,
			    SB_ALT_MANUFACTURER_ACCESS, data, sizeof(data));
	if (rv)
		return false;
	/*
	 * Each bit represents for one safey status, and normally they should
	 * all be 0. Data reads from LSB to MSB.
	 * data[2] - BIT 7-0
	 * AOLDL, AOLD, OCD2, OCD1, OCC2, OCC1, COV, CUV
	 *
	 * data[3] - BIT 15-8
	 * RSVD, CUVC, OTD, OTC, ASCDL, ASCD, ASCCL, ASCC
	 *
	 * data[4] - BIT 23-16
	 * CHGC, OC, RSVD, CTO, RSVD, PTO, RSVD, OTF
	 *
	 * data[5] - BIT 31-24
	 * RSVD, RSVD, OCDL, COVL, UTD, UTC, PCHGC, CHGV
	 */
	if (data[2] || data[3] || data[4] || data[5])
		return true;

	return false;
}

__override void led_set_color_battery(enum ec_led_colors color)
{
	switch (color) {
	case EC_LED_COLOR_WHITE:
		/* Ports are controlled by different GPIO */
		if (battery_safety_check()) {
			gpio_set_level(GPIO_BAT_LED_WHITE_L, LED_OFF_LVL);
			gpio_set_level(GPIO_EC_CHG_LED_R_W, LED_OFF_LVL);
		} else if (charge_manager_get_active_charge_port() == 1 ||
			system_get_board_version() < 3) {
			gpio_set_level(GPIO_BAT_LED_WHITE_L, LED_ON_LVL);
			gpio_set_level(GPIO_BAT_LED_AMBER_L, LED_OFF_LVL);
		} else if (charge_manager_get_active_charge_port() == 0) {
			gpio_set_level(GPIO_EC_CHG_LED_R_W, LED_ON_LVL);
			gpio_set_level(GPIO_EC_CHG_LED_R_Y, LED_OFF_LVL);
		}
		break;
	case EC_LED_COLOR_AMBER:
		/* Ports are controlled by different GPIO */
		if (battery_safety_check()) {
			gpio_set_level(GPIO_BAT_LED_AMBER_L, LED_OFF_LVL);
			gpio_set_level(GPIO_EC_CHG_LED_R_Y, LED_OFF_LVL);
		} else if (charge_get_state() == PWR_STATE_ERROR &&
				system_get_board_version() >= 3) {
			gpio_set_level(GPIO_EC_CHG_LED_R_W, LED_OFF_LVL);
			gpio_set_level(GPIO_EC_CHG_LED_R_Y, LED_ON_LVL);
		} else if (charge_manager_get_active_charge_port() == 1 ||
				system_get_board_version() < 3) {
			gpio_set_level(GPIO_BAT_LED_WHITE_L, LED_OFF_LVL);
			gpio_set_level(GPIO_BAT_LED_AMBER_L, LED_ON_LVL);
			gpio_set_level(GPIO_EC_CHG_LED_R_Y, LED_OFF_LVL);
		} else if (charge_manager_get_active_charge_port() == 0) {
			gpio_set_level(GPIO_EC_CHG_LED_R_W, LED_OFF_LVL);
			gpio_set_level(GPIO_EC_CHG_LED_R_Y, LED_ON_LVL);
		} else if (charge_get_percent() <
				CONFIG_LED_ONOFF_STATES_BAT_LOW) {
			gpio_set_level(GPIO_EC_CHG_LED_R_W, LED_OFF_LVL);
			gpio_set_level(GPIO_EC_CHG_LED_R_Y, LED_ON_LVL);
		}
		break;
	default: /* LED_OFF and other unsupported colors */
		gpio_set_level(GPIO_BAT_LED_WHITE_L, LED_OFF_LVL);
		gpio_set_level(GPIO_BAT_LED_AMBER_L, LED_OFF_LVL);
		gpio_set_level(GPIO_EC_CHG_LED_R_W, LED_OFF_LVL);
		gpio_set_level(GPIO_EC_CHG_LED_R_Y, LED_OFF_LVL);
		break;
	}
}

void led_get_brightness_range(enum ec_led_id led_id, uint8_t *brightness_range)
{
	if (led_id == EC_LED_ID_BATTERY_LED) {
		brightness_range[EC_LED_COLOR_WHITE] = 1;
		brightness_range[EC_LED_COLOR_AMBER] = 1;
	} else if (led_id == EC_LED_ID_POWER_LED) {
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
	} else if (led_id == EC_LED_ID_POWER_LED) {
		if (brightness[EC_LED_COLOR_WHITE] != 0)
			led_set_color_power(EC_LED_COLOR_WHITE);
		else
			led_set_color_power(LED_OFF);
	}

	return EC_SUCCESS;
}
