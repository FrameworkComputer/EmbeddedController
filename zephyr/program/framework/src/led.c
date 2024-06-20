/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Power and battery LED control.
 */

#define DT_DRV_COMPAT cros_ec_led_policy
#include <stdint.h>

#include "battery.h"
#include "board_led.h"
#include "charge_manager.h"
#include "charge_state.h"
#include "chipset.h"
#include "ec_commands.h"
#include "extpower.h"
#include "hooks.h"
#include "host_command.h"
#include "led.h"
#include "led_common.h"
#include "power.h"
#include "power_sequence.h"
#include "system.h"
#include "util.h"

#include "board_function.h"
#include "cypress_pd_common.h"
#include "diagnostics.h"
#include "lid_switch.h"

#ifdef CONFIG_BOARD_LOTUS
#include "gpu.h"
#include "input_module.h"
#endif

#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(led, LOG_LEVEL_ERR);

BUILD_ASSERT(DT_NUM_INST_STATUS_OKAY(DT_DRV_COMPAT) == 1,
	     "Exactly one instance of cros-ec,led-policy should be defined.");

#define DECLARE_PINS_NODE(id) extern struct led_pins_node_t PINS_NODE(id);
#define BOARD_LED_PWM_PERIOD_NS BOARD_LED_HZ_TO_PERIOD_NS(324)

DT_FOREACH_CHILD_STATUS_OKAY_VARGS(
	DT_COMPAT_GET_ANY_STATUS_OKAY(cros_ec_pwm_led_pins), DT_FOREACH_CHILD,
	DECLARE_PINS_NODE)

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

const enum ec_led_id supported_led_ids[] = { EC_LED_ID_BATTERY_LED,
					     EC_LED_ID_POWER_LED };

const int supported_led_ids_count = ARRAY_SIZE(supported_led_ids);

static int led_tick_time = 200;
static bool pre_multifunction_led_state;
static bool pre_fingerprint_led_state;

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

static void change_pwm_led_maximum_duty(void)
{
	int node_idx, pattern_idx, color_idx, num_patterns;
	enum ec_led_id id;
	struct led_pattern_node_t *patt;
	struct pwm_pin_t *target_pwm;
	uint8_t fingerpint_led_level;
	uint64_t pulse_ns;

	system_get_bbram(SYSTEM_BBRAM_IDX_FP_LED_LEVEL, &fingerpint_led_level);

	if (fingerpint_led_level == 0)
		fingerpint_led_level = FP_LED_HIGH;

	pulse_ns = DIV_ROUND_NEAREST(BOARD_LED_PWM_PERIOD_NS * fingerpint_led_level, 100);

	/* found the power led id */
	for (node_idx = 0; node_idx < ARRAY_SIZE(node_array); node_idx++) {

		num_patterns = node_array[node_idx].num_patterns;
		patt = node_array[node_idx].led_patterns;

		for (pattern_idx = 0; pattern_idx < num_patterns; pattern_idx++) {

			id = patt[pattern_idx].pattern_color[0].led_color_node->led_id;
			if (id == EC_LED_ID_POWER_LED) {

				for (color_idx = 0; color_idx < patt->pattern_len;
					color_idx++) {
					target_pwm =
					  patt->pattern_color[color_idx].led_color_node->pwm_pins;

					if (target_pwm->pulse_ns != 0)
						target_pwm->pulse_ns = pulse_ns;
				}
			}
		}
	}
}
DECLARE_DEFERRED(change_pwm_led_maximum_duty);
DECLARE_HOOK(HOOK_INIT, change_pwm_led_maximum_duty, HOOK_PRIO_DEFAULT + 1);

void update_pwr_led_level(void)
{
	hook_call_deferred(&change_pwm_led_maximum_duty_data, 100 * MSEC);
}

