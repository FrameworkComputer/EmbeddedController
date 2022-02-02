/* Copyright 2022 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Power and battery LED control.
 */

#include "battery.h"
#include "charge_manager.h"
#include "charge_state.h"
#include "chipset.h"
#include "ec_commands.h"
#include "gpio.h"
#include "hooks.h"
#include "host_command.h"
#include "led_common.h"
#include "power.h"
#include "system.h"
#include "util.h"

#include <devicetree.h>
#include <logging/log.h>
LOG_MODULE_REGISTER(gpio_led, LOG_LEVEL_ERR);

#define LED_ONE_SEC (1000 / HOOK_TICK_INTERVAL_MS)

#define BAT_LED_ON 1
#define BAT_LED_OFF 0

#define GPIO_LED_NODE    DT_PATH(gpio_led, gpio_led_colors)

const enum ec_led_id supported_led_ids[] = {
	EC_LED_ID_BATTERY_LED,
};

const int supported_led_ids_count = ARRAY_SIZE(supported_led_ids);

enum led_color {
	LED_OFF = 0,
	LED_AMBER,
	LED_BLUE,
	LED_COLOR_COUNT  /* Number of colors, not a color itself */
};

static void led_set_color(enum led_color color)
{
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_chg_led_y_c1),
		(color == LED_AMBER) ? BAT_LED_ON : BAT_LED_OFF);
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_ec_chg_led_b_c1),
		(color == LED_BLUE) ? BAT_LED_ON : BAT_LED_OFF);
}

void led_get_brightness_range(enum ec_led_id led_id, uint8_t *brightness_range)
{
	brightness_range[EC_LED_COLOR_AMBER] = 1;
	brightness_range[EC_LED_COLOR_BLUE] = 1;
}

int led_set_brightness(enum ec_led_id led_id, const uint8_t *brightness)
{
	if (brightness[EC_LED_COLOR_BLUE] != 0)
		led_set_color(LED_BLUE);
	else if (brightness[EC_LED_COLOR_AMBER] != 0)
		led_set_color(LED_AMBER);
	else
		led_set_color(LED_OFF);

	return EC_SUCCESS;
}

struct led_color_node_t {
	int led_color;
	int acc_period;
};

enum led_extra_flag_t {
	NONE = 0,
	LED_CHFLAG_FORCE_IDLE,
	LED_CHFLAG_DEFAULT,
	LED_BATT_BELOW_10_PCT,
	LED_BATT_ABOVE_10_PCT,
};

/*
 * Currently 4 different colors are supported for blinking LED, each of which
 * can have different periods. Each period slot is the accumulation of previous
 * periods as described below. Last slot is the total accumulation which is
 * used as a dividing factor to calculate ticks to switch color
 * Eg LED_COLOR_1 1 sec, LED_COLOR_2 2 sec, LED_COLOR_3 3 sec, LED_COLOR_4 3 sec
 * period_1 = 1, period_2 = 1 + 2, period_3 = 1 + 2 + 3, period_4 =1 + 2 + 3 + 3
 * ticks -> 0, 1, 2, 3, 4, 5, 6, 7, 8, 0, 1, 2 and so on (ticks % 9)
 * 0 < period_1 -> LED_COLOR_1 for 1 sec
 * 1, 2 < period_2 -> LED_COLOR_2 for 2 secs
 * 3, 4, 5 < period_3 -> LED_COLOR_3 for 3 secs
 * 6, 7, 8 < period_4 -> LED_COLOR_4 for 3 secs
 */
#define MAX_COLOR	4

struct node_prop_t {
	enum charge_state pwr_state;
	enum power_state chipset_state;
	enum led_extra_flag_t led_extra_flag;
	struct led_color_node_t led_colors[MAX_COLOR];
};

/*
 * acc_period is the accumulated period value of all color-x children
 * led_colors[0].acc_period = period value of color-0 node
 * led_colors[1].acc_period = period value of color-0 + color-1 nodes
 * led_colors[2].acc_period = period value of color-0 + color-1 + color-2 nodes
 * and so on. If period prop or color node doesn't exist, period val is 0
 */

#define PERIOD_VAL(id) COND_CODE_1(DT_NODE_HAS_PROP(id, period),	\
				   (DT_PROP(id, period)),		\
				   (0))

