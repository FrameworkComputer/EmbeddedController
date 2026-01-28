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
#include <zephyr/spinlock.h>
#include <zephyr/sys/util.h>
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

static void led_execute_patterns(void);

static void led_animation_worker(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(led_worker_data, led_animation_worker);

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

static bool is_pattern_done(struct led_pattern_node_t *pattern)
{
	uint8_t last;

	/* 0 means infinite cycles */
	if (pattern->cycle_limit == 0) {
		return false;
	}

	pattern->cycle_curr++;
	if (pattern->cycle_curr < pattern->cycle_limit) {
		return false;
	}

	/* Limit reached. Hold final state. */
	last = pattern->pattern_len - 1;
	if (pattern->cur_color != last) {
		pattern->cur_color = last;
		pattern->elapsed_ms = get_step_duration(pattern, last);
		pattern->needs_update = true;
	}

	return true;
}

static void advance_led_pattern(struct led_pattern_node_t *pattern,
				uint32_t increment)
{
	uint32_t duration;
	int steps = 0;
	uint8_t prev_color = pattern->cur_color;

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
			if (is_pattern_done(pattern)) {
				return;
			}
			/* Cycle continues, wrap to start */
			pattern->cur_color = 0;
		}

		duration = get_step_duration(pattern, pattern->cur_color);
	}

	/* Reset time if limit hit to prevent accumulation/overflow */
	if (steps >= pattern->pattern_len) {
		pattern->elapsed_ms = 0;
	}

	/*
	 * Mark for update if color changed. Exponential transitions also
	 * require updates because they use absolute value recalculation.
	 *
	 * TODO: Support relative calculation for exponential transitions
	 * so the update flag is not required.
	 */
	if (pattern->cur_color != prev_color ||
	    pattern->transition == LED_TRANSITION_EXPONENTIAL) {
		pattern->needs_update = true;
	}
}

static void led_init_pattern_state(struct led_pattern_node_t *pattern)
{
	pattern->cur_color = 0;
	pattern->elapsed_ms = 0;
	pattern->cycle_curr = 0;
	pattern->needs_update = true;
	/* Skip initial 0-duration colors before first render */
	advance_led_pattern(pattern, 0);
}

struct node_status {
	bool needs_apply;
	bool is_active;
	bool is_animating;
	bool has_transitions;
};

static void process_pattern_update(const struct policy_group *grp,
				   struct led_pattern_node_t *pattern,
				   uint32_t increment,
				   struct node_status *status)
{
	bool pattern_is_done = (pattern->cycle_limit > 0 &&
				pattern->cycle_curr >= pattern->cycle_limit);

	/* Apply color calculated in the previous tick */
	if (pattern->needs_update) {
		status->needs_apply = true;
		grp->driver->api->set_color_with_pattern(pattern);
		pattern->needs_update = false;
	}

	/* Advance state machine for the next tick */
	advance_led_pattern(pattern, increment);

	if (pattern_is_done) {
		return;
	}
	status->is_active = true;

	if (pattern->transition != LED_TRANSITION_STEP ||
	    pattern->pattern_len > 1) {
		status->is_animating = true;
	}

	if (pattern->transition != LED_TRANSITION_STEP) {
		status->has_transitions = true;
	}
}

/* Reset any built-in patterns that match the given led_id */
static void reset_policy_patterns(enum ec_led_id led_id)
{
	for (int i = 0; i < ARRAY_SIZE(policy_groups); i++) {
		const struct policy_group *grp = &policy_groups[i];

		for (int j = 0; j < grp->num_nodes; j++) {
			const struct node_prop_t *node = &grp->nodes[j];

			if (!grp->active[j]) {
				continue;
			}

			for (int k = 0; k < node->num_patterns; k++) {
				struct led_pattern_node_t *pat =
					&node->led_patterns[k];
				enum ec_led_id id =
					pat->pattern_color[0]
						.led_color_node->led_id;

				if (id == led_id) {
					led_init_pattern_state(pat);
				}
			}
		}
	}
}

/* Pointer to runtime-assigned patterns from host commands */
static struct custom_led_patterns_t *g_custom_patterns;
static struct k_spinlock led_custom_lock;

