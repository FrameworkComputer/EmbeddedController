/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Power and battery LED control.
 */

#define DT_DRV_COMPAT cros_ec_led_policy

#include "battery.h"
#include "charge_manager.h"
#include "charge_state.h"
#include "chipset.h"
#include "ec_commands.h"
#include "hooks.h"
#include "host_command.h"
#include "led.h"
#include "led_common.h"
#include "power.h"
#include "system.h"
#include "util.h"

#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(led, LOG_LEVEL_ERR);

BUILD_ASSERT(DT_NUM_INST_STATUS_OKAY(DT_DRV_COMPAT) == 1,
	     "Exactly one instance of cros-ec,led-policy should be defined.");

#define DECLARE_PINS_NODE(id) extern struct led_pins_node_t PINS_NODE(id);

#if CONFIG_PLATFORM_EC_LED_DT_PWM
DT_FOREACH_CHILD_STATUS_OKAY_VARGS(
	DT_COMPAT_GET_ANY_STATUS_OKAY(cros_ec_pwm_led_pins), DT_FOREACH_CHILD,
	DECLARE_PINS_NODE)
#elif CONFIG_PLATFORM_EC_LED_DT_GPIO
DT_FOREACH_CHILD_STATUS_OKAY_VARGS(
	DT_COMPAT_GET_ANY_STATUS_OKAY(cros_ec_gpio_led_pins), DT_FOREACH_CHILD,
	DECLARE_PINS_NODE)
#endif

#define PINS_NODE_FROM_POLICY(led_id, color_token) \
	DT_CAT4(PIN_NODE_, led_id, _COLOR_, color_token)

#define SET_PATTERN_COLOR_ARRAY(id)                                           \
	{                                                                     \
		.led_color_node = &PINS_NODE_FROM_POLICY(                     \
			GET_PROP(DT_PARENT(id), led_id),                      \
			GET_PROP(id, led_color)),                             \
		.duration =                                                   \
			DT_PROP_OR(id, period_ms, 0) / HOOK_TICK_INTERVAL_MS, \
	},

#define PATTERN_COLOR_ARRAY(id) DT_CAT(PATTERN_COLOR_, id)
#define GEN_PATTERN_COLOR_ARRAY(id, fn)                  \
	struct pattern_color_node_t PATTERN_COLOR_ARRAY( \
		id)[] = { fn(id, SET_PATTERN_COLOR_ARRAY) };
DT_INST_FOREACH_CHILD_STATUS_OKAY_VARGS(0, DT_FOREACH_CHILD_VARGS,
					GEN_PATTERN_COLOR_ARRAY,
					DT_FOREACH_CHILD)

#define PLUS_ONE(id) +1

#define LED_PATTERN_INIT(node_id, fn)                          \
	{                                                      \
		.cur_color = 0,                                \
		.ticks = 0,                                    \
		.transition = GET_PROP(node_id, transition),   \
		.pattern_len = 0 fn(node_id, PLUS_ONE),        \
		.pattern_color = PATTERN_COLOR_ARRAY(node_id), \
	},

struct node_prop_t {
	enum led_pwr_state pwr_state;
	enum power_state chipset_state;
	int batt_state_mask;
	int batt_state;
	int8_t batt_lvl[2];
	int8_t charge_port;
	struct led_pattern_node_t *led_patterns;
	uint8_t num_patterns;
	bool state_active;
};

#define PATTERN_NODE_ARRAY(id) DT_CAT(PATTERN_ARRAY_, id)
#define GEN_PATTERN_NODE_ARRAY(id, fn1, fn2)          \
	struct led_pattern_node_t PATTERN_NODE_ARRAY( \
		id)[] = { fn1(id, LED_PATTERN_INIT, fn2) };
DT_INST_FOREACH_CHILD_STATUS_OKAY_VARGS(0, GEN_PATTERN_NODE_ARRAY,
					DT_FOREACH_CHILD_VARGS,
					DT_FOREACH_CHILD)

/*
 * Initialize node_array struct with prop listed in dts.
 * Zephyr does not recognize nested FOREACH macros unless they are carried in
 * as a _VARGS, so DT_FOREACH_CHILD is brought in as an additional fn variable
 */
#define SET_LED_VALUES(state_id, fn)                                          \
	{                                                                     \
		.pwr_state = GET_PROP(state_id, charge_state),                \
		.chipset_state = GET_PROP(state_id, chipset_state),           \
		.batt_state_mask = COND_CODE_1(                               \
			DT_NODE_HAS_PROP(state_id, batt_state_mask),          \
			(DT_PROP(state_id, batt_state_mask)), (-1)),          \
		.batt_state =                                                 \
			COND_CODE_1(DT_NODE_HAS_PROP(state_id, batt_state),   \
				    (DT_PROP(state_id, batt_state)), (-1)),   \
		.batt_lvl = COND_CODE_1(DT_NODE_HAS_PROP(state_id, batt_lvl), \
					(DT_PROP(state_id, batt_lvl)),        \
					({ -1, -1 })),                        \
		.charge_port =                                                \
			COND_CODE_1(DT_NODE_HAS_PROP(state_id, charge_port),  \
				    (DT_PROP(state_id, charge_port)), (-1)),  \
		.led_patterns = PATTERN_NODE_ARRAY(state_id),                 \
		.num_patterns = 0 fn(state_id, PLUS_ONE),                     \
		.state_active = false,                                        \
	},

