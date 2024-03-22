/* Copyright 2018 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Power and battery LED state control
 */

/*
 * TODO(b/272518464): Work around coreboot GCC preprocessor bug.
 * #line marks the *next* line, so it is off by one.
 */
#line 13

#include "battery.h"
#include "charge_state.h"
#include "chipset.h"
#include "console.h"
#include "ec_commands.h"
#include "extpower.h"
#include "hooks.h"
#include "led_common.h"
#include "led_onoff_states.h"
#include "system.h"
#include "util.h"

#define CPRINTS(format, args...) cprints(CC_GPIO, format, ##args)

/*
 * In order to support the battery LED being optional (ex. for Chromeboxes),
 * set up default battery table, setter, and variables.
 */
__overridable struct led_descriptor led_bat_state_table[LED_NUM_STATES]
						       [LED_NUM_PHASES];
__overridable const int led_charge_lvl_1;
__overridable const int led_charge_lvl_2;
__overridable void led_set_color_battery(enum ec_led_colors color)
{
}

#ifndef CONFIG_CHARGER
/* Include for the sake of compilation */
int charge_get_percent(void);
#endif

static int led_get_charge_percent(void)
{
	return DIV_ROUND_NEAREST(charge_get_display_charge(), 10);
}

static enum led_states led_get_state(void)
{
	int charge_lvl;
	enum led_states new_state = LED_NUM_STATES;

	if (!IS_ENABLED(CONFIG_CHARGER))
		return new_state;

	switch (led_pwr_get_state()) {
	case LED_PWRS_CHARGE:
		/* Get percent charge */
		charge_lvl = led_get_charge_percent();
		/* Determine which charge state to use */
		if (charge_lvl < led_charge_lvl_1)
			new_state = STATE_CHARGING_LVL_1;
		else if (charge_lvl < led_charge_lvl_2)
			new_state = STATE_CHARGING_LVL_2;
		else if (chipset_in_state(CHIPSET_STATE_ANY_OFF))
			new_state = STATE_CHARGING_FULL_S5;
		else
			new_state = STATE_CHARGING_FULL_CHARGE;
		break;
	case LED_PWRS_DISCHARGE_FULL:
		if (extpower_is_present()) {
			if (chipset_in_state(CHIPSET_STATE_ANY_OFF))
				new_state = STATE_CHARGING_FULL_S5;
			else
				new_state = STATE_CHARGING_FULL_CHARGE;
			break;
		}
		__fallthrough;
	case LED_PWRS_DISCHARGE /* and LED_PWRS_DISCHARGE_FULL */:
		if (chipset_in_state(CHIPSET_STATE_ON)) {
#ifdef CONFIG_LED_ONOFF_STATES_BAT_LOW
			if (led_get_charge_percent() <
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
	case LED_PWRS_ERROR:
		new_state = STATE_BATTERY_ERROR;
		break;
	case LED_PWRS_CHARGE_NEAR_FULL:
		if (chipset_in_state(CHIPSET_STATE_ANY_OFF))
			new_state = STATE_CHARGING_FULL_S5;
		else
			new_state = STATE_CHARGING_FULL_CHARGE;
		break;
	case LED_PWRS_IDLE: /* External power connected in IDLE */
		if (chipset_in_state(CHIPSET_STATE_ANY_OFF))
			new_state = STATE_DISCHARGE_S5;
		else
			new_state = STATE_DISCHARGE_S0;
		break;
	case LED_PWRS_FORCED_IDLE:
		new_state = STATE_FACTORY_TEST;
		break;
	default:
		/* Other states don't alter LED behavior */
		break;
	}

	return new_state;
}

__overridable enum led_states board_led_get_state(enum led_states desired_state)
{
	return desired_state;
}

static void led_update_battery(void)
{
	static uint8_t ticks, period;
	static int led_state = LED_NUM_STATES;
	int phase;
	enum led_states desired_state = led_get_state();

	desired_state = board_led_get_state(desired_state);

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
			"turning off LED",
			led_state);
		led_set_color_battery(LED_OFF);
		return;
	}

	/*
	 * Determine which phase of the state table to use. The phase is
	 * determined if it falls within first phase time duration.
	 */
	phase = ticks < led_bat_state_table[led_state][LED_PHASE_0].time ? 0 :
									   1;
	ticks = (ticks + 1) % period;

	/* Set the color for the given state and phase */
	led_set_color_battery(led_bat_state_table[led_state][phase].color);
}

/*
 * In order to support the power LED being optional, set up default power LED
 * table and setter
 */
__overridable const struct led_descriptor
	led_pwr_state_table[PWR_LED_NUM_STATES][LED_NUM_PHASES];
__overridable void led_set_color_power(enum ec_led_colors color)
{
}

static enum pwr_led_states pwr_led_get_state(void)
{
	if (chipset_in_state(CHIPSET_STATE_ANY_SUSPEND)) {
		if (extpower_is_present())
			return PWR_LED_STATE_SUSPEND_AC;
		else
			return PWR_LED_STATE_SUSPEND_NO_AC;
	} else if (chipset_in_state(CHIPSET_STATE_ANY_OFF)) {
		if (system_can_boot_ap())
			return PWR_LED_STATE_OFF;
		else
			return PWR_LED_STATE_OFF_LOW_POWER;
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
		/*
		 * Allow optional OFF_LOW_POWER state to fall back to
		 * OFF not defined, as indicated by no specified phase 0 time.
		 */
		if (desired_state == PWR_LED_STATE_OFF_LOW_POWER &&
		    led_pwr_state_table[desired_state][LED_PHASE_0].time == 0)
			desired_state = PWR_LED_STATE_OFF;

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
			"turning off LED",
			led_state);
		led_set_color_power(LED_OFF);
		return;
	}

	/*
	 * Determine which phase of the state table to use. The phase is
	 * determined if it falls within first phase time duration.
	 */
	phase = ticks < led_pwr_state_table[led_state][LED_PHASE_0].time ? 0 :
									   1;
	ticks = (ticks + 1) % period;

	/* Set the color for the given state and phase */
	led_set_color_power(led_pwr_state_table[led_state][phase].color);
}

static void led_init(void)
{
	/* If battery LED is enabled, set it to "off" to start with */
	if (led_auto_control_is_enabled(EC_LED_ID_BATTERY_LED))
		led_set_color_battery(LED_OFF);

	/* If power LED is enabled, set it to "off" to start with */
	if (led_auto_control_is_enabled(EC_LED_ID_POWER_LED))
		led_set_color_power(LED_OFF);
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
	if (led_auto_control_is_enabled(EC_LED_ID_POWER_LED))
		led_update_power();
}
DECLARE_HOOK(HOOK_TICK, led_update, HOOK_PRIO_DEFAULT);
