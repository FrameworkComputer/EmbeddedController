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
#include "drivers/led.h"
#include "ec_commands.h"
#include "hooks.h"
#include "host_command.h"
#include "led_common.h"
#include "power.h"
#include "system.h"
#include "util.h"

#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(led, LOG_LEVEL_ERR);

/* Extern the driver handles linked by 'led-pins' in the policies */
#define DECLARE_DRIVER(inst)                        \
	extern const struct led_driver_t PINS_NODE( \
		DT_INST_PHANDLE(inst, led_pins));
DT_INST_FOREACH_STATUS_OKAY(DECLARE_DRIVER)

/* Extern the led_pins_node instances for each individual color step */
#define DECLARE_PINS_NODE(id) extern const struct led_pins_node_t PINS_NODE(id);

#define DECLARE_PINS_NODE_FOR_POLICY(inst)                                  \
	DT_FOREACH_CHILD_STATUS_OKAY_VARGS(DT_INST_PHANDLE(inst, led_pins), \
					   DT_FOREACH_CHILD,                \
					   DECLARE_PINS_NODE)

DT_INST_FOREACH_STATUS_OKAY(DECLARE_PINS_NODE_FOR_POLICY)

#define ASSERT_LEDS_HW_MATCH(id)                                               \
	BUILD_ASSERT(                                                          \
		DT_SAME_NODE(DT_PHANDLE(DT_PARENT(DT_PARENT(DT_PARENT(id))),   \
					led_pins),                             \
			     DT_PARENT(DT_PARENT(DT_PHANDLE(id, led_color)))), \
		"The led-color node " #id                                      \
		" does not match the driver linked in 'led-pins'.");

#define ASSERT_LEDS_ID_MATCH(id)                                              \
	BUILD_ASSERT(                                                         \
		DT_STRING_TOKEN(DT_PARENT(id), led_id) ==                     \
			DT_STRING_TOKEN(DT_PARENT(DT_PHANDLE(id, led_color)), \
					led_id),                              \
		"The led-color node (" #id                                    \
		") must belong to the same led-id defined in the policy.");

/* Generates the step-level pattern array for each rule */
#define SET_PATTERN_COLOR_ARRAY(id)                                      \
	{                                                                \
		.led_color_node = &PINS_NODE(DT_PHANDLE(id, led_color)), \
		.duration_ms = DT_PROP_OR(id, period_ms, 0),             \
	},

#define PATTERN_COLOR_ARRAY(id) DT_CAT(PATTERN_COLOR_, id)

#define GEN_PATTERN_COLOR_ARRAY(id, fn)                        \
	const struct pattern_color_node_t PATTERN_COLOR_ARRAY( \
		id)[] = { fn(id, SET_PATTERN_COLOR_ARRAY) };   \
	fn(id, ASSERT_LEDS_HW_MATCH) fn(id, ASSERT_LEDS_ID_MATCH)

#define GEN_PATTERN_COLOR_ARRAY_FOR_POLICY(inst)                              \
	DT_INST_FOREACH_CHILD_STATUS_OKAY_VARGS(inst, DT_FOREACH_CHILD_VARGS, \
						GEN_PATTERN_COLOR_ARRAY,      \
						DT_FOREACH_CHILD)

DT_INST_FOREACH_STATUS_OKAY(GEN_PATTERN_COLOR_ARRAY_FOR_POLICY)

#define PLUS_ONE(id) +1

#define LED_PATTERN_INIT(node_id, fn)                               \
	{                                                           \
		.cur_color = 0,                                     \
		.elapsed_ms = 0,                                    \
		.transition = GET_PROP(node_id, transition),        \
		.pattern_len = 0 fn(node_id, PLUS_ONE),             \
		.pattern_color = PATTERN_COLOR_ARRAY(node_id),      \
		.cycle_limit = DT_PROP_OR(node_id, cycle_count, 0), \
		.cycle_curr = 0,                                    \
	},

#define VALIDATE_CYCLE_COUNT(node_id, ...)                       \
	BUILD_ASSERT(DT_PROP_OR(node_id, cycle_count, 0) <= 255, \
		     "cycle-count exceeds uint8_t limit (255)");