static void set_color(int node_idx)
{
	struct led_pattern_node_t *patterns = node_array[node_idx].led_patterns;
	enum ec_led_id led_id;

	for (int i = 0; i < node_array[node_idx].num_patterns; i++) {

		led_id = patterns[i].pattern_color[0].led_color_node->led_id;

		/* Auto control is disabled, factory control */
		if (!led_auto_control_is_enabled(led_id))
			continue;

		/* customized fingerprint led feature is enabled */
		if (pre_fingerprint_led_state && led_id == EC_LED_ID_POWER_LED)
			continue;

		/* customized multifunctino led feature is enabled */
		if (pre_multifunction_led_state && led_id == EC_LED_ID_BATTERY_LED)
			continue;

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

/*
 * The script zephyr/scripts/led_policy.py is used to verify that all
 * power/battery states are covered by the cros-ec,led-policy devicetree.
 * Update the python script whenever major changes are made to the matching
 * function here.
 */
static int match_node(int node_idx)
{
	/* Check if this node depends on power state */
	if (node_array[node_idx].pwr_state != LED_PWRS_UNCHANGE) {
		enum led_pwr_state pwr_state = led_pwr_get_state();
		int port = charge_manager_get_active_charge_port();

		if (led_auto_control_is_enabled(EC_LED_ID_BATTERY_LED) &&
			!pre_multifunction_led_state) {
			if (pwr_state == LED_PWRS_DISCHARGE ||
				pwr_state == LED_PWRS_DISCHARGE_FULL ||
				(pwr_state == LED_PWRS_IDLE && port < 0)) {
				gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_right_side), 0);
				gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_left_side), 0);
			} else {
				if (port < 0) {
					LOG_ERR("Illegal condition, port:%d, pwr:%d",
						port, pwr_state);
					return -1;
				}
				gpio_pin_set_dt(
					GPIO_DT_FROM_NODELABEL(gpio_right_side),
						(port < 2) ? 1 : 0);
				gpio_pin_set_dt(
					GPIO_DT_FROM_NODELABEL(gpio_left_side),
						(port >= 2) ? 1 : 0);
			}
		}

		if (node_array[node_idx].pwr_state != pwr_state) {
			node_array[node_idx].state_active = false;
			return -1;
		}

		/* Check if this node depends on charge port */
		if (node_array[node_idx].charge_port != -1) {

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

static void customized_leds_set_color(int *colors, int num_color,
			int period, enum ec_led_id id)
{
	static uint32_t ticks;
	static int idx;

	ticks++;

	if ((ticks * led_tick_time) >= period) {
		ticks = 0;
		idx++;

		if (idx >= num_color)
			idx = 0;
	}

	led_set_color(colors[idx], id);
}

static bool multifunction_leds_control(void)
{
	int colors[3] = {LED_OFF, LED_OFF, LED_OFF};

	/* In facotry mode, don't control led */
	if (!led_auto_control_is_enabled(EC_LED_ID_BATTERY_LED))
		return false;

	/* Debug Active */
	if (diagnostics_tick())
		return true;

	/* Battery disconnect active signal */
	if (battery_is_cut_off()) {
		colors[0] = LED_RED;
		colors[1] = LED_BLUE;
		colors[2] = LED_OFF;
		customized_leds_set_color(colors, 2, CONFIG_PLATFORM_MULTI_LED_FREQ,
			EC_LED_ID_BATTERY_LED);
		return true;
	}

	/* Battery is not present, ignored if in standalone mode */
	if ((battery_is_present() != BP_YES) && !get_standalone_mode()) {
		colors[0] = LED_RED;
		colors[1] = LED_BLUE;
		colors[2] = LED_OFF;
		customized_leds_set_color(colors, 2, CONFIG_PLATFORM_MULTI_LED_FREQ,
			EC_LED_ID_BATTERY_LED);
		return true;
	}

	/* C cover detect switch open */
	if (gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(gpio_chassis_open_l)) == 0 &&
		!get_standalone_mode()) {

		colors[0] = LED_RED;
		colors[1] = LED_OFF;
		colors[2] = LED_OFF;
		customized_leds_set_color(colors, 2, 1000, EC_LED_ID_BATTERY_LED);
		return true;
	}

#ifdef CONFIG_BOARD_LOTUS
	/* GPU bay cover detect switch open */
	if (gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(gpio_f_beam_open_l)) == 0 &&
		!get_standalone_mode()) {
		colors[0] = LED_RED;
		colors[1] = LED_AMBER;
		colors[2] = LED_OFF;
		customized_leds_set_color(colors, 3, 1000, EC_LED_ID_BATTERY_LED);
		return true;
	}

	/* GPU Bay Module Fault */
	if (gpu_module_fault() && extpower_is_present()) {
		colors[0] = LED_RED;
		colors[1] = LED_AMBER;
		colors[2] = LED_OFF;
		customized_leds_set_color(colors, 3, 1000, EC_LED_ID_BATTERY_LED);
		return true;
	}

	/* Input Deck not fully populated */
	if (!input_deck_is_fully_populated() && !get_standalone_mode() &&
		!chipset_in_state(CHIPSET_STATE_ANY_OFF)) {
		colors[0] = LED_RED;
		colors[1] = LED_BLUE;
		colors[2] = LED_OFF;
		customized_leds_set_color(colors, 3, 500, EC_LED_ID_BATTERY_LED);
		return true;
	}
#endif

	return false;
}

static bool fingerprint_led_control(void)
{
	int colors[3] = {LED_OFF, LED_OFF, LED_OFF};

	/* In facotry mode, don't control led */
	if (!led_auto_control_is_enabled(EC_LED_ID_POWER_LED))
		return false;

	/* Turn off fingerprint LED when lid is closed */
	if (!lid_is_open()) {
		led_set_color(LED_OFF, EC_LED_ID_POWER_LED);
		return true;
	}

	if (chipset_in_state(CHIPSET_STATE_ON) && (charge_get_percent() < 3) &&
		!extpower_is_present()) {
		colors[0] = LED_WHITE;
		colors[1] = LED_OFF;
		colors[2] = LED_OFF;
		customized_leds_set_color(colors, 2, 500, EC_LED_ID_POWER_LED);
		return true;
	}

	return false;
}

/* Called by hook task every HOOK_TICK_INTERVAL_MS */
static void led_tick(void);
DECLARE_DEFERRED(led_tick);
static void led_tick(void)
{
	int enable;

	/* If multifunction leds is enabled, disable the battery led auto control */
	enable = multifunction_leds_control();
	if (pre_multifunction_led_state != enable)
		pre_multifunction_led_state = enable;

	/* If multifunction leds is enabled, disable the power led auto control */
	enable = fingerprint_led_control();
	if (pre_fingerprint_led_state != enable)
		pre_fingerprint_led_state = enable;

	/* Facotry test */
	if (!led_auto_control_is_enabled(EC_LED_ID_BATTERY_LED) ||
		pre_multifunction_led_state) {
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_right_side), 1);
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_left_side), 1);
	}

	if (chipset_in_state(CHIPSET_STATE_ANY_SUSPEND))
		led_tick_time = 10;
	else
		led_tick_time = 200;

	board_led_set_color();
	board_led_apply_color();

	hook_call_deferred(&led_tick_data, led_tick_time * MSEC);
}

static void led_hook_init(void)
{
	hook_call_deferred(&led_tick_data, 200 * MSEC);
}
DECLARE_HOOK(HOOK_INIT, led_hook_init, HOOK_PRIO_DEFAULT);

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
