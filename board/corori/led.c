/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Power and battery LED control for lalala
 */

#include "battery.h"
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

#define LED_ON_LVL 0
#define LED_OFF_LVL 1
#define LED_INDEFINITE	UINT8_MAX
#define LED_ONE_SEC	(1000 / HOOK_TICK_INTERVAL_MS)
#define LED_OFF         EC_LED_COLOR_COUNT

struct led_descriptor {
	enum ec_led_colors color;
	uint8_t time;
};

enum led_phase {
	LED_PHASE_0,
	LED_PHASE_1,
	LED_NUM_PHASES
};

enum led_states {
	STATE_CHARGING,
	STATE_CHARGING_FULL_CHARGE,
	STATE_DISCHARGE_S0,
	STATE_DISCHARGE_S0_BAT_LOW,
	STATE_BATTERY_S0_ERROR,
	STATE_BATTERY_S3_BLINK,
	STATE_BATTERY_S5_OFF,
	STATE_FACTORY_TEST,
	LED_NUM_STATES
};

static const struct led_descriptor
			led_bat_state_table[LED_NUM_STATES][LED_NUM_PHASES] = {
	[STATE_CHARGING]	     = {{EC_LED_COLOR_AMBER, LED_INDEFINITE} },
	[STATE_CHARGING_FULL_CHARGE] = {{EC_LED_COLOR_WHITE, LED_INDEFINITE} },
	[STATE_DISCHARGE_S0]	     = {{EC_LED_COLOR_WHITE, LED_INDEFINITE} },
	[STATE_DISCHARGE_S0_BAT_LOW] = {{EC_LED_COLOR_AMBER, 1 * LED_ONE_SEC},
					{LED_OFF,	     3 * LED_ONE_SEC} },
	[STATE_BATTERY_S0_ERROR]     = {{EC_LED_COLOR_AMBER,  1 * LED_ONE_SEC},
					{LED_OFF,	    1 * LED_ONE_SEC} },
	[STATE_BATTERY_S3_BLINK]     = {{EC_LED_COLOR_WHITE,  1 * LED_ONE_SEC},
					{LED_OFF,	    3 * LED_ONE_SEC} },
	[STATE_BATTERY_S5_OFF]	     = {{LED_OFF,  LED_INDEFINITE} },
	[STATE_FACTORY_TEST]	     = {{EC_LED_COLOR_WHITE,   2 * LED_ONE_SEC},
					{EC_LED_COLOR_AMBER, 2 * LED_ONE_SEC} },
};

const enum ec_led_id supported_led_ids[] = {
	EC_LED_ID_BATTERY_LED
};

const int supported_led_ids_count = ARRAY_SIZE(supported_led_ids);


static int led_get_charge_percent(void)
{
	return DIV_ROUND_NEAREST(charge_get_display_charge(), 10);
}

void led_set_color_battery(enum ec_led_colors color)
{
	switch (color) {
	case EC_LED_COLOR_AMBER:
		gpio_set_level(GPIO_BAT_LED_AMBER, LED_ON_LVL);
		gpio_set_level(GPIO_PWR_LED_WHITE, LED_OFF_LVL);
		break;
	case EC_LED_COLOR_WHITE:
		gpio_set_level(GPIO_BAT_LED_AMBER, LED_OFF_LVL);
		gpio_set_level(GPIO_PWR_LED_WHITE, LED_ON_LVL);
		break;
	default: /* LED_OFF and other unsupported colors */
		gpio_set_level(GPIO_BAT_LED_AMBER, LED_OFF_LVL);
		gpio_set_level(GPIO_PWR_LED_WHITE, LED_OFF_LVL);
		break;
	}
}

void led_get_brightness_range(enum ec_led_id led_id, uint8_t *brightness_range)
{
	if (led_id == EC_LED_ID_BATTERY_LED) {
		brightness_range[EC_LED_COLOR_AMBER] = 1;
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
	}
	return EC_SUCCESS;
}

/* Custom led on off states control */
static enum led_states led_get_state(void)
{
	enum led_states new_state = LED_NUM_STATES;

	switch (charge_get_state()) {
	case PWR_STATE_CHARGE:
		new_state = STATE_CHARGING;
		break;
	case PWR_STATE_DISCHARGE_FULL:
		if (extpower_is_present()) {
			new_state = STATE_CHARGING_FULL_CHARGE;
			break;
		}
		/* Intentional fall-through */
	case PWR_STATE_DISCHARGE /* and PWR_STATE_DISCHARGE_FULL */:
		if (chipset_in_state(CHIPSET_STATE_ON))
			new_state = (led_get_charge_percent() < 10) ?
				STATE_DISCHARGE_S0_BAT_LOW : STATE_DISCHARGE_S0;
		else if (chipset_in_state(CHIPSET_STATE_ANY_SUSPEND))
			new_state = STATE_BATTERY_S3_BLINK;
		else
			new_state = STATE_BATTERY_S5_OFF;
		break;
	case PWR_STATE_ERROR:
		if (chipset_in_state(CHIPSET_STATE_ON))
			new_state = STATE_BATTERY_S0_ERROR;
		else if (chipset_in_state(CHIPSET_STATE_ANY_SUSPEND))
			new_state = STATE_BATTERY_S3_BLINK;
		else
			new_state = STATE_BATTERY_S5_OFF;
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
		/* State is changing */
		led_state = desired_state;
		/* Reset ticks and period when state changes */
		ticks = 0;

		period = led_bat_state_table[led_state][LED_PHASE_0].time +
			led_bat_state_table[led_state][LED_PHASE_1].time;

	}

	/* If this state is undefined, turn the LED off */
	if (period == 0) {
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
}
DECLARE_HOOK(HOOK_TICK, led_update, HOOK_PRIO_DEFAULT);