/* Generate the logic-level pattern array for each rule */
#define PATTERN_NODE_ARRAY(id) DT_CAT(PATTERN_ARRAY_, id)
#define GEN_PATTERN_NODE_ARRAY(id, fn1, fn2)                \
	struct led_pattern_node_t PATTERN_NODE_ARRAY(       \
		id)[] = { fn1(id, LED_PATTERN_INIT, fn2) }; \
	fn1(id, VALIDATE_CYCLE_COUNT)

#define GEN_PATTERN_NODE_ARRAY_FOR_POLICY(inst)                               \
	DT_INST_FOREACH_CHILD_STATUS_OKAY_VARGS(inst, GEN_PATTERN_NODE_ARRAY, \
						DT_FOREACH_CHILD_VARGS,       \
						DT_FOREACH_CHILD)

DT_INST_FOREACH_STATUS_OKAY(GEN_PATTERN_NODE_ARRAY_FOR_POLICY)

struct node_prop_t {
	enum led_pwr_state pwr_state;
	enum power_state chipset_state;
	int batt_state_mask;
	int batt_state;
	int8_t batt_lvl[2];
	int8_t charge_port;
	int8_t board_led_alt_policy_label;
	struct led_pattern_node_t *led_patterns;
	uint8_t num_patterns;
};

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
		.board_led_alt_policy_label = COND_CODE_1(                    \
			DT_NODE_HAS_PROP(state_id,                            \
					 board_led_alt_policy_label),         \
			(DT_PROP(state_id, board_led_alt_policy_label)),      \
			(-1)),                                                \
		.led_patterns = PATTERN_NODE_ARRAY(state_id),                 \
		.num_patterns = 0 fn(state_id, PLUS_ONE),                     \
	},

struct policy_group {
	const struct led_driver_t *driver;
	const struct node_prop_t *nodes;
	bool *active;
	size_t num_nodes;
};

#define LOCAL_NODE_ARRAY(inst) DT_CAT(node_array_, inst)
#define LOCAL_ACTIVE_ARRAY(inst) DT_CAT(active_array_, inst)

#define GEN_LOCAL_ARRAYS(inst)                                                \
	static const struct node_prop_t LOCAL_NODE_ARRAY(inst)[] = {          \
		DT_INST_FOREACH_CHILD_STATUS_OKAY_VARGS(inst, SET_LED_VALUES, \
							DT_FOREACH_CHILD)     \
	};                                                                    \
	static bool LOCAL_ACTIVE_ARRAY(                                       \
		inst)[ARRAY_SIZE(LOCAL_NODE_ARRAY(inst))];
DT_INST_FOREACH_STATUS_OKAY(GEN_LOCAL_ARRAYS)

#define INIT_POLICY_GROUP(inst)                                        \
	{                                                              \
		.driver = &PINS_NODE(DT_INST_PHANDLE(inst, led_pins)), \
		.nodes = LOCAL_NODE_ARRAY(inst),                       \
		.active = LOCAL_ACTIVE_ARRAY(inst),                    \
		.num_nodes = ARRAY_SIZE(LOCAL_NODE_ARRAY(inst)),       \
	},

