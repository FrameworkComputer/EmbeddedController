/* Copyright 2018 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Power and battery LED state control
 */

#include "battery.h"
#include "charge_state.h"
#include "chipset.h"
#include "console.h"
#include "ec_commands.h"
#include "extpower.h"
#include "hooks.h"
#include "led_common.h"
#include "led_onoff_states.h"

#define CPRINTS(format, args...) cprints(CC_GPIO, format, ## args)

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
			if (chipset_in_state(CHIPSET_STATE_ANY_OFF))
				new_state = STATE_CHARGING_FULL_S5;
			else
				new_state = STATE_CHARGING_FULL_CHARGE;
		break;
	case PWR_STATE_DISCHARGE_FULL:
		if (extpower_is_present()) {
			if (chipset_in_state(CHIPSET_STATE_ANY_OFF))
				new_state = STATE_CHARGING_FULL_S5;
			else
				new_state = STATE_CHARGING_FULL_CHARGE;
			break;
		}
		/* Intentional fall-through */
	case PWR_STATE_DISCHARGE /* and PWR_STATE_DISCHARGE_FULL */:
		if (chipset_in_state(CHIPSET_STATE_ON)) {
#ifdef CONFIG_LED_ONOFF_STATES_BAT_LOW
			if (charge_get_percent() <
				CONFIG_LED_ONOFF_STATES_BAT_LOW)
				new_state = STATE_DISCHARGE_S0_BAT_LOW;
			else
#endif
				new_state = STATE_DISCHARGE_S0;
		} else if (chipset_in_state(CHIPSET_STATE_ANY_SUSPEND))
			new_state = STATE_DISCHARGE_S3;
		else
			new_state = STATE_DISCHARGE_S5;
		break;
	case PWR_STATE_ERROR:
		new_state = STATE_BATTERY_ERROR;
		break;
	case PWR_STATE_CHARGE_NEAR_FULL:
		if (chipset_in_state(CHIPSET_STATE_ANY_OFF))
			new_state = STATE_CHARGING_FULL_S5;
		else
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
	static int led_state = LED_NUM_STATES;
	int phase;
	enum led_states desired_state = led_get_state();

	/*
	 * We always need to check the current state since the value could
	 * have been manually overwritten. If we're in a new valid state,
	 * update our ticks and period info. If our new state isn't defined,
	 * continue using the previous one.
	 */
	if (desired_state != led_state && desired_state < LED_NUM_STATES) {
		/*
		 * Allow optional CHARGING_FULL_S5 state to fall back to
		 * FULL_CHARGE if not defined.
		 */
		if (desired_state == STATE_CHARGING_FULL_S5 &&
		    led_bat_state_table[desired_state][LED_PHASE_0].time == 0)
			desired_state = STATE_CHARGING_FULL_CHARGE;

		/* State is changing */
		led_state = desired_state;
		/* Reset ticks and period when state changes */
		ticks = 0;

		period = led_bat_state_table[led_state][LED_PHASE_0].time +
			led_bat_state_table[led_state][LED_PHASE_1].time;

	}

	/* If this state is undefined, turn the LED off */
	if (period == 0) {
		CPRINTS("Undefined LED behavior for battery state %d,"
			"turning off LED", led_state);
		led_set_color_battery(LED_OFF);
		return;
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

#ifdef CONFIG_LED_POWER_LED
static enum pwr_led_states pwr_led_get_state(void)
{
	if (chipset_in_state(CHIPSET_STATE_ANY_SUSPEND)) {
		if (extpower_is_present())
			return PWR_LED_STATE_SUSPEND_AC;
		else
			return PWR_LED_STATE_SUSPEND_NO_AC;
	} else if (chipset_in_state(CHIPSET_STATE_ANY_OFF)) {
		return PWR_LED_STATE_OFF;
	} else if (chipset_in_state(CHIPSET_STATE_ON)) {
		return PWR_LED_STATE_ON;
	}

	return PWR_LED_NUM_STATES;
}

static void led_update_power(void)
{
	static uint8_t ticks, period;
	static enum pwr_led_states led_state = PWR_LED_NUM_STATES;
	int phase;
	enum pwr_led_states desired_state = pwr_led_get_state();

	/*
	 * If we're in a new valid state, update our ticks and period info.
	 * Otherwise, continue to use old state
	 */
	if (desired_state != led_state && desired_state < PWR_LED_NUM_STATES) {
		/* State is changing */
		led_state = desired_state;
		/* Reset ticks and period when state changes */
		ticks = 0;

		period = led_pwr_state_table[led_state][LED_PHASE_0].time +
			led_pwr_state_table[led_state][LED_PHASE_1].time;

	}

	/* If this state is undefined, turn the LED off */
	if (period == 0) {
		CPRINTS("Undefined LED behavior for power state %d,"
			"turning off LED", led_state);
		led_set_color_power(LED_OFF);
		return;
	}

	/*
	 * Determine which phase of the state table to use. The phase is
	 * determined if it falls within first phase time duration.
	 */
	phase = ticks < led_pwr_state_table[led_state][LED_PHASE_0].time ?
									0 : 1;
	ticks = (ticks + 1) % period;

	/* Set the color for the given state and phase */
	led_set_color_power(led_pwr_state_table[led_state][phase].color);

}
#endif

static void led_init(void)
{
	/* If battery LED is enabled, set it to "off" to start with */
	if (led_auto_control_is_enabled(EC_LED_ID_BATTERY_LED))
		led_set_color_battery(LED_OFF);

	/* If power LED is enabled, set it to "off" to start with */
#ifdef CONFIG_LED_POWER_LED
	if (led_auto_control_is_enabled(EC_LED_ID_POWER_LED))
		led_set_color_power(LED_OFF);
#endif /* CONFIG_LED_POWER_LED */

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
#ifdef CONFIG_LED_POWER_LED
	if (led_auto_control_is_enabled(EC_LED_ID_POWER_LED))
		led_update_power();
#endif
}
DECLARE_HOOK(HOOK_TICK, led_update, HOOK_PRIO_DEFAULT);
