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

#define LED_COLOR_NODE DT_PATH(led_colors)

#define BOARD_LED_PWM_PERIOD_NS BOARD_LED_HZ_TO_PERIOD_NS(324)

struct led_color_node_t {
	struct led_pins_node_t *pins_node;
	int acc_period;
};

enum breath_status {
	BREATH_LIGHT_UP = 0,
	BREATH_LIGHT_DOWN,
	BREATH_HOLD,
	BREATH_OFF,
};

#define DECLARE_PINS_NODE(id) extern struct led_pins_node_t PINS_NODE(id);

DT_FOREACH_CHILD_STATUS_OKAY_VARGS(
	DT_COMPAT_GET_ANY_STATUS_OKAY(cros_ec_pwm_led_pins), DT_FOREACH_CHILD,
	DECLARE_PINS_NODE)

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
#define MAX_COLOR 4

struct node_prop_t {
	enum led_pwr_state pwr_state;
	enum power_state chipset_state;
	int batt_state_mask;
	int batt_state;
	int8_t batt_lvl[2];
	int8_t charge_port;
	struct led_color_node_t led_colors[MAX_COLOR];
};

/*
 * acc_period is the accumulated period value of all color-x children
 * led_colors[0].acc_period = period value of color-0 node
 * led_colors[1].acc_period = period value of color-0 + color-1 nodes
 * led_colors[2].acc_period = period value of color-0 + color-1 + color-2 nodes
 * and so on. If period prop or color node doesn't exist, period val is 0
 * It is stored in terms of number of ticks by dividing it with
 * HOOT_TICK_INTERVAL_MS
 */

#define PERIOD_VAL(id)                               \
	COND_CODE_1(DT_NODE_HAS_PROP(id, period_ms), \
		    (DT_PROP(id, period_ms) / HOOK_TICK_INTERVAL_MS), (0))

