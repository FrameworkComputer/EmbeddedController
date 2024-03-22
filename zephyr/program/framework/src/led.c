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

enum breath_status {
	BREATH_LIGHT_UP = 0,
	BREATH_LIGHT_DOWN,
	BREATH_HOLD,
	BREATH_OFF,
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
		if (patterns[i].pattern_color[0].led_color_node->led_id == EC_LED_ID_POWER_LED)
			continue;

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

		if (pwr_state == LED_PWRS_DISCHARGE || pwr_state == LED_PWRS_DISCHARGE_FULL ||
			(pwr_state == LED_PWRS_IDLE && port < 0)) {
			gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_right_side), 0);
			gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_left_side), 0);
		} else {
			if (port < 0) {
				LOG_ERR("Illegal condition, port:%d, pwr:%d", port, pwr_state);
				return -1;
			}
			gpio_pin_set_dt(
				GPIO_DT_FROM_NODELABEL(gpio_right_side), (port < 2) ? 1 : 0);
			gpio_pin_set_dt(
				GPIO_DT_FROM_NODELABEL(gpio_left_side), (port >= 2) ? 1 : 0);
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

/* =========== Breath API =========== */

uint8_t breath_led_light_up;
uint8_t breath_led_light_down;
uint8_t breath_led_hold;
uint8_t breath_led_off;

int breath_pwm_enable;
int breath_led_status;
static void breath_led_pwm_deferred(void);
DECLARE_DEFERRED(breath_led_pwm_deferred);

static void multifunction_pwr_leds_control(int *colors, int num_color, int period)
{
	static uint32_t ticks;
	static int idx;

	ticks++;

	if ((ticks * 200) >= period) {
		ticks = 0;
		idx++;

		if (idx >= num_color)
			idx = 0;
	}

	led_set_color(colors[idx], EC_LED_ID_POWER_LED);
	board_led_apply_color();
}

/*
 *	Breath LED API
 *	Max duty (percentage) = BREATH_LIGHT_LENGTH (100%)
 *	Fade time (second) = 1000ms(In) / 1000ms(Out)
 *	Duration time (second) = BREATH_HOLD_LENGTH(500ms)
 *	Interval time (second) = BREATH_OFF_LENGTH(2000ms)
 */
static void breath_led_pwm_deferred(void)
{
	uint8_t led_hold_length;
	uint8_t led_duty_percentage;
	uint8_t bbram_led_level;

#ifdef CONFIG_BOARD_LOTUS
	if (chipset_in_state(CHIPSET_STATE_ANY_SUSPEND))
		bbram_led_level = FP_LED_LOW;
	else
#endif
		system_get_bbram(SYSTEM_BBRAM_IDX_FP_LED_LEVEL, &bbram_led_level);

	switch (bbram_led_level) {
	case FP_LED_LOW:
		led_duty_percentage = FP_LED_LOW;
		led_hold_length = BREATH_ON_LENGTH_LOW;
		break;
	case FP_LED_MEDIUM:
		led_duty_percentage = FP_LED_MEDIUM;
		led_hold_length = BREATH_ON_LENGTH_MID;
		break;
	case FP_LED_HIGH:
	default:
		led_duty_percentage = FP_LED_HIGH;
		led_hold_length = BREATH_ON_LENGTH_HIGH;
		break;
	}

	switch (breath_led_status) {
	case BREATH_LIGHT_UP:

		if (breath_led_light_up <= led_duty_percentage)
			pwm_set_breath_dt(breath_led_light_up++);
		else {
			breath_led_light_up = 0;
			breath_led_light_down = led_duty_percentage;
			breath_led_status = BREATH_HOLD;
		}

		break;
	case BREATH_HOLD:

		if (breath_led_hold <= led_hold_length)
			breath_led_hold++;
		else {
			breath_led_hold = 0;
			breath_led_status = BREATH_LIGHT_DOWN;
		}

		break;
	case BREATH_LIGHT_DOWN:

		if (breath_led_light_down != 0)
			pwm_set_breath_dt(--breath_led_light_down);
		else {
			breath_led_light_down = led_duty_percentage;
			breath_led_status = BREATH_OFF;
		}

		break;
	case BREATH_OFF:

		if (breath_led_off <= BREATH_OFF_LENGTH)
			breath_led_off++;
		else {
			breath_led_off = 0;
			breath_led_status = BREATH_LIGHT_UP;
		}

		break;
	}

	if (breath_pwm_enable)
		hook_call_deferred(&breath_led_pwm_deferred_data, 10 * MSEC);
}

void breath_led_run(uint8_t enable)
{
	if (enable && !breath_pwm_enable) {
		breath_pwm_enable = true;
		breath_led_status = BREATH_LIGHT_UP;
		hook_call_deferred(&breath_led_pwm_deferred_data, 10 * MSEC);
	} else if (!enable && breath_pwm_enable) {
		breath_pwm_enable = false;
		breath_led_light_up = 0;
		breath_led_light_down = 0;
		breath_led_hold = 0;
		breath_led_off = 0;
		breath_led_status = BREATH_OFF;
		hook_call_deferred(&breath_led_pwm_deferred_data, -1);
	}
}

static void board_led_set_power(void)
{
	uint8_t bbram_led_level;
	int colors[3] = {LED_OFF, LED_OFF, LED_OFF};

	system_get_bbram(SYSTEM_BBRAM_IDX_FP_LED_LEVEL, &bbram_led_level);

	/* turn off led when lid is close*/
	if (!lid_is_open()) {
		breath_led_run(0);
		led_set_color(LED_OFF, EC_LED_ID_POWER_LED);
		return;
	}

	if (check_s0ix_status()) {
		breath_led_run(1);
		return;
	}

	breath_led_run(0);

	if (chipset_in_state(CHIPSET_STATE_ON)) {
		if (charge_get_percent() < 3 && !extpower_is_present()) {
			colors[0] = LED_WHITE;
			colors[1] = LED_OFF;
			colors[2] = LED_OFF;
			multifunction_pwr_leds_control(colors, 2, 500);
		} else {
			pwm_set_breath_dt(bbram_led_level ? bbram_led_level : FP_LED_HIGH);
		}
	} else
		led_set_color(LED_OFF, EC_LED_ID_POWER_LED);
}

static void multifunction_leds_control(int *colors, int num_color, int period)
{
	static uint32_t ticks;
	static int idx;

	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_right_side), 1);
	gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_left_side), 1);

	ticks++;

	if ((ticks * 200) >= period) {
		ticks = 0;
		idx++;

		if (idx >= num_color)
			idx = 0;
	}

	led_set_color(colors[idx], EC_LED_ID_BATTERY_LED);
	board_led_apply_color();
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

	int colors[3] = {LED_OFF, LED_OFF, LED_OFF};

	if (led_auto_control_is_enabled(EC_LED_ID_POWER_LED))
		board_led_set_power();

	/* Facotry test */
	if (!led_auto_control_is_enabled(EC_LED_ID_BATTERY_LED)) {
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_right_side), 1);
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_left_side), 1);
		return;
	}

	/* Debug Active */
	if (diagnostics_tick())
		return;

	/* Battery disconnect active signal */
	if (battery_is_cut_off()) {
		colors[0] = LED_RED;
		colors[1] = LED_BLUE;
		colors[2] = LED_OFF;
		multifunction_leds_control(colors, 2, CONFIG_PLATFORM_MULTI_LED_FREQ);
		return;
	}

	/* Battery is not present, ignored if in standalone mode */
	if ((battery_is_present() != BP_YES) && !get_standalone_mode()) {
		colors[0] = LED_RED;
		colors[1] = LED_BLUE;
		colors[2] = LED_OFF;
		multifunction_leds_control(colors, 2, CONFIG_PLATFORM_MULTI_LED_FREQ);
		return;
	}

	/* C cover detect switch open */
	if (gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(gpio_chassis_open_l)) == 0 &&
		!get_standalone_mode()) {

		colors[0] = LED_RED;
		colors[1] = LED_OFF;
		colors[2] = LED_OFF;
		multifunction_leds_control(colors, 2, 1000);
		return;
	}

#ifdef CONFIG_BOARD_LOTUS
	/* GPU bay cover detect switch open */
	if (gpio_pin_get_dt(GPIO_DT_FROM_NODELABEL(gpio_f_beam_open_l)) == 0 &&
		!get_standalone_mode()) {
		colors[0] = LED_RED;
		colors[1] = LED_AMBER;
		colors[2] = LED_OFF;
		multifunction_leds_control(colors, 3, 1000);
		return;
	}

	/* GPU Bay Module Fault */
	if (gpu_module_fault() && extpower_is_present()) {
		colors[0] = LED_RED;
		colors[1] = LED_AMBER;
		colors[2] = LED_OFF;
		multifunction_leds_control(colors, 3, 1000);
		return;
	}

	/* Input Deck not fully populated */
	if (!input_deck_is_fully_populated() && !get_standalone_mode() &&
		!chipset_in_state(CHIPSET_STATE_ANY_OFF)) {
		colors[0] = LED_RED;
		colors[1] = LED_BLUE;
		colors[2] = LED_OFF;
		multifunction_leds_control(colors, 3, 500);
		return;
	}
#endif

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
