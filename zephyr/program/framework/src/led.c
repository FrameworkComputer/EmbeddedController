/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Power and battery LED control.
 */

#define DT_DRV_COMPAT cros_ec_fwk_led_policy
#include <stdint.h>

#include "battery.h"
#include "board_host_command.h"
#include "board_led.h"
#include "board_function.h"
#include "charge_manager.h"
#include "charge_state.h"
#include "chipset.h"
#include "diagnostics.h"
#include "ec_commands.h"
#include "extpower.h"
#include "hooks.h"
#include "host_command.h"
#include "hid_device.h"
#include "keyboard_backlight.h"
#include "led.h"
#include "led_common.h"
#include "math_util.h"
#include "power.h"
#include "power_sequence.h"
#include "system.h"
#include "util.h"

#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(led, LOG_LEVEL_ERR);

BUILD_ASSERT(DT_NUM_INST_STATUS_OKAY(DT_DRV_COMPAT) == 1,
	     "Exactly one instance of cros-ec,fwk-led-policy should be defined.");

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
		.period_ms = DT_PROP_OR(id, period_ms, 0)                     \
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
	bool standalone_mode;
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
		.standalone_mode =                                                 \
			COND_CODE_1(DT_NODE_HAS_PROP(state_id, standalone_mode),   \
				    (DT_PROP(state_id, standalone_mode)), (false)),   \
		.led_patterns = PATTERN_NODE_ARRAY(state_id),                 \
		.num_patterns = 0 fn(state_id, PLUS_ONE),                     \
		.state_active = false,                                        \
	},

static struct node_prop_t node_array[] = {
	DT_INST_FOREACH_CHILD_STATUS_OKAY_VARGS(0, SET_LED_VALUES,
						DT_FOREACH_CHILD)
};

static int led_tick_time = 200;
static bool pre_multifunction_led_state;
static bool pre_fingerprint_led_state;

static bool fp_als_auto_brightness;
static bool last_fp_led_brightness;
#ifdef CONFIG_PLATFORM_EC_KEYBOARD
static int last_kbbl_led_brightness;
#endif
static int prev_als_lux;

static bool als_stable;
static timestamp_t als_init_deadline;

static int als_lux_get(void)
{
	uint16_t real_illuminance = *(uint16_t *)host_get_memmap(EC_MEMMAP_ALS);
	timestamp_t now = get_time();

	if (chipset_in_state(CHIPSET_STATE_ON) && !als_stable) {
		if (now.val > als_init_deadline.val + 2 * SECOND) {
			als_stable = true;
			als_init_deadline.val = 0;
		}
		/*
		 * before ALS returns stable data (als.c common)
		 * keep 2 second of the lightness to default high
		 * power button brightness
		 **/
		real_illuminance = FP_LED_HIGH_ALS_THRESH + 1;
	} else if (chipset_in_state(CHIPSET_STATE_ANY_OFF)) {
		als_init_deadline.val = now.val;
		if (als_stable) {
			als_stable = false;
			/* Default keyboard backlight to off */
			real_illuminance = KB_BL_THRESHOLD + 1;
			/* clear als data and set default level for next time bootup */
			*(uint16_t *)host_get_memmap(EC_MEMMAP_ALS) = 0;
			system_set_bbram(SYSTEM_BBRAM_IDX_FP_LED_LEVEL, FP_LED_HIGH);
			update_pwr_led_level();
		}
	}

	return real_illuminance;
}

int led_get_current_tick_time(void)
{
	return led_tick_time;
}

/*
 * return auto dim status, it decided by BIOS setup menu
 * option(Power Button LED Brightness level).
 */
int fp_led_auto_is_enable(void)
{
	return fp_als_auto_brightness;
}

/*
 * According to Microsoft's ALS spec common lighting conditions.
 * Reference the first 5 level to modify the auto range.
 *
 * +------------------------+-----------+----------------+
 * | Lighting condition     | lux       |  lux-fpled-kb  |
 * +------------------------+-----------+----------------+
 * | Pitch black            | 1         | 40(8%)(5%)     |
 * | Very dark              | 10        | 70(15%)(20%    |
 * | Dark indoors           | 50        | 100(28%)(50%)  |
 * | Dim indoors            | 100       | 130(40%)(75%)  |
 * | Normal indoors         | 300       | 200(55%)(100%) |
 * | Bright indoors         | 700       | 200(55%)(100%) |
 * | Dim outdoors(overcast) | 1000      | 200(55%)(100%) |
 * | Sunlight outdoors      | 15000     | 200(55%)(100%) |
 * | Direct Sunlight        | 100,000   | 200(55%)(100%) |
 * +------------------------+-----------+---------------+
 */