static const struct policy_group policy_groups[] = {
	DT_INST_FOREACH_STATUS_OKAY(INIT_POLICY_GROUP)
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

static void advance_led_pattern(struct led_pattern_node_t *pattern,
				uint32_t increment)
{
	uint32_t duration;
	int steps = 0;

	/* If we have finished the requested number of cycles, hold state. */
	if (pattern->cycle_limit > 0 &&
	    pattern->cycle_curr >= pattern->cycle_limit) {
		return;
	}

	duration = get_step_duration(pattern, pattern->cur_color);

	pattern->elapsed_ms += increment;

	/*
	 * Process steps that have lapsed. We limit transitions to pattern
	 * length to prevent infinite loops.
	 */
	while (pattern->elapsed_ms >= duration &&
	       steps < pattern->pattern_len) {
		pattern->elapsed_ms -= duration;
		pattern->cur_color++;
		steps++;

		/* Wrap around if we reached the end of the pattern */
		if (pattern->cur_color >= pattern->pattern_len) {
			/* Handle cycle counting if a limit is configured */
			if (pattern->cycle_limit > 0) {
				pattern->cycle_curr++;
				if (pattern->cycle_curr >=
				    pattern->cycle_limit) {
					/* Limit reached. Hold final state. */
					pattern->cur_color =
						pattern->pattern_len - 1;
					pattern->elapsed_ms = get_step_duration(
						pattern, pattern->cur_color);
					return;
				}
			}

			pattern->cur_color = 0;
		}

		duration = get_step_duration(pattern, pattern->cur_color);
	}

	/* Reset time if limit hit to prevent accumulation/overflow */
	if (steps >= pattern->pattern_len) {
		pattern->elapsed_ms = 0;
	}
}

static void update_led_pattern(const struct policy_group *grp,
			       struct led_pattern_node_t *pattern)
{
	/* Check if auto control is enabled */
	if (!led_auto_control_is_enabled(
		    pattern->pattern_color[0].led_color_node->led_id)) {
		return;
	}

	/* Apply color calculated in the previous tick */
	grp->driver->api->set_color_with_pattern(pattern);

	/* Advance state machine for the next tick */
	advance_led_pattern(pattern, HOOK_TICK_INTERVAL_MS);
}

static void update_node_patterns(const struct policy_group *grp,
				 const struct node_prop_t *node)
{
	struct led_pattern_node_t *patterns = node->led_patterns;

	for (int i = 0; i < node->num_patterns; i++) {
		update_led_pattern(grp, &patterns[i]);
	}
}

/* LCOV_EXCL_START */
__overridable int board_led_alt_policy(void)
{
	/* Default no led alt policy */
	return -1;
}
/* LCOV_EXCL_STOP */

/*
 * The script zephyr/scripts/led_policy.py is used to verify that all
 * power/battery states are covered by the cros-ec,led-policy devicetree.
 * Update the python script whenever major changes are made to the matching
 * function here.
 */
static int match_node(const struct policy_group *grp, int node_idx)
{
	const struct node_prop_t *node = &grp->nodes[node_idx];
	bool *active = &grp->active[node_idx];

#if (IS_ENABLED(CONFIG_PLATFORM_EC_CHARGE_MANAGER))
	/* Check if this node depends on power state */
	if (node->pwr_state != LED_PWRS_UNCHANGE) {
		enum led_pwr_state pwr_state = led_pwr_get_state();

		if (node->pwr_state != pwr_state) {
			*active = false;
			return -1;
		}

		/* Check if this node depends on charge port */
		if (node->charge_port != -1) {
			int port = charge_manager_get_active_charge_port();

			if (node->charge_port != port) {
				*active = false;
				return -1;
			}
		}
	}
#endif /* CONFIG_PLATFORM_EC_CHARGE_MANAGER */

	/* Check if this node depends on chipset state */
	if (node->chipset_state != 0) {
		enum power_state chipset_state = get_chipset_state();

		if (node->chipset_state != chipset_state) {
			*active = false;
			return -1;
		}
	}

	/* Check if this node depends on board alt policy */
	if (node->board_led_alt_policy_label != -1) {
		if (node->board_led_alt_policy_label !=
		    board_led_alt_policy()) {
			*active = false;
			return -1;
		}
	}

#if (IS_ENABLED(CONFIG_PLATFORM_EC_BATTERY))
	/* check if this node depends on battery status */
	if (node->batt_state_mask != -1) {
		int batt_state;

		battery_status(&batt_state);
		if ((node->batt_state_mask & batt_state) !=
		    (node->batt_state_mask & node->batt_state)) {
			*active = false;
			return -1;
		}
	}
#endif /* CONFIG_PLATFORM_EC_BATTERY */

#if (IS_ENABLED(CONFIG_PLATFORM_EC_CHARGE_MANAGER))
	/* Check if this node depends on battery level */
	if (node->batt_lvl[0] != -1) {
		int curr_batt_lvl =
			DIV_ROUND_NEAREST(charge_get_display_charge(), 10);

		if ((curr_batt_lvl < node->batt_lvl[0]) ||
		    (curr_batt_lvl > node->batt_lvl[1])) {
			*active = false;
			return -1;
		}
	}
#endif /* CONFIG_PLATFORM_EC_CHARGE_MANAGER */

	/* reset the color counter if pattern just activated */
	if (!(*active)) {
		*active = true;
		for (int i = 0; i < node->num_patterns; i++) {
			struct led_pattern_node_t *pattern =
				&node->led_patterns[i];

			pattern->cur_color = 0;
			pattern->elapsed_ms = 0;
			pattern->cycle_curr = 0;
			/* Skip initial 0-duration colors before first render */
			advance_led_pattern(pattern, 0);
		}
	}

	/* We found the node that matches the current system state */
	return node_idx;
}

static bool led_set_all_colors(void)
{
	bool has_transitions = false;

	/*
	 * Find all the nodes that match the current state of the system and
	 * set color for these nodes. Depending on the policy defined in
	 * led.dts, a node could depend on power-state, chipset-state, extra
	 * flags like battery percentage etc.
	 * We must find at least one node that indicates the LED Behavior for
	 * current system state.
	 */
	for (int i = 0; i < ARRAY_SIZE(policy_groups); i++) {
		const struct policy_group *grp = &policy_groups[i];
		bool found_node = false;

		for (int j = 0; j < grp->num_nodes; j++) {
			if (match_node(grp, j) != -1) {
				found_node = true;

				// TODO: has_transitions should support all
				// non-step patterns
				if (grp->nodes[j].led_patterns->transition ==
				    LED_TRANSITION_LINEAR)
					has_transitions = true;

				update_node_patterns(grp, &grp->nodes[j]);
			}
		}
		if (!found_node) {
			LOG_ERR("Node with matching prop not found");
		}
	}

	return has_transitions;
}

void led_asynchronous_apply_color(bool has_transitions)
{
	for (int i = 0; i < ARRAY_SIZE(policy_groups); i++) {
		policy_groups[i].driver->api->asynchronous_apply_color(
			has_transitions);
	}
}

/* Called by hook task every HOOK_TICK_INTERVAL_MS */
static void led_tick(void)
{
	bool has_transitions = led_set_all_colors();
	led_asynchronous_apply_color(has_transitions);
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
		return;
	}

	led_auto_control(led_id, 0);

	led_set_color(color, led_id, 100);
}