void led_set_custom_patterns(struct custom_led_patterns_t *p)
{
	struct custom_led_patterns_t *old_pattern;
	k_spinlock_key_t key;

	if (p) {
		/* Initialize state for new custom patterns */
		for (int i = 0; i < p->num_patterns; i++) {
			led_init_pattern_state(&p->led_patterns[i]);
		}
	}

	/* Atomic swap to ensure we get the old pattern */
	key = k_spin_lock(&led_custom_lock);
	old_pattern = g_custom_patterns;
	g_custom_patterns = p;
	k_spin_unlock(&led_custom_lock, key);

	if (old_pattern) {
		reset_policy_patterns(old_pattern->led_id);
	}

	/* Schedule animation worker to execute patterns */
	k_work_schedule(&led_worker_data, K_NO_WAIT);
}

static bool is_custom_pattern_active(const struct custom_led_patterns_t *p,
				     enum ec_led_id led_id)
{
	return p && p->led_id == led_id;
}

static struct node_status
update_custom_node(const struct policy_group *grp,
		   struct custom_led_patterns_t *custom, uint32_t increment)
{
	struct node_status status = { 0 };

	if (!custom) {
		return status;
	}

	/* Only process if the LED is managed by this driver */
	if (!(grp->driver->led_id_mask & (BIT(custom->led_id)))) {
		return status;
	}

	/* Check if auto control is enabled */
	if (!led_auto_control_is_enabled(custom->led_id)) {
		return status;
	}

	for (int i = 0; i < custom->num_patterns; i++) {
		struct led_pattern_node_t *pattern = &custom->led_patterns[i];

		process_pattern_update(grp, pattern, increment, &status);
	}
	return status;
}

/* Clears the custom pattern if the given policy node conflicts with it. */
static void cancel_custom_if_conflict(const struct node_prop_t *node)
{
	k_spinlock_key_t key;
	bool conflict = false;
	int i;

	key = k_spin_lock(&led_custom_lock);
	if (g_custom_patterns) {
		for (i = 0; i < node->num_patterns; i++) {
			enum ec_led_id id = node->led_patterns[i]
						    .pattern_color[0]
						    .led_color_node->led_id;

			if (g_custom_patterns->led_id == id) {
				conflict = true;
				break;
			}
		}
	}
	k_spin_unlock(&led_custom_lock, key);

	if (conflict) {
		led_set_custom_patterns(NULL);
	}
}

static struct node_status
update_policy_node(const struct policy_group *grp,
		   const struct node_prop_t *node,
		   struct custom_led_patterns_t *custom, uint32_t increment)
{
	struct led_pattern_node_t *patterns = node->led_patterns;
	struct node_status status = { 0 };

	for (int i = 0; i < node->num_patterns; i++) {
		struct led_pattern_node_t *pattern = &patterns[i];
		enum ec_led_id led_id =
			pattern->pattern_color[0].led_color_node->led_id;

		/* If a custom pattern is active, skip default policy. */
		if (is_custom_pattern_active(custom, led_id)) {
			continue;
		}

		/* Check if auto control is enabled */
		if (!led_auto_control_is_enabled(led_id)) {
			continue;
		}

		process_pattern_update(grp, pattern, increment, &status);
	}
	return status;
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

		/*
		 * If a system state transition activates a policy for an LED
		 * currently running a custom pattern, cancel the custom
		 * pattern.
		 */
		cancel_custom_if_conflict(node);

		for (int i = 0; i < node->num_patterns; i++) {
			struct led_pattern_node_t *pattern =
				&node->led_patterns[i];

			led_init_pattern_state(pattern);
		}
		/* Schedule animation worker to execute patterns */
		k_work_schedule(&led_worker_data, K_NO_WAIT);
	}

	/* We found the node that matches the current system state */
	return node_idx;
}

