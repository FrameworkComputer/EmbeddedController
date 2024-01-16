/* Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Primus specific PWM LED settings. */

#include "battery.h"
#include "charge_manager.h"
#include "charge_state.h"
#include "chipset.h"
#include "common.h"
#include "compile_time_macros.h"
#include "console.h"
#include "ec_commands.h"
#include "extpower.h"
#include "hooks.h"
#include "led_common.h"
#include "power.h"
#include "pwm.h"
#include "task.h"
#include "timer.h"
#include "util.h"

#include <stdint.h>
#define CPRINTS(format, args...) cprints(CC_LOGOLED, format, ##args)

#define LED_ON_LVL 100
#define LED_OFF_LVL 0
#define LED_BAT_S3_OFF_TIME_MS 3000
#define LED_BAT_S3_TICK_MS 50
#define LED_BAT_S3_PWM_RESCALE 5
#define LED_TOTAL_TICKS 6
#define TICKS_STEP1_BRIGHTER 0
#define TICKS_STEP2_DIMMER (1000 / LED_BAT_S3_TICK_MS)
#define TICKS_STEP3_OFF (2 * TICKS_STEP2_DIMMER)
#define LED_ONE_SEC (1000 / HOOK_TICK_INTERVAL_MS)
#define LED_LOGO_TICK_SEC (LED_ONE_SEC / 4)
/* Total on/off duration in a period */
#define PERIOD (LED_LOGO_TICK_SEC * 2)
#define LED_ON 1
#define LED_OFF EC_LED_COLOR_COUNT
#define LED_EVENT_SUSPEND TASK_EVENT_CUSTOM_BIT(0)
#define LED_EVENT_200MS_TICK TASK_EVENT_CUSTOM_BIT(1)
#define BATT_NEAR_FULL 900

static int tick;

const enum ec_led_id supported_led_ids[] = { EC_LED_ID_BATTERY_LED,
					     EC_LED_ID_POWER_LED };
const int supported_led_ids_count = ARRAY_SIZE(supported_led_ids);

static void led_set_color_battery(enum ec_led_colors color)
{
	switch (color) {
	case EC_LED_COLOR_AMBER:
		pwm_set_duty(PWM_CH_LED1_AMBER, LED_ON_LVL);
		pwm_set_duty(PWM_CH_LED2_WHITE, LED_OFF_LVL);
		break;
	case EC_LED_COLOR_WHITE:
		pwm_set_duty(PWM_CH_LED2_WHITE, LED_ON_LVL);
		pwm_set_duty(PWM_CH_LED1_AMBER, LED_OFF_LVL);
		break;
	default: /* LED_OFF and other unsupported colors */
		pwm_set_duty(PWM_CH_LED1_AMBER, LED_OFF_LVL);
		pwm_set_duty(PWM_CH_LED2_WHITE, LED_OFF_LVL);
		break;
	}
}

static void led_set_battery(void)
{
	switch (led_pwr_get_state()) {
	case LED_PWRS_CHARGE:
		/*
		 * Always indicate when charging, even in suspend.
		 * When the battery RSOC > 90, set LED to white.
		 */
		if (charge_get_display_charge() > BATT_NEAR_FULL)
			led_set_color_battery(EC_LED_COLOR_WHITE);
		else
			led_set_color_battery(EC_LED_COLOR_AMBER);

		break;
	case LED_PWRS_DISCHARGE:
		led_set_color_battery(LED_OFF);
		break;
	case LED_PWRS_CHARGE_NEAR_FULL:
		led_set_color_battery(EC_LED_COLOR_WHITE);
		break;
	default:
		/* Other states don't alter LED behavior */
		break;
	}
}

void led_set_color_power(int onoff_status)
{
	/* primus logo led and power led have same behavior. */
	if (onoff_status == LED_ON) {
		pwm_set_duty(PWM_CH_TKP_A_LED_N, LED_ON_LVL);
		pwm_set_duty(PWM_CH_LED4, LED_ON_LVL);
	} else {
		/* LED_OFF and unsupported colors */
		pwm_set_duty(PWM_CH_TKP_A_LED_N, LED_OFF_LVL);
		pwm_set_duty(PWM_CH_LED4, LED_OFF_LVL);
	}
}

#define AC_DISCONNECTED (-1)