static struct node_prop_t node_array[] = {
	DT_INST_FOREACH_CHILD_STATUS_OKAY_VARGS(0, SET_LED_VALUES,
						DT_FOREACH_CHILD)
};

test_export_static enum power_state get_chipset_state(void)
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

static void set_color(int node_idx)
{
	struct led_pattern_node_t *patterns = node_array[node_idx].led_patterns;

	for (int i = 0; i < node_array[node_idx].num_patterns; i++) {
		if (!led_auto_control_is_enabled(
			    patterns[i].pattern_color[0].led_color_node->led_id))
			continue; /* Auto control is disabled */

		led_set_color_with_pattern(&patterns[i]);

		if (GET_DURATION(patterns[i], patterns[i].cur_color) != 0)
			patterns[i].ticks++;

		if (patterns[i].ticks >=
		    GET_DURATION(patterns[i], patterns[i].cur_color)) {
			patterns[i].cur_color++;
			patterns[i].ticks = 0;
		}

		if (patterns[i].cur_color >= patterns[i].pattern_len) {
			patterns[i].cur_color = 0;
		}
	}
}

static int match_node(int node_idx)
{
	/* Check if this node depends on power state */
	if (node_array[node_idx].pwr_state != LED_PWRS_UNCHANGE) {
		enum led_pwr_state pwr_state = led_pwr_get_state();

		if (node_array[node_idx].pwr_state != pwr_state) {
			node_array[node_idx].state_active = false;
			return -1;
		}

		/* Check if this node depends on charge port */
		if (node_array[node_idx].charge_port != -1) {
			int port = charge_manager_get_active_charge_port();

			if (node_array[node_idx].charge_port != port) {
				node_array[node_idx].state_active = false;
				return -1;
			}
		}
	}

	/* Check if this node depends on chipset state */
	if (node_array[node_idx].chipset_state != 0) {
		enum power_state chipset_state = get_chipset_state();

		if (node_array[node_idx].chipset_state != chipset_state) {
			node_array[node_idx].state_active = false;
			return -1;
		}
	}

	/* check if this node depends on battery status */
	if (node_array[node_idx].batt_state_mask != -1) {
		int batt_state;

		battery_status(&batt_state);
		if ((node_array[node_idx].batt_state_mask & batt_state) !=
		    (node_array[node_idx].batt_state_mask &
		     node_array[node_idx].batt_state)) {
			node_array[node_idx].state_active = false;
			return -1;
		}
	}

	/* Check if this node depends on battery level */
	if (node_array[node_idx].batt_lvl[0] != -1) {
		int curr_batt_lvl =
			DIV_ROUND_NEAREST(charge_get_display_charge(), 10);

		if ((curr_batt_lvl < node_array[node_idx].batt_lvl[0]) ||
		    (curr_batt_lvl > node_array[node_idx].batt_lvl[1])) {
			node_array[node_idx].state_active = false;
			return -1;
		}
	}

	/* reset the color counter if pattern just activated */
	if (node_array[node_idx].state_active == false) {
		node_array[node_idx].state_active = true;
		for (int i = 0; i < node_array[node_idx].num_patterns; i++) {
			node_array[node_idx].led_patterns[i].cur_color = 0;
			node_array[node_idx].led_patterns[i].ticks = 0;
		}
	}

	/* We found the node that matches the current system state */
	return node_idx;
}

static void board_led_set_color(void)
{
	bool found_node = false;

	/*
	 * Find all the nodes that match the current state of the system and
	 * set color for these nodes. Depending on the policy defined in
	 * led.dts, a node could depend on power-state, chipset-state, extra
	 * flags like battery percentage etc.
	 * We must find at least one node that indicates the LED Behavior for
	 * current system state.
	 */
	for (int i = 0; i < ARRAY_SIZE(node_array); i++) {
		if (match_node(i) != -1) {
			found_node = true;

			set_color(i);
		}
	}

	if (!found_node)
		LOG_ERR("Node with matching prop not found");
}

/* Called by hook task every HOOK_TICK_INTERVAL_MS */
static void led_tick(void)
{
	board_led_set_color();
	board_led_apply_color();
}
DECLARE_HOOK(HOOK_TICK, led_tick, HOOK_PRIO_DEFAULT);

void led_control(enum ec_led_id led_id, enum ec_led_state state)
{
	enum led_color color;

	switch (led_id) {
	case EC_LED_ID_RECOVERY_HW_REINIT_LED:
		led_id = DT_INST_STRING_TOKEN(0, recovery_hw_reinit_alias);
		color = state ? DT_INST_STRING_TOKEN(
					0,
					recovery_hw_reinit_led_control_color) :
				LED_OFF;
		break;
	case EC_LED_ID_SYSRQ_DEBUG_LED:
		led_id = DT_INST_STRING_TOKEN(0, sysrq_alias);
		color = state ? DT_INST_STRING_TOKEN(0,
						     sysrq_led_control_color) :
				LED_OFF;
		break;
	default:
		return;
	}

	if (state == LED_STATE_RESET) {
		led_auto_control(led_id, 1);
		board_led_set_color();
		return;
	}

	led_auto_control(led_id, 0);

	led_set_color(color, led_id);
}