#define LED_PERIOD(color_num, state_id)					\
	PERIOD_VAL(DT_CHILD(state_id, color_##color_num))

#define LED_PLUS_PERIOD(color_num, state_id)				\
	+ LED_PERIOD(color_num, state_id)

#define ACC_PERIOD(color_num, state_id)					\
	(0 UTIL_LISTIFY(color_num, LED_PLUS_PERIOD, state_id))

#define GET_PROP(id, prop)						\
	COND_CODE_1(DT_NODE_HAS_PROP(id, prop),				\
		    (DT_STRING_UPPER_TOKEN(id, prop)),			\
		    (0))

#define LED_COLOR_INIT(color_num, color_num_plus_one, state_id)		\
{									\
	.led_color = GET_PROP(DT_CHILD(state_id, color_##color_num),	\
							led_color),	\
	.acc_period = ACC_PERIOD(color_num_plus_one, state_id)		\
}

/* Initialize node_array struct with prop listed in dts */
#define SET_LED_VALUES(state_id)					\
{									\
	.pwr_state = GET_PROP(state_id, charge_state),			\
	.chipset_state = GET_PROP(state_id, chipset_state),		\
	.led_extra_flag = GET_PROP(state_id, extra_flag),		\
	.led_colors = {LED_COLOR_INIT(0, 1, state_id),			\
		       LED_COLOR_INIT(1, 2, state_id),			\
		       LED_COLOR_INIT(2, 3, state_id),			\
		       LED_COLOR_INIT(3, 4, state_id),			\
		      }							\
},

struct node_prop_t node_array[] = {
	DT_FOREACH_CHILD(GPIO_LED_NODE, SET_LED_VALUES)
};

static enum power_state get_chipset_state(void)
{
	enum power_state chipset_state = 0;

	/*
	 * Only covers subset of power states as other states don't
	 * alter LED behavior
	 */
	if (chipset_in_state(CHIPSET_STATE_ON))
		/* S0 */
		chipset_state = POWER_S0;
	else if (chipset_in_state(CHIPSET_STATE_ANY_SUSPEND))
		/* S3 */
		chipset_state = POWER_S3;
	else if (chipset_in_state(CHIPSET_STATE_ANY_OFF))
		/* S5 */
		chipset_state = POWER_S5;

	return chipset_state;
}

static bool find_node_with_extra_flag(int i)
{
	uint32_t chflags = charge_get_flags();
	bool found_node = false;

	switch (node_array[i].led_extra_flag) {
	case LED_CHFLAG_FORCE_IDLE:
	case LED_CHFLAG_DEFAULT:
		if (chflags & CHARGE_FLAG_FORCE_IDLE) {
			if (node_array[i].led_extra_flag ==
					LED_CHFLAG_FORCE_IDLE)
				found_node = true;
		} else {
			if (node_array[i].led_extra_flag == LED_CHFLAG_DEFAULT)
				found_node = true;
		}
		break;
	case LED_BATT_BELOW_10_PCT:
	case LED_BATT_ABOVE_10_PCT:
		if (charge_get_percent() < 10) {
			if (node_array[i].led_extra_flag ==
					LED_BATT_BELOW_10_PCT)
				found_node = true;
		} else {
			if (node_array[i].led_extra_flag !=
					LED_BATT_ABOVE_10_PCT)
				found_node = true;
		}
		break;
	default:
		LOG_ERR("Invalid led extra flag %d",
				node_array[i].led_extra_flag);
		break;
	}

	return found_node;
}

static int find_node(void)
{
	int i = 0;

	for (i = 0; i < ARRAY_SIZE(node_array); i++) {
		/* Check if this node depends on power state */
		if (node_array[i].pwr_state != PWR_STATE_UNCHANGE) {
			enum charge_state pwr_state = charge_get_state();

			if (node_array[i].pwr_state != pwr_state)
				continue;
		}

		/* Check if this node depends on chipset state */
		if (node_array[i].chipset_state != 0) {
			enum power_state chipset_state =
						get_chipset_state();

			/* Continue at current index as nodes are in sequence */
			if (node_array[i].chipset_state != chipset_state)
				continue;
		}

		/* Check if the node depends on any special flags */
		if (node_array[i].led_extra_flag != NONE)
			if (!find_node_with_extra_flag(i))
				continue;

		/* We found the node */
		return i;
	}

	/*
	 * Didn't find a valid node that matches all the properties
	 * Return -1 to signify error
	 */
	return -1;
}

#define GET_PERIOD(n_idx, c_idx)  node_array[n_idx].led_colors[c_idx].acc_period
#define GET_COLOR(n_idx, c_idx)   node_array[n_idx].led_colors[c_idx].led_color

static int find_color(int node_idx, int ticks)
{
	int color_idx = 0;

	/* If period value at index 0 is not 0, it's a blinking LED */
	if (GET_PERIOD(node_idx, 0) != 0) {
		/*  Period is accumulated at the last index */
		ticks = ticks % GET_PERIOD(node_idx, MAX_COLOR - 1);

		for (color_idx = 0; color_idx < MAX_COLOR; color_idx++) {
			if (GET_PERIOD(node_idx, color_idx) < ticks)
				break;
		}
	}

	return GET_COLOR(node_idx, color_idx);
}

static void board_led_set_color(void)
{
	int color = LED_OFF;
	int node = 0;
	static int ticks;

	ticks++;

	node = find_node();

	if (node < 0)
		LOG_ERR("Invalid node id, node with matching prop not found");
	else
		color = find_color(node, ticks);

	led_set_color(color);
}

/* Called by hook task every TICK */
static void led_tick(void)
{
	if (led_auto_control_is_enabled(EC_LED_ID_BATTERY_LED))
		board_led_set_color();
}
DECLARE_HOOK(HOOK_TICK, led_tick, HOOK_PRIO_DEFAULT);

void led_control(enum ec_led_id led_id, enum ec_led_state state)
{
	enum led_color color;

	if ((led_id != EC_LED_ID_RECOVERY_HW_REINIT_LED) &&
	    (led_id != EC_LED_ID_SYSRQ_DEBUG_LED))
		return;

	if (state == LED_STATE_RESET) {
		led_auto_control(EC_LED_ID_BATTERY_LED, 1);
		board_led_set_color();
		return;
	}

	color = state ? LED_BLUE : LED_OFF;

	led_auto_control(EC_LED_ID_BATTERY_LED, 0);

	led_set_color(color);
}
