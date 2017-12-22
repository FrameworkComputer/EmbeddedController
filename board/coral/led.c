/* Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Power and battery LED control for Coral
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
#define LED_INDEFINITE -1
#define LED_ONE_SEC (1000 / HOOK_TICK_INTERVAL_MS)
#define LED_CHARGE_LEVEL_1_DEFAULT 100
#define LED_CHARGE_LEVEL_1_ROBO 5
#define LED_POWER_BLINK_ON_MSEC 3000
#define LED_POWER_BLINK_OFF_MSEC 600
#define LED_POWER_ON_TICKS (LED_POWER_BLINK_ON_MSEC / HOOK_TICK_INTERVAL_MS)
#define LED_POWER_OFF_TICKS (LED_POWER_BLINK_OFF_MSEC / HOOK_TICK_INTERVAL_MS)

const enum ec_led_id supported_led_ids[] = {
			EC_LED_ID_BATTERY_LED};

const int supported_led_ids_count = ARRAY_SIZE(supported_led_ids);

#define GPIO_LED_COLOR_1 GPIO_BAT_LED_AMBER
#define GPIO_LED_COLOR_2 GPIO_BAT_LED_BLUE
#define GPIO_LED_COLOR_3 GPIO_POW_LED

enum led_phase {
	LED_PHASE_0,
	LED_PHASE_1,
	LED_NUM_PHASES
};

enum led_color {
	LED_OFF,
	LED_COLOR_1,
	LED_COLOR_2,
	LED_COLOR_BOTH,
	LED_COLOR_COUNT  /* Number of colors, not a color itself */
};

enum led_states {
	STATE_CHARGING_LVL_1,
	STATE_CHARGING_LVL_2,
	STATE_CHARGING_LVL_3,
	STATE_DISCHARGE_S0,
	STATE_DISCHARGE_S3,
	STATE_DISCHARGE_S5,
	STATE_BATTERY_ERROR,
	STATE_FACTORY_TEST,
	LED_NUM_STATES
};

struct led_descriptor {
	int8_t color;
	int8_t time;
};

struct led_info {
	enum led_states state;
	uint8_t charge_lvl_1;
	const struct led_descriptor (*state_table)[LED_NUM_PHASES];
	void (*update_power)(void);
};

/*
 * LED state tables describe the desired LED behavior for a each possible
 * state. The LED state is based on both chip power state and the battery charge
 * level. The first parameter is the color and the 2nd parameter is the time in
 * ticks, where each tick is 200 msec. If the time parameter is set to -1, that
 * means it is a non-blinking pattern.
 */

/* COLOR_1 = Amber, COLOR_2 = Blue */
static const struct led_descriptor led_default_state_table[][LED_NUM_PHASES] = {
	{ {LED_COLOR_1, LED_INDEFINITE}, {LED_OFF, LED_INDEFINITE} },
	{ {LED_COLOR_2, LED_INDEFINITE}, {LED_COLOR_1, LED_INDEFINITE} },
	{ {LED_COLOR_2, LED_INDEFINITE}, {LED_OFF, LED_INDEFINITE} },
	{ {LED_COLOR_2, LED_INDEFINITE}, {LED_OFF, LED_INDEFINITE} },
	{ {LED_COLOR_1, 1 * LED_ONE_SEC }, {LED_OFF, 3 * LED_ONE_SEC} },
	{ {LED_OFF, LED_INDEFINITE}, {LED_OFF, LED_INDEFINITE} },
	{ {LED_COLOR_1, 1 * LED_ONE_SEC}, {LED_OFF, 1 * LED_ONE_SEC} },
	{ {LED_COLOR_1, 2 * LED_ONE_SEC}, {LED_COLOR_2, 2 * LED_ONE_SEC} },
};

