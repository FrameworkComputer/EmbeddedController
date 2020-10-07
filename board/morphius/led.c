/* Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "battery.h"
#include "charge_manager.h"
#include "charge_state.h"
#include "chipset.h"
#include "cros_board_info.h"
#include "ec_commands.h"
#include "extpower.h"
#include "gpio.h"
#include "hooks.h"
#include "led_common.h"
#include "pwm.h"
#include "timer.h"
#include "util.h"

#define LED_BAT_OFF_LVL	0
#define LED_BAT_ON_LVL	1
#define LED_BAT_S3_OFF_TIME_MS 3000
#define LED_BAT_S3_PWM_RESCALE 5
#define LED_BAT_S3_TICK_MS 50

#define LED_TOTAL_TICKS 2
#define LED_ON_TICKS 1

#define LED_PWR_TICKS_PER_CYCLE 7

#define TICKS_STEP1_BRIGHTER 0
#define TICKS_STEP2_DIMMER 20
#define TICKS_STEP3_OFF 40

static int ticks;

const enum ec_led_id supported_led_ids[] = {
	EC_LED_ID_BATTERY_LED,
	EC_LED_ID_POWER_LED
};

const int supported_led_ids_count = ARRAY_SIZE(supported_led_ids);

enum led_color {
	LED_OFF = 0,
	LED_WHITE,
	LED_AMBER,
	LED_COLOR_COUNT  /* Number of colors, not a color itself */
};

/* PWM brightness vs. color, in the order of off, white */
static const uint8_t color_brightness[2] = {
	[LED_OFF]   = 0,
	[LED_WHITE]   = 100,
};

void led_set_color_power(enum ec_led_colors color)
{
	pwm_set_duty(PWM_CH_POWER_LED, color_brightness[color]);
}

void led_set_color_battery(enum ec_led_colors color)
{
	uint32_t board_ver = 0;
	int led_batt_on_lvl, led_batt_off_lvl;

	cbi_get_board_version(&board_ver);
	if (board_ver >= 3) {
		led_batt_on_lvl = LED_BAT_ON_LVL;
		led_batt_off_lvl = LED_BAT_OFF_LVL;
	} else {
		led_batt_on_lvl = !LED_BAT_ON_LVL;
		led_batt_off_lvl = !LED_BAT_OFF_LVL;
	}

	switch (color) {
	case LED_AMBER:
		gpio_set_level(GPIO_LED_FULL_L, led_batt_off_lvl);
		gpio_set_level(GPIO_LED_CHRG_L, led_batt_on_lvl);
		break;
	case LED_WHITE:
		gpio_set_level(GPIO_LED_FULL_L, led_batt_on_lvl);
		gpio_set_level(GPIO_LED_CHRG_L, led_batt_off_lvl);
		break;
	default: /* LED_OFF and other unsupported colors */
		gpio_set_level(GPIO_LED_FULL_L, led_batt_off_lvl);
		gpio_set_level(GPIO_LED_CHRG_L, led_batt_off_lvl);
		break;
	}
}

void led_get_brightness_range(enum ec_led_id led_id, uint8_t *brightness_range)
{
	if (led_id == EC_LED_ID_BATTERY_LED) {
		brightness_range[EC_LED_COLOR_AMBER] = 1;
		brightness_range[EC_LED_COLOR_WHITE] = 1;
	} else if (led_id == EC_LED_ID_POWER_LED) {
		brightness_range[EC_LED_COLOR_WHITE] = 100;
	}
}

int led_set_brightness(enum ec_led_id led_id, const uint8_t *brightness)
{
	if (led_id == EC_LED_ID_BATTERY_LED) {
		if (brightness[EC_LED_COLOR_AMBER] != 0)
			led_set_color_battery(LED_AMBER);
		else if (brightness[EC_LED_COLOR_WHITE] != 0)
			led_set_color_battery(LED_WHITE);
		else
			led_set_color_battery(LED_OFF);
	} else if (led_id == EC_LED_ID_POWER_LED) {
		if (brightness[EC_LED_COLOR_WHITE] != 0)
			pwm_set_duty(PWM_CH_POWER_LED,
						color_brightness[LED_WHITE]);
		else
			pwm_set_duty(PWM_CH_POWER_LED,
						color_brightness[LED_OFF]);
	}

	return EC_SUCCESS;
}

static void suspend_led_update_deferred(void);
DECLARE_DEFERRED(suspend_led_update_deferred);

