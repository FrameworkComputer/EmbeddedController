/* Copyright 2018 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Power and battery LED state control for octopus boards
 */

#include "battery.h"
#include "charge_state.h"
#include "chipset.h"
#include "ec_commands.h"
#include "extpower.h"
#include "hooks.h"
#include "led_common.h"
#include "led_states.h"

static enum led_states led_get_state(void)
{
	int  charge_lvl;
	enum led_states new_state = LED_NUM_STATES;

	switch (charge_get_state()) {
	case PWR_STATE_CHARGE:
		/* Get percent charge */
		charge_lvl = charge_get_percent();
		/* Determine which charge state to use */
		if (charge_lvl < led_charge_lvl_1)
			new_state = STATE_CHARGING_LVL_1;
		else if (charge_lvl < led_charge_lvl_2)
			new_state = STATE_CHARGING_LVL_2;
		else
			new_state = STATE_CHARGING_FULL_CHARGE;
		break;
	case PWR_STATE_DISCHARGE_FULL:
		if (extpower_is_present()) {
			new_state = STATE_CHARGING_FULL_CHARGE;
			break;
		}
		/* Intentional fall-through */
	case PWR_STATE_DISCHARGE /* and PWR_STATE_DISCHARGE_FULL */:
		if (chipset_in_state(CHIPSET_STATE_ON))
			new_state = STATE_DISCHARGE_S0;
		else if (chipset_in_state(CHIPSET_STATE_ANY_SUSPEND))
			new_state = STATE_DISCHARGE_S3;
		else
			new_state = STATE_DISCHARGE_S5;
		break;
	case PWR_STATE_ERROR:
		new_state = STATE_BATTERY_ERROR;
		break;
	case PWR_STATE_CHARGE_NEAR_FULL:
		new_state = STATE_CHARGING_FULL_CHARGE;
		break;
	case PWR_STATE_IDLE: /* External power connected in IDLE */
		if (charge_get_flags() & CHARGE_FLAG_FORCE_IDLE)
			new_state = STATE_FACTORY_TEST;
		else
			new_state = STATE_DISCHARGE_S0;
		break;
	default:
		/* Other states don't alter LED behavior */
		break;
	}

	return new_state;
}

static void led_update_battery(void)
{
	static uint8_t ticks, period;
	static int led_state = STATE_DEFAULT;
	int phase;
	enum led_states desired_state = led_get_state();

	/*
	 * We always need to check the current state since the value could
	 * have been manually overwritten. If we're in a new valid state,
	 * update our ticks and period info. If our new state isn't defined,
	 * continue using the previous one.
	 */
	if (desired_state != led_state && desired_state < LED_NUM_STATES) {
		/* State is changing */
		led_state = desired_state;
		/* Reset ticks and period when state changes */
		ticks = 0;

		period = led_bat_state_table[led_state][LED_PHASE_0].time +
			led_bat_state_table[led_state][LED_PHASE_1].time;

	}

	/*
	 * Determine which phase of the state table to use. The phase is
	 * determined if it falls within first phase time duration.
	 */
	phase = ticks < led_bat_state_table[led_state][LED_PHASE_0].time ?
									0 : 1;
	ticks = (ticks + 1) % period;

	/* Set the color for the given state and phase */
	led_set_color_battery(led_bat_state_table[led_state][phase].color);
}

#ifdef OCTOPUS_POWER_LED
static void led_update_power(void)
{
	int enable;
	static int ticks;
	enum led_states desired_state;

	if (chipset_in_state(CHIPSET_STATE_ON)) {
		/* In S0 power LED is always on */
		enable = 1;
		ticks = 0;
	} else if (chipset_in_state(CHIPSET_STATE_ANY_SUSPEND)) {
		desired_state = led_get_state();
		if (desired_state == STATE_CHARGING_FULL_CHARGE ||
		desired_state == STATE_CHARGING_LVL_1 ||
		desired_state == STATE_CHARGING_LVL_2) {
			int period;
			int led_power_on_ticks = led_power_blink_on_msec / HOOK_TICK_INTERVAL_MS;
			int led_power_off_ticks = led_power_blink_off_msec / HOOK_TICK_INTERVAL_MS;

			/*
			* If in suspend/standby and the device is charging, then the
			* power LED is blinking.
			*/
			period = led_power_on_ticks + led_power_off_ticks;
			enable = ticks % period < led_power_off_ticks ?
				0 : 1;
			ticks++;
		} else {
			enable = 0;
			ticks = 0;
		}
	} else {
		enable = 0;
		ticks = 0;
	}

	led_set_color_power(enable);
}
#endif

static void led_init(void)
{
	/* If battery LED is enabled, set it to "off" to start with */
	if (led_auto_control_is_enabled(EC_LED_ID_BATTERY_LED))
		led_set_color_battery(LED_OFF);
}
DECLARE_HOOK(HOOK_INIT, led_init, HOOK_PRIO_DEFAULT);

/* Called by hook task every hook tick (200 msec) */
static void led_update(void)
{
	/*
	 * If battery LED is enabled, set its state based on our power and
	 * charge
	 */
	if (led_auto_control_is_enabled(EC_LED_ID_BATTERY_LED))
		led_update_battery();
#ifdef OCTOPUS_POWER_LED
	if (led_auto_control_is_enabled(EC_LED_ID_POWER_LED))
		led_update_power();
#endif
}
DECLARE_HOOK(HOOK_TICK, led_update, HOOK_PRIO_DEFAULT);