/* COLOR_1 = Green, COLOR_2 = Red */
static const struct led_descriptor led_robo_state_table[][LED_NUM_PHASES] = {
	{ {LED_COLOR_2, LED_INDEFINITE}, {LED_OFF, LED_INDEFINITE} },
	{ {LED_COLOR_BOTH, LED_INDEFINITE}, {LED_OFF, LED_INDEFINITE} },
	{ {LED_COLOR_1, LED_INDEFINITE}, {LED_OFF, LED_INDEFINITE} },
	{ {LED_OFF, LED_INDEFINITE}, {LED_OFF, LED_INDEFINITE} },
	{ {LED_OFF, LED_INDEFINITE}, {LED_OFF, LED_INDEFINITE} },
	{ {LED_OFF, LED_INDEFINITE}, {LED_OFF, LED_INDEFINITE} },
	{ {LED_COLOR_2, 1 * LED_ONE_SEC}, {LED_OFF, 1 * LED_ONE_SEC} },
	{ {LED_COLOR_2, 2 * LED_ONE_SEC}, {LED_COLOR_1, 2 * LED_ONE_SEC} },
};

static const struct led_descriptor led_nasher_state_table[][LED_NUM_PHASES] = {
	{ {LED_COLOR_1, LED_INDEFINITE}, {LED_OFF, LED_INDEFINITE} },
	{ {LED_COLOR_2, LED_INDEFINITE}, {LED_COLOR_1, LED_INDEFINITE} },
	{ {LED_COLOR_2, LED_INDEFINITE}, {LED_OFF, LED_INDEFINITE} },
	{ {LED_COLOR_2, LED_INDEFINITE}, {LED_OFF, LED_INDEFINITE} },
	{ {LED_COLOR_2, 1 * LED_ONE_SEC}, {LED_OFF, 1 * LED_ONE_SEC} },
	{ {LED_OFF, LED_INDEFINITE}, {LED_OFF, LED_INDEFINITE} },
	{ {LED_COLOR_1, 1 * LED_ONE_SEC}, {LED_OFF, 1 * LED_ONE_SEC} },
	{ {LED_COLOR_1, 2 * LED_ONE_SEC}, {LED_COLOR_2, 2 * LED_ONE_SEC} },
};

static struct led_info led;

static int led_set_color_battery(enum led_color color)
{
	switch (color) {
	case LED_OFF:
		gpio_set_level(GPIO_LED_COLOR_1, LED_OFF_LVL);
		gpio_set_level(GPIO_LED_COLOR_2, LED_OFF_LVL);
		break;
	case LED_COLOR_1:
		gpio_set_level(GPIO_LED_COLOR_1, LED_ON_LVL);
		gpio_set_level(GPIO_LED_COLOR_2, LED_OFF_LVL);
		break;
	case LED_COLOR_2:
		gpio_set_level(GPIO_LED_COLOR_1, LED_OFF_LVL);
		gpio_set_level(GPIO_LED_COLOR_2, LED_ON_LVL);
		break;
	case LED_COLOR_BOTH:
		gpio_set_level(GPIO_LED_COLOR_1, LED_ON_LVL);
		gpio_set_level(GPIO_LED_COLOR_2, LED_ON_LVL);
		break;
	default:
		return EC_ERROR_UNKNOWN;
	}
	return EC_SUCCESS;
}

static void led_set_color_power(int level)
{
	gpio_set_level(GPIO_POWER_LED, level);
}

void led_get_brightness_range(enum ec_led_id led_id, uint8_t *brightness_range)
{
	brightness_range[EC_LED_COLOR_BLUE] = 1;
	brightness_range[EC_LED_COLOR_AMBER] = 1;
	brightness_range[EC_LED_COLOR_RED] = 1;
	brightness_range[EC_LED_COLOR_GREEN] = 1;
}

int led_set_brightness(enum ec_led_id led_id, const uint8_t *brightness)
{
	if (brightness[EC_LED_COLOR_BLUE] != 0)
		led_set_color_battery(LED_COLOR_2);
	else if (brightness[EC_LED_COLOR_AMBER] != 0)
		led_set_color_battery(LED_COLOR_1);
	else if (brightness[EC_LED_COLOR_RED] != 0)
		led_set_color_battery(LED_COLOR_2);
	else if (brightness[EC_LED_COLOR_GREEN] != 0)
		led_set_color_battery(LED_COLOR_1);
	else
		led_set_color_battery(LED_OFF);

	return EC_SUCCESS;
}

static enum led_states led_get_state(void)
{
	int  charge_lvl;
	enum led_states new_state = LED_NUM_STATES;