__override int led_is_supported(enum ec_led_id led_id)
{
	static int supported_leds = -1;

	if (supported_leds == -1) {
		supported_leds = 0;
		for (int i = 0; i < ARRAY_SIZE(policy_groups); i++) {
			supported_leds |= policy_groups[i].driver->led_id_mask;
		}
	}
	return ((1 << (int)led_id) & supported_leds);
}

/*
 * Iterate through LED pins nodes to find the color matching node.
 */
void led_set_color(enum led_color color, enum ec_led_id led_id,
		   uint8_t brightness)
{
	uint32_t mask = (1 << led_id);

	for (int i = 0; i < ARRAY_SIZE(policy_groups); i++) {
		if (policy_groups[i].driver->led_id_mask & mask) {
			policy_groups[i].driver->api->set_color(color, led_id,
								brightness);
		}
	}
}

void led_get_brightness_range(enum ec_led_id led_id, uint8_t *brightness_range)
{
	uint32_t mask = (1 << led_id);

	memset(brightness_range, 0, EC_LED_COLOR_COUNT);

	for (int i = 0; i < ARRAY_SIZE(policy_groups); i++) {
		if (policy_groups[i].driver->led_id_mask & mask) {
			policy_groups[i].driver->api->get_brightness_range(
				led_id, brightness_range);
			return;
		}
	}
}

int led_set_brightness(enum ec_led_id led_id, const uint8_t *brightness)
{
	uint32_t mask = (1 << led_id);

	for (int i = 0; i < ARRAY_SIZE(policy_groups); i++) {
		if (policy_groups[i].driver->led_id_mask & mask) {
			int rv = policy_groups[i].driver->api->set_brightness(
				led_id, brightness);
			return rv;
		}
	}

	return EC_ERROR_INVAL;
}
