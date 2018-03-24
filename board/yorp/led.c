/* Copyright 2018 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Power and battery LED control for Yorp
 */

#include "battery.h"
#include "charge_state.h"
#include "chipset.h"
#include "ec_commands.h"
#include "extpower.h"
#include "gpio.h"
#include "hooks.h"
#include "led_common.h"
#include "system.h"
#include "util.h"

#define LED_OFF_LVL	1
#define LED_ON_LVL	0
#define LED_INDEFINITE	UINT8_MAX
#define LED_ONE_SEC	(1000 / HOOK_TICK_INTERVAL_MS)
#define STATE_DEFAULT	LED_NUM_STATES

enum led_phase {
	LED_PHASE_0,
	LED_PHASE_1,
	LED_NUM_PHASES
};

enum led_color {
	LED_OFF = 0,
	LED_COLOR_BLUE,
	LED_COLOR_AMBER,
	LED_COLOR_COUNT  /* Max colors */
};

enum led_states {
	STATE_CHARGING,
	STATE_CHARGING_FULLY_CHARGED,
	STATE_DISCHARGE_S0,
	STATE_DISCHARGE_S3,
	STATE_DISCHARGE_S5,
	STATE_BATTERY_ERROR,
	LED_NUM_STATES
};

struct led_descriptor {
	int8_t color;
	uint8_t time;
};

/* Yorp: Note there is only LED for charge / power */
static const struct led_descriptor
			led_state_table[LED_NUM_STATES][LED_NUM_PHASES] = {
	[STATE_CHARGING] = { {LED_COLOR_AMBER, LED_INDEFINITE} },
	[STATE_CHARGING_FULLY_CHARGED] = { {LED_COLOR_BLUE, LED_INDEFINITE} },
	[STATE_DISCHARGE_S0] = { {LED_COLOR_BLUE, LED_INDEFINITE} },
	[STATE_DISCHARGE_S3] = { {LED_COLOR_AMBER, 4 * LED_ONE_SEC},
					{LED_OFF, 1 * LED_ONE_SEC} },
	[STATE_DISCHARGE_S5] = { {LED_OFF, LED_INDEFINITE } },
	[STATE_BATTERY_ERROR] = { {LED_COLOR_BLUE, 2 * LED_ONE_SEC},
					{LED_COLOR_AMBER, 2 * LED_ONE_SEC} },
};

const enum ec_led_id supported_led_ids[] = { EC_LED_ID_BATTERY_LED };

const int supported_led_ids_count = ARRAY_SIZE(supported_led_ids);

static void led_set_color_battery(enum led_color color)
{
	switch (color) {
	case LED_COLOR_BLUE:
		gpio_set_level(GPIO_BAT_LED_BLUE_L, LED_ON_LVL);
		gpio_set_level(GPIO_BAT_LED_ORANGE_L, LED_OFF_LVL);
		break;
	case LED_COLOR_AMBER:
		gpio_set_level(GPIO_BAT_LED_BLUE_L, LED_OFF_LVL);
		gpio_set_level(GPIO_BAT_LED_ORANGE_L, LED_ON_LVL);
		break;
	default: /* LED_OFF and other unsupported colors */
		gpio_set_level(GPIO_BAT_LED_BLUE_L, LED_OFF_LVL);
		gpio_set_level(GPIO_BAT_LED_ORANGE_L, LED_OFF_LVL);
		break;
	}
}

void led_get_brightness_range(enum ec_led_id led_id, uint8_t *brightness_range)
{
	brightness_range[EC_LED_COLOR_BLUE] = 1;
	brightness_range[EC_LED_COLOR_AMBER] = 1;
}

int led_set_brightness(enum ec_led_id led_id, const uint8_t *brightness)
{
	if (brightness[EC_LED_COLOR_BLUE] != 0)
		led_set_color_battery(LED_COLOR_BLUE);
	else if (brightness[EC_LED_COLOR_AMBER] != 0)
		led_set_color_battery(LED_COLOR_AMBER);
	else
		led_set_color_battery(LED_OFF);

	return EC_SUCCESS;
}

static enum led_states led_get_state(void)
{
	enum led_states new_state = LED_NUM_STATES;

	switch (charge_get_state()) {
	case PWR_STATE_CHARGE:
		new_state = STATE_CHARGING;
		break;
	case PWR_STATE_DISCHARGE_FULL:
		if (extpower_is_present()) {
			new_state = STATE_CHARGING_FULLY_CHARGED;
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
		new_state = STATE_CHARGING_FULLY_CHARGED;
		break;
	case PWR_STATE_IDLE: /* External power connected in IDLE */
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

	/* Get updated state based on power state and charge level */
	if (desired_state != led_state) {
		/* State is changing */
		led_state = desired_state;
		/* Reset ticks and period when state changes */
		ticks = 0;
		period = led_state_table[led_state][LED_PHASE_0].time +
			led_state_table[led_state][LED_PHASE_1].time;
	} else if (period == UINT8_MAX)
		/*
		 * No change of state so no LED change needed
		 * for indefinite phase 0.
		 */
		return;

	if (led_state == STATE_DEFAULT) /* No change needed */
		return;

	/*
	 * Determine which phase of the state table to use. The phase is
	 * determined if it falls within first phase time duration.
	 */
	phase = ticks < led_state_table[led_state][LED_PHASE_0].time ? 0 : 1;
	ticks = (ticks + 1) % period;

	/* Set the color for the given state and phase */
	led_set_color_battery(led_state_table[led_state][phase].color);
}

static void led_init(void)
{
	led_set_color_battery(LED_OFF);
}
DECLARE_HOOK(HOOK_INIT, led_init, HOOK_PRIO_DEFAULT);

/* Called by hook task every hook tick (200 msec) */
static void led_update(void)
{
	/*
	 * Yorp only has one LED, so overload it to act as both
	 * power LED and battery LED.
	 */
	if (led_auto_control_is_enabled(EC_LED_ID_BATTERY_LED))
		led_update_battery();
}
DECLARE_HOOK(HOOK_TICK, led_update, HOOK_PRIO_DEFAULT);