void auto_als_led_brightness(void)
{
	int als_lux = als_lux_get();
	int led_brightness;
#ifdef CONFIG_PLATFORM_EC_KEYBOARD
	int kb_brightness;

	if (kbbl_auto_dim_is_enable() &&
		chipset_in_state(CHIPSET_STATE_ON)) {
		last_kbbl_led_brightness = kblight_get();

		if (als_lux > KB_BL_THRESHOLD)
			kb_brightness = KEYBOARD_BL_BRIGHTNESS_OFF;
		else
			kb_brightness = KEYBOARD_BL_BRIGHTNESS_ULT_LOW;

		if (last_kbbl_led_brightness != kb_brightness) {
			last_kbbl_led_brightness = kb_brightness;
			kblight_set(kb_brightness);
		}
	}
#endif

	/* Only change power button brightness if lux has significantly changed */
	/* Otherwise if it's around a threshold it might flip back and forth */
	if (prev_als_lux != 0 && (ABS(als_lux - prev_als_lux) <= 15))
		return;
	prev_als_lux = als_lux;

	if (fp_led_auto_is_enable() &&
		chipset_in_state(CHIPSET_STATE_ON)) {
		if (als_lux > FP_LED_HIGH_ALS_THRESH)
			led_brightness = FP_LED_HIGH;
		else if (als_lux > FP_LED_MED_ALS_THRESH)
			led_brightness = FP_LED_MEDIUM;
		else if (als_lux > FP_LED_MED_LOW_ALS_THRESH)
			led_brightness = FP_LED_MEDIUM_LOW;
		else if (als_lux > FP_LED_LOW_ALS_THRESH)
			led_brightness = FP_LED_LOW;
		else
			led_brightness = FP_LED_ULTRA_LOW;

		if (last_fp_led_brightness != led_brightness) {
			last_fp_led_brightness = led_brightness;
			system_set_bbram(SYSTEM_BBRAM_IDX_FP_LED_LEVEL, led_brightness);
			update_pwr_led_level();
		}
	}
}

/*
 * Force ALS dependent brightness (keyboard, powerbutton) to immediately adjust
 * based on ALS measurement, if they're enabled. Useeful if the setting was just enabled.
 */