static void led_update_policy_state(void)
{
	/*
	 * Find all the nodes that match the current state of the system and
	 * mark them as active. Depending on the policy defined in
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
			}
		}
		if (!found_node) {
			LOG_ERR("Node with matching prop not found");
		}
	}
}

#ifdef CONFIG_ZTEST
uint32_t led_test_apply_count;
#endif

static void led_execute_patterns(void)
{
	bool continue_animating = false;
	bool custom_patterns_active = false;
	struct custom_led_patterns_t *active_custom;
	k_spinlock_key_t key;
	int64_t start_time = k_uptime_get();
	int64_t elapsed_ms;
	int64_t delay_ms;

	key = k_spin_lock(&led_custom_lock);
	active_custom = g_custom_patterns;
	k_spin_unlock(&led_custom_lock, key);

	/* Iterate through all policy groups to process active patterns */
	for (int i = 0; i < ARRAY_SIZE(policy_groups); i++) {
		const struct policy_group *grp = &policy_groups[i];
		struct node_status group_status = { 0 };
		struct node_status status;

		/* 1. Process custom patterns (high priority) */
		status = update_custom_node(grp, active_custom,
					    LED_ANIMATION_TICK_MS);
		group_status.needs_apply |= status.needs_apply;
		group_status.is_animating |= status.is_animating;
		group_status.has_transitions |= status.has_transitions;

		if (status.is_active) {
			custom_patterns_active = true;
		}

		/* 2. Process DT-defined policy patterns */
		for (int j = 0; j < grp->num_nodes; j++) {
			if (!grp->active[j]) {
				continue;
			}

			status = update_policy_node(grp, &grp->nodes[j],
						    active_custom,
						    LED_ANIMATION_TICK_MS);
			group_status.needs_apply |= status.needs_apply;
			group_status.is_animating |= status.is_animating;
			group_status.has_transitions |= status.has_transitions;
		}

		if (group_status.is_animating) {
			continue_animating = true;
		}

		if (group_status.needs_apply || group_status.has_transitions) {
#ifdef CONFIG_ZTEST
			led_test_apply_count++;
#endif
			grp->driver->api->asynchronous_apply_color(
				group_status.has_transitions);
		}
	}

	/*
	 * If we have a custom pattern but it is no longer active (completed
	 * its cycles), clear it so the next tick resumes normal policy.
	 */
	if (active_custom && !custom_patterns_active) {
		bool cleared = false;

		key = k_spin_lock(&led_custom_lock);
		/* Check global hasn't changed before clearing */
		if (g_custom_patterns == active_custom) {
			g_custom_patterns = NULL;
			cleared = true;
		}
		k_spin_unlock(&led_custom_lock, key);

		/* Only reset if the above cleared the custom pattern */
		if (cleared) {
			reset_policy_patterns(active_custom->led_id);
			continue_animating = true;
		}
	}

	if (continue_animating) {
		elapsed_ms = k_uptime_delta(&start_time);
		delay_ms = max(0, (int64_t)LED_ANIMATION_TICK_MS - elapsed_ms);
		k_work_schedule(&led_worker_data, K_MSEC(delay_ms));
	} else {
		k_work_cancel_delayable(&led_worker_data);
	}
}

static void led_animation_worker(struct k_work *work)
{
	led_execute_patterns();
}

/* Called by hook task every HOOK_TICK_INTERVAL_MS */
static void led_tick(void)
{
	led_update_policy_state();
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
	return (BIT(led_id) & supported_leds);
}

static const struct led_driver_t *led_find_driver(enum ec_led_id led_id)
{
	uint32_t mask = BIT(led_id);

	for (int i = 0; i < ARRAY_SIZE(policy_groups); i++) {
		if (policy_groups[i].driver->led_id_mask & mask) {
			return policy_groups[i].driver;
		}
	}

	/* LCOV_EXCL_START - Unreachable as led_is_supported() is called first
	 * to filter out this case.
	 */
	return NULL;
	/* LCOV_EXCL_STOP */
}

void led_set_color(enum led_color color, enum ec_led_id led_id,
		   uint8_t brightness)
{
	const struct led_driver_t *drv = led_find_driver(led_id);

	if (drv) {
		drv->api->set_color(color, led_id, brightness);
	}
}

void led_get_brightness_range(enum ec_led_id led_id, uint8_t *brightness_range)
{
	const struct led_driver_t *drv = led_find_driver(led_id);

	if (drv) {
		drv->api->get_brightness_range(led_id, brightness_range);
	}
}

int led_set_brightness(enum ec_led_id led_id, const uint8_t *brightness)
{
	const struct led_driver_t *drv = led_find_driver(led_id);

	if (drv) {
		return drv->api->set_brightness(led_id, brightness);
	}

	/* LCOV_EXCL_START - Unreachable as led_is_supported() is called first
	 * to filter out this case.
	 */
	return EC_ERROR_INVAL;
	/* LCOV_EXCL_STOP */
}