	switch (charge_get_state()) {
	case PWR_STATE_CHARGE:
		/* Get percent charge */
		charge_lvl = charge_get_percent();
		/* Determine which charge state to use */
		new_state = charge_lvl <= led.charge_lvl_1 ?
			STATE_CHARGING_LVL_1 : STATE_CHARGING_LVL_2;
		break;
	case PWR_STATE_DISCHARGE_FULL:
		if (extpower_is_present()) {
			new_state = STATE_CHARGING_LVL_3;
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
		new_state = STATE_CHARGING_LVL_3;
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
	static int ticks;
	int phase;
	enum led_states desired_state = led_get_state();

	/* Get updated state based on power state and charge level */
	if (desired_state < LED_NUM_STATES && desired_state != led.state) {
		/* State is changing */
		led.state = desired_state;
		/* Reset ticks counter when state changes */
		ticks = 0;
	}

	/*
	 * Determine the which phase of the state table to use. Assume it's
	 * phase 0. If the time values for both phases of the current state are
	 * not -1, then this state uses some blinking pattern. The phase is then
	 * determined by taking the modulo of ticks by the blinking pattern
	 * period.
	 */
	phase = 0;
	if ((led.state_table[led.state][LED_PHASE_0].time != LED_INDEFINITE) &&
	    (led.state_table[led.state][LED_PHASE_1].time != LED_INDEFINITE)) {
		int period;

		period = led.state_table[led.state][LED_PHASE_0].time +
			led.state_table[led.state][LED_PHASE_1].time;
		if (period)
			phase = ticks % period <
				led.state_table[led.state][LED_PHASE_0].time ?
				0 : 1;
	}

	/* Set the color for the given state and phase */
	led_set_color_battery(led.state_table[led.state][phase].color);
	ticks++;
}

static void led_robo_update_power(void)
{
	int level;
	static int ticks;

	if (chipset_in_state(CHIPSET_STATE_ON)) {
		/* In S0 power LED is always on */
		level = LED_ON_LVL;
		ticks = 0;
	} else if (chipset_in_state(CHIPSET_STATE_ANY_SUSPEND) &&
		   led.state <= STATE_CHARGING_LVL_3) {
		int period;

		/*
		 * If in suspend/standby and the device is charging, then the
		 * power LED is off for 600 msec, on for 3 seconds.
		 */
		period = LED_POWER_ON_TICKS + LED_POWER_OFF_TICKS;
		level = ticks % period < LED_POWER_OFF_TICKS ?
			LED_OFF_LVL : LED_ON_LVL;
		ticks++;
	} else {
		level = LED_OFF_LVL;
		ticks = 0;
	}

	led_set_color_power(level);
}

/* Called by hook task every hook tick (200 msec) */
static void led_update(void)
{
	/* Update battery LED */
	if (led_auto_control_is_enabled(EC_LED_ID_BATTERY_LED)) {
		led_update_battery();
		if (led.update_power != NULL)
			(*led.update_power)();
	}
}
DECLARE_HOOK(HOOK_TICK, led_update, HOOK_PRIO_DEFAULT);

static void led_init(void)
{
	int sku = system_get_sku_id();

	if ((sku >= 70 && sku <= 79) || (sku >= 124 && sku <= 125) ||
	    (sku >= 144 && sku <= 145)) {
		led.charge_lvl_1 = LED_CHARGE_LEVEL_1_ROBO;
		led.state_table = led_robo_state_table;
		led.update_power = led_robo_update_power;
	} else if (sku >= 160 && sku <= 166) {
		led.charge_lvl_1 = LED_CHARGE_LEVEL_1_DEFAULT;
		led.state_table = led_nasher_state_table;
		led.update_power = NULL;
	} else {
		led.charge_lvl_1 = LED_CHARGE_LEVEL_1_DEFAULT;
		led.state_table = led_default_state_table;
		led.update_power = NULL;
	}
	led_set_color_battery(LED_OFF);
}
/* Make sure this comes after SKU ID hook */
DECLARE_HOOK(HOOK_INIT, led_init, HOOK_PRIO_DEFAULT + 2);