static void led_set_power(void)
{
	static int plug_ac_countdown;
	static int ticks;

	if (plug_ac_countdown > 0) {
		ticks = (ticks + 1) % PERIOD;
		plug_ac_countdown--;
		if (ticks < LED_LOGO_TICK_SEC)
			led_set_color_power(LED_OFF);
		else
			led_set_color_power(LED_ON);
	} else if (chipset_in_state(CHIPSET_STATE_ON))
		led_set_color_power(LED_ON);
	else if (chipset_in_state(CHIPSET_STATE_ANY_OFF))
		led_set_color_power(LED_OFF);

	/* Check AC_PRESENT */
	if (!extpower_is_present())
		plug_ac_countdown = AC_DISCONNECTED;
	else if (plug_ac_countdown == AC_DISCONNECTED)
		/* AC power was plugged in (previous state was "disconnected"),
		 * set plug_ac_countdown for amount of ticks left
		 */
		plug_ac_countdown = LED_TOTAL_TICKS;
}

void led_get_brightness_range(enum ec_led_id led_id, uint8_t *brightness_range)
{
	if (led_id == EC_LED_ID_BATTERY_LED) {
		brightness_range[EC_LED_COLOR_AMBER] = 1;
		brightness_range[EC_LED_COLOR_WHITE] = 1;
	} else if (led_id == EC_LED_ID_POWER_LED) {
		brightness_range[EC_LED_COLOR_RED] = 1;
	}
}

int led_set_brightness(enum ec_led_id led_id, const uint8_t *brightness)
{
	if (led_id == EC_LED_ID_BATTERY_LED) {
		if (brightness[EC_LED_COLOR_AMBER] != 0)
			led_set_color_battery(EC_LED_COLOR_AMBER);
		else if (brightness[EC_LED_COLOR_WHITE] != 0)
			led_set_color_battery(EC_LED_COLOR_WHITE);
		else
			led_set_color_battery(LED_OFF);
	} else if (led_id == EC_LED_ID_POWER_LED) {
		if (brightness[EC_LED_COLOR_RED] != 0)
			led_set_color_power(LED_ON);
		else
			led_set_color_power(LED_OFF);
	}

	return EC_SUCCESS;
}

/* Called by hook task every 200 ms */
static void led_tick(void)
{
	task_set_event(TASK_ID_LOGOLED, LED_EVENT_200MS_TICK);
}
DECLARE_HOOK(HOOK_TICK, led_tick, HOOK_PRIO_DEFAULT);

static void suspend_led_update(void)
{
	while (1) {
		tick++;

		/* HOOK_CHIPSET_SUSPEND will be called when POWER_S0S0ix,
		 * if we are not transitioning to suspend, we should break here.
		 */
		if (!chipset_in_or_transitioning_to_state(
			    CHIPSET_STATE_ANY_SUSPEND))
			break;

		/* 1s gradual on, 1s gradual off, 3s off */
		if (tick <= TICKS_STEP2_DIMMER) {
			/* increase 5 duty every 50ms until PWM=100
			 * enter here 20 times, total duartion is 1sec
			 * A-cover and power button led are shared same
			 * behavior.
			 */
			pwm_set_duty(PWM_CH_TKP_A_LED_N,
				     tick * LED_BAT_S3_PWM_RESCALE);
			pwm_set_duty(PWM_CH_LED4,
				     tick * LED_BAT_S3_PWM_RESCALE);
			msleep(LED_BAT_S3_TICK_MS);
		} else if (tick <= TICKS_STEP3_OFF) {
			/* decrease 5 duty every 50ms until PWM=0
			 * enter here 20 times, total duartion is 1sec
			 * A-cover and power button led are shared same
			 * behavior.
			 */
			pwm_set_duty(PWM_CH_TKP_A_LED_N,
				     (TICKS_STEP3_OFF - tick) *
					     LED_BAT_S3_PWM_RESCALE);
			pwm_set_duty(PWM_CH_LED4,
				     (TICKS_STEP3_OFF - tick) *
					     LED_BAT_S3_PWM_RESCALE);
			msleep(LED_BAT_S3_TICK_MS);
		} else {
			tick = TICKS_STEP1_BRIGHTER;
			msleep(LED_BAT_S3_OFF_TIME_MS);
		}
	}
}

static void suspend_led_init(void)
{
	task_set_event(TASK_ID_LOGOLED, LED_EVENT_SUSPEND);
}
DECLARE_HOOK(HOOK_CHIPSET_SUSPEND, suspend_led_init, HOOK_PRIO_DEFAULT);

void logoled_task(void *u)
{
	uint32_t evt;

	while (1) {
		evt = task_wait_event(-1);

		if (evt & LED_EVENT_SUSPEND) {
			tick = TICKS_STEP2_DIMMER;
			suspend_led_update();
		}

		if (evt & LED_EVENT_200MS_TICK) {
			if (led_auto_control_is_enabled(EC_LED_ID_POWER_LED))
				led_set_power();
			if (led_auto_control_is_enabled(EC_LED_ID_BATTERY_LED))
				led_set_battery();
		}
	}
}