#define LED_PERIOD(color_num, state_id) \
	PERIOD_VAL(DT_CHILD(state_id, color_##color_num))

#define LED_PLUS_PERIOD(color_num, state_id) +LED_PERIOD(color_num, state_id)

#define ACC_PERIOD(color_num, state_id) \
	(0 LISTIFY(color_num, LED_PLUS_PERIOD, (), state_id))

#define PINS_NODE_ADDR(id) DT_PHANDLE(id, led_color)
#define LED_COLOR_INIT(color_num, color_num_plus_one, state_id)                \
	{                                                                      \
		.pins_node = COND_CODE_1(                                      \
			DT_NODE_EXISTS(DT_CHILD(state_id, color_##color_num)), \
			(&PINS_NODE(PINS_NODE_ADDR(                            \
				DT_CHILD(state_id, color_##color_num)))),      \
			(NULL)),                                               \
		.acc_period = ACC_PERIOD(color_num_plus_one, state_id)         \
	}

/*
 * Initialize node_array struct with prop listed in dts
 */
#define SET_LED_VALUES(state_id)                                              \
	{ .pwr_state = GET_PROP(state_id, charge_state),                      \
	  .chipset_state = GET_PROP(state_id, chipset_state),                 \
	  .batt_state_mask =                                                  \
		  COND_CODE_1(DT_NODE_HAS_PROP(state_id, batt_state_mask),    \
			      (DT_PROP(state_id, batt_state_mask)), (-1)),    \
	  .batt_state = COND_CODE_1(DT_NODE_HAS_PROP(state_id, batt_state),   \
				    (DT_PROP(state_id, batt_state)), (-1)),   \
	  .batt_lvl = COND_CODE_1(DT_NODE_HAS_PROP(state_id, batt_lvl),       \
				  (DT_PROP(state_id, batt_lvl)),              \
				  ({ -1, -1 })),                              \
	  .charge_port = COND_CODE_1(DT_NODE_HAS_PROP(state_id, charge_port), \
				     (DT_PROP(state_id, charge_port)), (-1)), \
	  .led_colors = {                                                     \
		  LED_COLOR_INIT(0, 1, state_id),                             \
		  LED_COLOR_INIT(1, 2, state_id),                             \
		  LED_COLOR_INIT(2, 3, state_id),                             \
		  LED_COLOR_INIT(3, 4, state_id),                             \
	  } },

static const struct node_prop_t node_array[] = { DT_INST_FOREACH_CHILD(
	0, SET_LED_VALUES) };

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

#define GET_PERIOD(n_idx, c_idx) node_array[n_idx].led_colors[c_idx].acc_period
#define GET_PIN_NODE(n_idx, c_idx) node_array[n_idx].led_colors[c_idx].pins_node

static void set_color(int node_idx, uint32_t ticks)
{
	int color_idx = 0;

	/* If accumulated period value is not 0, it's a blinking LED */
	if (GET_PERIOD(node_idx, MAX_COLOR - 1) != 0) {
		/*  Period is accumulated at the last index */
		ticks = ticks % GET_PERIOD(node_idx, MAX_COLOR - 1);
	}

	/*
	 * Period value of 0 indicates solid LED color (non-blinking)
	 * In case of dual port battery LEDs, period value of 0 is
	 * also used to turn-off non-active port LED
	 * Nodes with period value of 0 strictly need to be listed before
	 * nodes with non-zero period values as we are accumulating the
	 * period at each node.
	 *
	 * TODO: Remove the strict sequence requirement for listing the
	 * zero-period value nodes.
	 */
	for (color_idx = 0; color_idx < MAX_COLOR; color_idx++) {
		struct led_pins_node_t *pins_node =
			GET_PIN_NODE(node_idx, color_idx);
		int period = GET_PERIOD(node_idx, color_idx);

		if (pins_node == NULL)
			break; /* No more valid color nodes, break here */

		if (!led_auto_control_is_enabled(pins_node->led_id))
			break; /* Auto control is disabled */

		if (pins_node->led_id == EC_LED_ID_POWER_LED)
			break; /* Avoid power led control */

		/*
		 * Period value that we use here is in terms of number
		 * of ticks stored during initialization of the struct
		 */
		if (period == 0)
			led_set_color_with_node(pins_node);
		else if (ticks < period) {
			led_set_color_with_node(pins_node);
			break;
		}
	}
}

static int match_node(int node_idx)
{
	/* Check if this node depends on power state */
	if (node_array[node_idx].pwr_state != LED_PWRS_UNCHANGE) {
		enum led_pwr_state pwr_state = led_pwr_get_state();
		int port = charge_manager_get_active_charge_port();

		if (pwr_state == LED_PWRS_DISCHARGE || pwr_state == LED_PWRS_DISCHARGE_FULL) {
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

		if (node_array[node_idx].pwr_state != pwr_state)
			return -1;

		/* Check if this node depends on charge port */
		if (node_array[node_idx].charge_port != -1) {

			if (node_array[node_idx].charge_port != port)
				return -1;
		}
	}

	/* Check if this node depends on chipset state */
	if (node_array[node_idx].chipset_state != 0) {
		enum power_state chipset_state = get_chipset_state();

		if (node_array[node_idx].chipset_state != chipset_state)
			return -1;
	}

	/* check if this node depends on battery status */
	if (node_array[node_idx].batt_state_mask != -1) {
		int batt_state;

		battery_status(&batt_state);
		if ((node_array[node_idx].batt_state_mask & batt_state) !=
		    (node_array[node_idx].batt_state_mask &
		     node_array[node_idx].batt_state))
			return -1;
	}

	/* Check if this node depends on battery level */
	if (node_array[node_idx].batt_lvl[0] != -1) {
		int curr_batt_lvl =
			DIV_ROUND_NEAREST(get_system_percentage(), 10);

		if ((curr_batt_lvl < node_array[node_idx].batt_lvl[0]) ||
		    (curr_batt_lvl > node_array[node_idx].batt_lvl[1]))
			return -1;
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
}

void pwm_set_breath_dt(const struct led_pins_node_t *pins_node, int percent)
{
	struct pwm_pin_t *pwm_pins = pins_node->pwm_pins;
	uint32_t pulse_ns;

	/*
	 * pulse_ns = (period_ns*duty_cycle_in_perct)/100
	 * freq = 100 Hz, period_ns = 1000000000/100 = 10000000ns
	 * duty_cycle = 50 %, pulse_ns  = (10000000*50)/100 = 5000000ns
	 */

	pulse_ns = DIV_ROUND_NEAREST(BOARD_LED_PWM_PERIOD_NS * percent, 100);

	pwm_set_pulse_dt(&pwm_pins->pwm, pulse_ns);
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
			pwm_set_breath_dt(GET_PIN_NODE(7, 0), breath_led_light_up++);
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
			pwm_set_breath_dt(GET_PIN_NODE(7, 0),
				     --breath_led_light_down);
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
		} else
			pwm_set_breath_dt(GET_PIN_NODE(7, 0),
				bbram_led_level ? bbram_led_level : FP_LED_HIGH);
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
}

/* =============================== */

static void board_led_set_color(void)
{
	static uint32_t ticks;
	bool found_node = false;

	ticks++;

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
			set_color(i, ticks);
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

	led_set_color(color, led_id);
}