static void suspend_led_update_deferred(void)
{
	int delay = LED_BAT_S3_TICK_MS * MSEC;

	ticks++;

	/* 1s gradual on, 1s gradual off, 3s off */
	if (ticks <= TICKS_STEP2_DIMMER) {
		pwm_set_duty(PWM_CH_POWER_LED, ticks * LED_BAT_S3_PWM_RESCALE);
	} else if (ticks <= TICKS_STEP3_OFF) {
		pwm_set_duty(PWM_CH_POWER_LED,
					(TICKS_STEP3_OFF - ticks) * LED_BAT_S3_PWM_RESCALE);
	} else {
		ticks = TICKS_STEP1_BRIGHTER;
		delay = LED_BAT_S3_OFF_TIME_MS * MSEC;
	}

	hook_call_deferred(&suspend_led_update_deferred_data, delay);
}

static void suspend_led_init(void)
{
	ticks = TICKS_STEP2_DIMMER;

	hook_call_deferred(&suspend_led_update_deferred_data, 0);
}
DECLARE_HOOK(HOOK_CHIPSET_SUSPEND, suspend_led_init, HOOK_PRIO_DEFAULT);

static void suspend_led_deinit(void)
{
	hook_call_deferred(&suspend_led_update_deferred_data, -1);
}
DECLARE_HOOK(HOOK_CHIPSET_RESUME, suspend_led_deinit, HOOK_PRIO_DEFAULT);
DECLARE_HOOK(HOOK_CHIPSET_SHUTDOWN, suspend_led_deinit, HOOK_PRIO_DEFAULT);

static void led_set_battery(void)
{
	static int battery_ticks;
	uint32_t chflags = charge_get_flags();

	battery_ticks++;

	switch (charge_get_state()) {
	case PWR_STATE_CHARGE:
		/* Always indicate when charging, even in suspend. */
		led_set_color_battery(LED_AMBER);
		break;
	case PWR_STATE_DISCHARGE:
		led_set_color_battery(LED_OFF);
		break;
	case PWR_STATE_CHARGE_NEAR_FULL:
		led_set_color_battery(LED_WHITE);
		break;
	case PWR_STATE_IDLE: /* External power connected in IDLE */
		if (chflags & CHARGE_FLAG_FORCE_IDLE)
			led_set_color_battery(
				(battery_ticks & 0x4) ? LED_AMBER : LED_OFF);
		else
			led_set_color_battery(LED_WHITE);
		break;
	default:
		/* Other states don't alter LED behavior */
		break;
	}
}

static void led_set_power(void)
{
	static int power_ticks;
	static int previous_state_suspend;
	static int blink_ticks;

	power_ticks++;

	/* Blink 3 times (0.25s on/0.25s off, repeat 3 times) */
	if (extpower_is_present()) {
		blink_ticks++;
		if (!previous_state_suspend)
			power_ticks = 0;

		while (blink_ticks < LED_PWR_TICKS_PER_CYCLE) {
			led_set_color_power(
				(power_ticks % LED_TOTAL_TICKS) < LED_ON_TICKS ?
				LED_WHITE : LED_OFF);

			previous_state_suspend = 1;
			return;
		}
	}
	if (!extpower_is_present())
		blink_ticks = 0;

	previous_state_suspend = 0;

	if (chipset_in_state(CHIPSET_STATE_SOFT_OFF))
		led_set_color_power(LED_OFF);
	if (chipset_in_state(CHIPSET_STATE_ON))
		led_set_color_power(LED_WHITE);
}

static void pwr_led_init(void)
{
	/* Configure GPIOs */
	gpio_config_module(MODULE_PWM, 1);

	/*
	 * Enable PWMs and set to 0% duty cycle.  If they're disabled,
	 * seems to ground the pins instead of letting them float.
	 */
	pwm_enable(PWM_CH_POWER_LED, 1);
	led_set_color_power(LED_OFF);
}
DECLARE_HOOK(HOOK_INIT, pwr_led_init, HOOK_PRIO_DEFAULT);

/* Called by hook task every 200 ms */
static void led_tick(void)
{
	if (led_auto_control_is_enabled(EC_LED_ID_POWER_LED))
		led_set_power();
	if (led_auto_control_is_enabled(EC_LED_ID_BATTERY_LED))
		led_set_battery();
}
DECLARE_HOOK(HOOK_TICK, led_tick, HOOK_PRIO_DEFAULT);
