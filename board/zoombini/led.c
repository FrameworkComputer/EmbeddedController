/* Copyright 2017 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Zoombini LED control to conform to Chrome OS LED behaviour specification. */

/*
 * TODO(crbug.com/752553): This should be turned into common code such that
 * boards that use PWM controlled LEDs can share code to follow the Chrome OS
 * LED behaviour spec.  If possible, tie into led_policy_std.c.
 */

#include "charge_state.h"
#include "chipset.h"
#include "common.h"
#include "hooks.h"
#include "pwm.h"
#include "timer.h"

enum led_id {
	EC_LED_ID_POWER = 0,
	EC_LED_ID_BATTERY,
	EC_LED_ID_COUNT,
};

static enum pwm_channel led_pwm_ch_map[EC_LED_ID_COUNT] = {
#ifdef BOARD_MEOWTH
	[EC_LED_ID_POWER] = PWM_CH_DB0_LED_GREEN,
	[EC_LED_ID_BATTERY] = PWM_CH_DB0_LED_RED,
#else /* !defined(BOARD_MEOWTH) */
	[EC_LED_ID_POWER] = PWM_CH_LED_GREEN,
	[EC_LED_ID_BATTERY] = PWM_CH_LED_RED,
#endif /* defined(BOARD_MEOWTH) */
};

static void set_led_state(enum led_id id, int on)
{
	int val;
#ifdef BOARD_MEOWTH
	val = on ? 98 : 100;
#else /* !defined(BOARD_MEOWTH) */
	val = on ? 90 : 100;
#endif /* defined(BOARD_MEOWTH) */

	pwm_set_duty(led_pwm_ch_map[id], val);
}

static uint8_t power_led_is_pulsing;

static void pulse_power_led(void);
DECLARE_DEFERRED(pulse_power_led);
static void pulse_power_led(void)
{
	static uint8_t tick_count;

	if (!power_led_is_pulsing) {
		tick_count = 0;
		return;
	}

	if (tick_count == 0)
		set_led_state(EC_LED_ID_POWER, 1);
	else
		set_led_state(EC_LED_ID_POWER, 0);

	/* 4 second period, 25% duty cycle. */
	tick_count = (tick_count + 1) % 4;
	hook_call_deferred(&pulse_power_led_data, SECOND);
}

static void power_led_update(void)
{
	if (chipset_in_state(CHIPSET_STATE_ON)) {
		/* Make sure we stop pulsing. */
		power_led_is_pulsing = 0;
		/* The power LED must be on in the Active state. */
		set_led_state(EC_LED_ID_POWER, 1);
	} else if (chipset_in_state(CHIPSET_STATE_ANY_SUSPEND)) {
		/* The power LED must pulse in the Suspend state. */
		if (!power_led_is_pulsing) {
			power_led_is_pulsing = 1;
			pulse_power_led();
		}
	} else if (chipset_in_state(CHIPSET_STATE_ANY_OFF)) {
		/* Make sure we stop pulsing. */
		power_led_is_pulsing = 0;
		/* The power LED must be off in the Deep Sleep state. */
		set_led_state(EC_LED_ID_POWER, 0);
	}
}

static void battery_led_update(void)
{
	enum charge_state chg_st = charge_get_state();

	switch (chg_st) {
	case PWR_STATE_DISCHARGE:
	case PWR_STATE_IDLE:
		set_led_state(EC_LED_ID_BATTERY, 0);
		break;

	/*
	 * We don't have another color to distingush full, so make it
	 * the same as charging.
	 */
	case PWR_STATE_CHARGE_NEAR_FULL:
	case PWR_STATE_CHARGE:
		set_led_state(EC_LED_ID_BATTERY, 1);
		break;

	default:
		break;
	}
}

static void update_leds(void)
{
	power_led_update();
	battery_led_update();
}
DECLARE_HOOK(HOOK_TICK, update_leds, HOOK_PRIO_DEFAULT);
