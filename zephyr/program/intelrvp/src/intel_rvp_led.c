/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "battery.h"
#include "charge_manager.h"
#include "charge_state.h"
#include "chipset.h"
#include "common.h"
#include "console.h"
#include "ec_commands.h"
#include "extpower.h"
#include "hooks.h"
#include "led_common.h"
#include "led_pwm.h"
#include "pwm.h"
#include "timer.h"
#include "util.h"

/* Battery percentage thresholds to blink at different rates. */
#define LOW_BATTERY_PERCENTAGE 10
#define NORMAL_BATTERY_PERCENTAGE 90

#define LED_OFF -1

#define LED_PULSE_TICK (125 * USEC_PER_MSEC)

#define LED_FAST_PULSE_PERIOD (250 / 125) /* 250 ms */
#define LED_SLOW_PULSE_PERIOD ((2 * USEC_PER_MSEC) / 125) /* 2 sec  */

struct led_pulse_data {
	bool led_is_pulsing;
	uint8_t led_pulse_period;
	uint8_t led_tick_count;
};

static struct led_pulse_data rvp_led[CONFIG_LED_PWM_COUNT];

static void pulse_led_deferred(void);
DECLARE_DEFERRED(pulse_led_deferred);

static void pulse_led_deferred(void)
{
	int i = 0;
	bool call_deferred = false;

	for (i = 0; i < CONFIG_LED_PWM_COUNT; i++) {
		if (!rvp_led[i].led_is_pulsing) {
			rvp_led[i].led_tick_count = 0;
			continue;
		}

		/*
		 * LED will be in ON state first half of the pulse period
		 * and in OFF state in second half of the pulse period.
		 */
		if (rvp_led[i].led_tick_count <
		    (rvp_led[i].led_pulse_period >> 1))
			set_pwm_led_color(i, EC_LED_COLOR_GREEN);
		else
			set_pwm_led_color(i, LED_OFF);

		rvp_led[i].led_tick_count = (rvp_led[i].led_tick_count + 1) %
					    rvp_led[i].led_pulse_period;
		call_deferred = true;
	}

	if (call_deferred)
		hook_call_deferred(&pulse_led_deferred_data, LED_PULSE_TICK);
}

static void pulse_leds(enum pwm_led_id id, int period)
{
	rvp_led[id].led_pulse_period = period;
	rvp_led[id].led_is_pulsing = true;

	pulse_led_deferred();
}

static void update_charger_led(enum pwm_led_id id)
{
	enum led_pwr_state chg_st = led_pwr_get_state();

	/*
	 * The colors listed below are the default, but can be overridden.
	 *
	 * Fast Flash = Charging error
	 * Slow Flash = Discharging
	 * LED on     = Charging
	 * LED off    = No Charger connected
	 */
	if (chg_st == LED_PWRS_CHARGE || chg_st == LED_PWRS_CHARGE_NEAR_FULL) {
		/* Charging: LED ON */
		rvp_led[id].led_is_pulsing = false;
		set_pwm_led_color(id, EC_LED_COLOR_GREEN);
	} else if (chg_st == LED_PWRS_DISCHARGE ||
		   chg_st == LED_PWRS_DISCHARGE_FULL) {
		if (extpower_is_present()) {
			/* Discharging:
			 * Flash slower (2 second period, 100% duty cycle)
			 */
			pulse_leds(id, LED_SLOW_PULSE_PERIOD);
		} else {
			/* No Charger connected: LED OFF */
			rvp_led[id].led_is_pulsing = false;
			set_pwm_led_color(id, LED_OFF);
		}
	} else if (chg_st == LED_PWRS_ERROR) {
		/* Charging error:
		 * Flash faster (250 ms period, 100% duty cycle)
		 */
		pulse_leds(id, LED_FAST_PULSE_PERIOD);
	} else {
		/* LED OFF */
		rvp_led[id].led_is_pulsing = false;
		set_pwm_led_color(id, LED_OFF);
	}
}

static void update_battery_led(enum pwm_led_id id)
{
	/*
	 * Fast Flash = Low Battery
	 * Slow Flash = Normal Battery
	 * LED on     = Full Battery
	 * LED off    = No Battery
	 */
	if (battery_is_present() == BP_YES) {
		int batt_percentage = charge_get_percent();

		if (batt_percentage < LOW_BATTERY_PERCENTAGE) {
			/* Low Battery:
			 * Flash faster (250 ms period, 100% duty cycle)
			 */
			pulse_leds(id, LED_FAST_PULSE_PERIOD);
		} else if (batt_percentage < NORMAL_BATTERY_PERCENTAGE) {
			/* Normal Battery:
			 * Flash slower (2 second period, 100% duty cycle)
			 */
			pulse_leds(id, LED_SLOW_PULSE_PERIOD);
		} else {
			/* Full Battery: LED ON */
			rvp_led[id].led_is_pulsing = false;
			set_pwm_led_color(id, EC_LED_COLOR_GREEN);
		}
	} else {
		/* No Battery: LED OFF */
		rvp_led[id].led_is_pulsing = false;
		set_pwm_led_color(id, LED_OFF);
	}
}

static void init_rvp_leds_off(void)
{
	/* Turn off LEDs such that they are in a known state with zero duty. */
	set_pwm_led_color(PWM_LED0, LED_OFF);
	set_pwm_led_color(PWM_LED1, LED_OFF);
}
DECLARE_HOOK(HOOK_INIT, init_rvp_leds_off, HOOK_PRIO_POST_PWM);

static void update_led(void)
{
	update_battery_led(PWM_LED0);
	update_charger_led(PWM_LED1);
}
DECLARE_HOOK(HOOK_SECOND, update_led, HOOK_PRIO_DEFAULT);