void auto_als_led_reset(void)
{
	prev_als_lux = 0;
	if (IS_ENABLED(CONFIG_PLATFORM_EC_DEDICATED_ALS))
		auto_als_led_brightness();
}

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

		if (GET_DURATION(patterns[i], patterns[i].cur_color, led_tick_time) != 0)
			patterns[i].ticks++;

		if (patterns[i].ticks >=
		    GET_DURATION(patterns[i], patterns[i].cur_color, led_tick_time)) {
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
#if (IS_ENABLED(CONFIG_PLATFORM_EC_CHARGE_MANAGER))
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
				/* If charging from left or right, indicate on that side.
				 * If charging from the back (dGPU), indicate
				 * on right side only to meet Lot6 requirements
				 **/
				gpio_pin_set_dt(
					GPIO_DT_FROM_NODELABEL(gpio_right_side),
						(port == 0 || port == 1 || port == 4) ? 1 : 0);
				gpio_pin_set_dt(
					GPIO_DT_FROM_NODELABEL(gpio_left_side),
						(port == 2 || port == 3) ? 1 : 0);
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
#endif /* CONFIG_PLATFORM_EC_CHARGE_MANAGER */

	/* Check if this node depends on chipset state */
	if (node_array[node_idx].chipset_state != 0) {
		enum power_state chipset_state = get_chipset_state();

		if (node_array[node_idx].chipset_state != chipset_state) {
			node_array[node_idx].state_active = false;
			return -1;
		}
	}

#if (IS_ENABLED(CONFIG_PLATFORM_EC_BATTERY))
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
#endif /* CONFIG_PLATFORM_EC_BATTERY */

#if (IS_ENABLED(CONFIG_PLATFORM_EC_CHARGE_MANAGER))
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

	/* Check if this node depends on standalone mode */
	if (node_array[node_idx].standalone_mode) {
		int curr_standalone_mode = get_standalone_mode();

		if (node_array[node_idx].standalone_mode != curr_standalone_mode) {
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
#endif /* CONFIG_PLATFORM_EC_CHARGE_MANAGER */

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

void customized_leds_set_color(int *colors, int num_color,
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

/* Called by hook task every HOOK_TICK_INTERVAL_MS */
static void led_tick(void);
DECLARE_DEFERRED(led_tick);
static void led_tick(void)
{
#ifndef CONFIG_PLATFORM_EC_FRAMEWORK_MINI_PC
	int enable;

	/* If multifunction leds is enabled, disable the battery led auto control */
	enable = multifunction_leds_control();
	if (pre_multifunction_led_state != enable)
		pre_multifunction_led_state = enable;

	/* If multifunction leds is enabled, disable the power led auto control */
	enable = power_button_led_control();
	if (pre_fingerprint_led_state != enable)
		pre_fingerprint_led_state = enable;

	/* Facotry test */
	if (!led_auto_control_is_enabled(EC_LED_ID_BATTERY_LED) ||
		pre_multifunction_led_state) {
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_right_side), 1);
		gpio_pin_set_dt(GPIO_DT_FROM_NODELABEL(gpio_left_side), 1);
	}
#endif

	if (chipset_in_state(CHIPSET_STATE_ANY_SUSPEND))
		led_tick_time = 10;
	else
		led_tick_time = 200;

	if (IS_ENABLED(CONFIG_PLATFORM_EC_DEDICATED_ALS))
		auto_als_led_brightness();

	board_led_set_color();
	board_led_apply_color();

	hook_call_deferred(&led_tick_data, led_tick_time * MSEC);
}

static void led_hook_init(void)
{
	uint8_t als_auto;

	system_get_bbram(SYSTEM_BBRAM_IDX_BIOS_FUNCTION, &als_auto);
	if (als_auto & ALS_AUTO_FP) {
		/*
		 * if enable auto als fp, set the default level to high
		 * and call the update level to update pwm duty
		 **/
		system_set_bbram(SYSTEM_BBRAM_IDX_FP_LED_LEVEL, FP_LED_HIGH);
		update_pwr_led_level();
		fp_als_auto_brightness = true;
	}
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

static enum ec_status fp_led_level_control(struct host_cmd_handler_args *args)
{
	const struct ec_params_fp_led_control_v0 *p_v0 = args->params;
	const struct ec_params_fp_led_control_v1 *p_v1 = args->params;
	struct ec_response_fp_led_level_v0 *r_v0 = args->response;
	struct ec_response_fp_led_level_v1 *r_v1 = args->response;
	uint8_t led_level = FP_LED_HIGH;
	uint8_t als_auto;

	system_get_bbram(SYSTEM_BBRAM_IDX_BIOS_FUNCTION, &als_auto);
	/* Returns percentage in HC v0 and v1 */
	if (p_v0->get_led_level) {
		if (args->version == 0) {
			system_get_bbram(SYSTEM_BBRAM_IDX_FP_LED_LEVEL, &r_v0->percentage);
			args->response_size = sizeof(*r_v0);
			return EC_RES_SUCCESS;
		} else if (args->version == 1) {
			system_get_bbram(SYSTEM_BBRAM_IDX_FP_LED_LEVEL, &r_v1->percentage);
			/* Map from percentage to level */
			/* Yes, it could also have been set manually to this level */
			/* But I don't it's worth adding additional state to track that */
			switch (r_v1->percentage) {
			case FP_LED_HIGH:
				r_v1->level = FP_LED_BRIGHTNESS_HIGH;
				break;
			case FP_LED_MEDIUM:
				r_v1->level = FP_LED_BRIGHTNESS_MEDIUM;
				break;
			case FP_LED_LOW:
				r_v1->level = FP_LED_BRIGHTNESS_LOW;
				break;
			case FP_LED_ULTRA_LOW:
				r_v1->level = FP_LED_BRIGHTNESS_ULTRA_LOW;
				break;
			default:
				r_v1->level = FP_LED_BRIGHTNESS_CUSTOM;
				break;
			}

			/* If auto mode, overwrite deduced level */
			if (fp_led_auto_is_enable())
				r_v1->level = FP_LED_BRIGHTNESS_AUTO;

			args->response_size = sizeof(*r_v1);
			return EC_RES_SUCCESS;
		}
	}

	if (args->version == 0) {
		als_auto &= ~ALS_AUTO_FP;
		/* HC v0 only allows setting 3 discrete levels */
		switch (p_v0->set_led_level) {
		case FP_LED_BRIGHTNESS_HIGH:
			led_level = FP_LED_HIGH;
			break;
		case FP_LED_BRIGHTNESS_MEDIUM:
			led_level = FP_LED_MEDIUM;
			break;
		case FP_LED_BRIGHTNESS_LOW:
			led_level = FP_LED_LOW;
			break;
		case FP_LED_BRIGHTNESS_ULTRA_LOW:
			led_level = FP_LED_ULTRA_LOW;
			break;
		/* Not used, use v1 to set custom, is only ever returned when getting */
		case FP_LED_BRIGHTNESS_CUSTOM:
		/* Keep using v0, even though auto is a new value */
		/* v1 is for setting custom percentage */
		case FP_LED_BRIGHTNESS_AUTO:
			als_auto |= ALS_AUTO_FP;
			break;
		default:
			return EC_RES_INVALID_PARAM;
		}
	} else if (args->version == 1) {
		/* HC v1 allows setting 1-100 percentage */
		if (p_v1->set_percentage == 0 || p_v1->set_percentage > 100)
			return EC_RES_INVALID_PARAM;
		led_level = p_v1->set_percentage;
		als_auto &= ~ALS_AUTO_FP;
	}

	/*
	 * save option setting for next time boot
	 * also make this time setting on work.
	 *
	 * If setting to auto, don't need to update the led_level now
	 * it'll be updated later in the periodic task based on ALS value
	 **/
	system_set_bbram(SYSTEM_BBRAM_IDX_BIOS_FUNCTION, als_auto);
	if (als_auto & ALS_AUTO_FP) {
		fp_als_auto_brightness = true;
	} else {
		fp_als_auto_brightness = false;
		system_set_bbram(SYSTEM_BBRAM_IDX_FP_LED_LEVEL, led_level);
		update_pwr_led_level();
	}

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_FP_LED_LEVEL_CONTROL, fp_led_level_control,
		     EC_VER_MASK(0) | EC_VER_MASK(1));
