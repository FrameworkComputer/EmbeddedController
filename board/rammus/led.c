/* Copyright 2017 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Power and battery LED control.
 */

#include "battery.h"
#include "charge_manager.h"
#include "charge_state.h"
#include "chipset.h"
#include "ec_commands.h"
#include "extpower.h"
#include "gpio.h"
#include "hooks.h"
#include "host_command.h"
#include "led_common.h"
#include "system.h"
#include "util.h"

#define LED_ON 1
#define LED_OFF 0
#define LED_TOTAL_TICKS 20
#define LED_CHARGE_PULSE 10
#define LED_POWER_PULSE 15

const enum ec_led_id supported_led_ids[] = { EC_LED_ID_POWER_LED,
					     EC_LED_ID_BATTERY_LED };

const int supported_led_ids_count = ARRAY_SIZE(supported_led_ids);

enum led_charge_state {
	LED_STATE_DISCHARGE = 0,
	LED_STATE_CHARGE,
	LED_STATE_FULL,
	LED_STATE_ERROR_PHASE0,
	LED_STATE_ERROR_PHASE1,
	LED_CHARGE_STATE_COUNT
};

enum led_power_state {
	LED_STATE_S0 = 0,
	LED_STATE_S3_PHASE0,
	LED_STATE_S3_PHASE1,
	LED_STATE_S5,
	LED_POWER_STATE_COUNT
};

static const struct {
	uint8_t led1 : 1;
	uint8_t led2 : 1;
} led_chg_state_table[] = { [LED_STATE_DISCHARGE] = { LED_OFF, LED_OFF },
			    [LED_STATE_CHARGE] = { LED_OFF, LED_ON },
			    [LED_STATE_FULL] = { LED_ON, LED_OFF },
			    [LED_STATE_ERROR_PHASE0] = { LED_OFF, LED_OFF },
			    [LED_STATE_ERROR_PHASE1] = { LED_OFF, LED_ON } };

static const struct {
	uint8_t led : 1;
} led_pwr_state_table[] = { [LED_STATE_S0] = { LED_ON },
			    [LED_STATE_S3_PHASE0] = { LED_OFF },
			    [LED_STATE_S3_PHASE1] = { LED_ON },
			    [LED_STATE_S5] = { LED_OFF } };

void led_get_brightness_range(enum ec_led_id led_id, uint8_t *brightness_range)
{
	brightness_range[EC_LED_COLOR_WHITE] = 1;
	brightness_range[EC_LED_COLOR_GREEN] = 1;
	brightness_range[EC_LED_COLOR_AMBER] = 1;
}

int led_set_brightness(enum ec_led_id led_id, const uint8_t *brightness)
{
	gpio_set_level(GPIO_PWR_LED, brightness[EC_LED_COLOR_WHITE]);
	gpio_set_level(GPIO_CHG_LED1, brightness[EC_LED_COLOR_GREEN]);
	gpio_set_level(GPIO_CHG_LED2, brightness[EC_LED_COLOR_AMBER]);

	return EC_SUCCESS;
}

void config_power_led(enum led_power_state state)
{
	gpio_set_level(GPIO_PWR_LED, led_pwr_state_table[state].led);
}

void config_battery_led(enum led_charge_state state)
{
	gpio_set_level(GPIO_CHG_LED1, led_chg_state_table[state].led1);
	gpio_set_level(GPIO_CHG_LED2, led_chg_state_table[state].led2);
}

static void rammus_led_set_power(void)
{
	static unsigned int power_ticks;
	int chipset_state;

	chipset_state = chipset_in_state(CHIPSET_STATE_HARD_OFF) |
			(chipset_in_state(CHIPSET_STATE_SOFT_OFF) << 1) |
			(chipset_in_state(CHIPSET_STATE_SUSPEND) << 2) |
			(chipset_in_state(CHIPSET_STATE_ON) << 3) |
			(chipset_in_state(CHIPSET_STATE_STANDBY) << 4);

	switch (chipset_state) {
	case CHIPSET_STATE_ON:
		config_power_led(LED_STATE_S0);
		power_ticks = 0;
		break;
	case CHIPSET_STATE_SUSPEND:
	case CHIPSET_STATE_STANDBY:
		if ((power_ticks++ % LED_TOTAL_TICKS) < LED_POWER_PULSE)
			config_power_led(LED_STATE_S3_PHASE0);
		else
			config_power_led(LED_STATE_S3_PHASE1);
		break;
	case CHIPSET_STATE_HARD_OFF:
	case CHIPSET_STATE_SOFT_OFF:
		config_power_led(LED_STATE_S5);
		power_ticks = 0;
		break;
	default:
		break;
	}
}

static void rammus_led_set_battery(void)
{
	enum led_pwr_state chg_state = led_pwr_get_state();
	int charge_percent = charge_get_percent();
	static unsigned int charge_ticks;

	/* CHIPSET_STATE_OFF */
	switch (chg_state) {
	case LED_PWRS_DISCHARGE:
		if (extpower_is_present() &&
		    charge_percent >= CONFIG_BATT_HOST_FULL_FACTOR)
			config_battery_led(LED_STATE_FULL);
		else
			config_battery_led(LED_STATE_DISCHARGE);
		charge_ticks = 0;
		break;
	case LED_PWRS_CHARGE:
		config_battery_led(LED_STATE_CHARGE);
		charge_ticks = 0;
		break;
	case LED_PWRS_ERROR:
		if ((charge_ticks++ % LED_TOTAL_TICKS) < LED_CHARGE_PULSE)
			config_battery_led(LED_STATE_ERROR_PHASE0);
		else
			config_battery_led(LED_STATE_ERROR_PHASE1);
		break;
	case LED_PWRS_CHARGE_NEAR_FULL:
	case LED_PWRS_IDLE:
		config_battery_led(LED_STATE_DISCHARGE);
		charge_ticks = 0;
		break;
	case LED_PWRS_FORCED_IDLE:
		config_battery_led(LED_STATE_FULL);
		charge_ticks = 0;
		break;
	default:
		break;
	}
}

/**
 * Called by hook task every 200 ms
 */
static void led_tick(void)
{
	if (led_auto_control_is_enabled(EC_LED_ID_POWER_LED))
		rammus_led_set_power();

	if (led_auto_control_is_enabled(EC_LED_ID_BATTERY_LED))
		rammus_led_set_battery();
}

DECLARE_HOOK(HOOK_TICK, led_tick, HOOK_PRIO_DEFAULT);
