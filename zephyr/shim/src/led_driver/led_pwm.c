/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * PWM LED control.
 */

#define DT_DRV_COMPAT cros_ec_pwm_led_pins

#include "ec_commands.h"
#include "led.h"
#include "util.h"

#include <zephyr/devicetree.h>
#include <zephyr/drivers/pwm.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(pwm_led, LOG_LEVEL_ERR);

BUILD_ASSERT(DT_NUM_INST_STATUS_OKAY(DT_DRV_COMPAT) == 1,
	     "Exactly one instance of cros-ec,pwm-led-pins should be defined.");

#define PWM_DATA_INIT(node_id)                                       \
	{                                                            \
		.pwm_spec = PWM_DT_SPEC_GET(node_id), .pulse_ns = 0, \
		.transition = LED_TRANSITION_STEP                    \
	}

#define GEN_PIN_DATA(node_id, prop, idx)                                     \
	struct pwm_data_t DATA_NODE(DT_PHANDLE_BY_IDX(node_id, prop, idx)) = \
		PWM_DATA_INIT(DT_PHANDLE_BY_IDX(node_id, prop, idx));

#define GEN_PINS_DATA(id) DT_FOREACH_PROP_ELEM(id, led_pwms, GEN_PIN_DATA)

DT_INST_FOREACH_CHILD_STATUS_OKAY(0, GEN_PINS_DATA)

#define SET_PIN(node_id, prop, i)                                            \
	{                                                                    \
		.pwm = &DATA_NODE(                                           \
			DT_PHANDLE_BY_IDX(DT_PARENT(node_id), led_pwms, i)), \
		.pulse_ns = DIV_ROUND_NEAREST(                               \
			DT_PWMS_PERIOD(DT_PHANDLE_BY_IDX(DT_PARENT(node_id), \
							 led_pwms, i)) *     \
				DT_PROP_BY_IDX(node_id, prop, i),            \
			100),                                                \
	},

#define SET_PWM_PIN(node_id) \
	{ DT_FOREACH_PROP_ELEM(node_id, led_values, SET_PIN) };

#define GEN_PINS_ARRAY(id) struct pwm_pin_t PINS_ARRAY(id)[] = SET_PWM_PIN(id)

DT_INST_FOREACH_CHILD_STATUS_OKAY_VARGS(0, DT_FOREACH_CHILD, GEN_PINS_ARRAY)

/* EC_LED_COLOR maps to LED_COLOR - 1 */
#define SET_PIN_NODE(node_id)                                   \
	{                                                       \
		.led_color = GET_PROP(node_id, led_color),      \
		.led_id = GET_PROP(DT_PARENT(node_id), led_id), \
		.pwm_pins = PINS_ARRAY(node_id),                \
		.pins_count = DT_PROP_LEN(node_id, led_values)  \
	}

/*
 * Initialize led_pins_node_t struct for each pin node defined
 */
#define GEN_PINS_NODES(id) \
	const struct led_pins_node_t PINS_NODE(id) = SET_PIN_NODE(id);

DT_INST_FOREACH_CHILD_STATUS_OKAY_VARGS(0, DT_FOREACH_CHILD, GEN_PINS_NODES)

/*
 * Array of pointers to each pin node
 */
#define PINS_NODE_PTR(id) &PINS_NODE(id),

const struct led_pins_node_t *pins_node[] = {
	DT_INST_FOREACH_CHILD_STATUS_OKAY_VARGS(0, DT_FOREACH_CHILD,
						PINS_NODE_PTR)
};

/*
 * Set all the PWM channels defined in the array to the defined value,
 * to enable the color. Defined value is duty cycle in percentage
 * converted to duty cycle in ns (pulse_ns)
 */
void led_set_color_with_pins(const struct pwm_pin_t *pwm_pins,
			     uint8_t pins_count, enum led_transition transition)
{
	for (int j = 0; j < pins_count; j++) {
		pwm_pins[j].pwm->pulse_ns = pwm_pins[j].pulse_ns;
		pwm_pins[j].pwm->transition = transition;
	}
}

/*
 * Iterate through LED pins nodes to find the color matching node.
 */
void led_set_color(enum led_color color, enum ec_led_id led_id)
{
	for (int i = 0; i < ARRAY_SIZE(pins_node); i++) {
		if ((pins_node[i]->led_color == color) &&
		    (pins_node[i]->led_id == led_id)) {
			led_set_color_with_pins(pins_node[i]->pwm_pins,
						pins_node[i]->pins_count,
						LED_TRANSITION_STEP);
			break;
		}
	}
}

/*
 * Set value for exponential pulsing as a minimum of 10,
 * because 0 to the power of anything is still 0.
 * Iterate through LED pins nodes to find the color matching node.
 */
#define PWM_MIN_NS 10
#define MSB(n) __builtin_clz(n)

/*
 * Currently, because the transitions are still on a 250ms tick rate, the
 * exponential transition approximates brightness to the closest power of 2.
 * a typical PWM LED will have pulse_ns at max brightness approximately equal to
 * 2^17.
 */
void led_set_color_with_pattern(const struct led_pattern_node_t *pattern)
{
	uint8_t pins_count = pattern->pattern_color[pattern->cur_color]
				     .led_color_node->pins_count;
	uint8_t duration = pattern->pattern_color[pattern->cur_color].duration;
	struct pwm_pin_t *next_color =
		pattern->pattern_color[pattern->cur_color]
			.led_color_node->pwm_pins;
	uint8_t prev_color_idx =
		(pattern->cur_color + pattern->pattern_len - 1) %
		pattern->pattern_len;
	struct pwm_pin_t *prev_color =
		pattern->pattern_color[prev_color_idx].led_color_node->pwm_pins;
	struct pwm_pin_t cur_color[pins_count];

	for (int i = 0; i < pins_count; i++) {
		cur_color[i].pwm = next_color[i].pwm;

		if (pattern->transition == LED_TRANSITION_LINEAR) {
			cur_color[i].pulse_ns = (next_color[i].pulse_ns -
						 prev_color[i].pulse_ns) *
							pattern->ticks /
							duration +
						prev_color[i].pulse_ns;
		}
		/*
		 * This algorithm first finds the ratio of the starting and end
		 * delay_ns (where a delay_ns of 0 is replaced with PWM_MIN_NS).
		 * This ratio is then expressed as a power of 2 by finding the
		 * Most Significant Bit (MSB). At each tick, the closest power
		 * of 2 progression is found by 2 ^ (MSB * tick / duration),
		 * then the previous color is multiplied or divided by this
		 * power of 2 to find cur_color.pulse_ns (in binary,
		 * multiplication or division by a power of 2 can by calculated
		 * by simply bit shifting by the power of 2 exponent).
		 */
		else if (pattern->transition == LED_TRANSITION_EXPONENTIAL) {
			if (next_color[i].pulse_ns > prev_color[i].pulse_ns) {
				int32_t scale =
					next_color[i].pulse_ns /
					MAX(prev_color[i].pulse_ns, PWM_MIN_NS);
				cur_color[i].pulse_ns =
					MAX(prev_color[i].pulse_ns, PWM_MIN_NS)
					<< (MSB(scale) * pattern->ticks /
					    duration);
			} else if (next_color[i].pulse_ns <
				   prev_color[i].pulse_ns) {
				int32_t scale =
					prev_color[i].pulse_ns /
					MAX(next_color[i].pulse_ns, PWM_MIN_NS);
				cur_color[i].pulse_ns =
					prev_color[i].pulse_ns >>
					(MSB(scale) * pattern->ticks /
					 duration);
			} else {
				cur_color[i].pulse_ns = next_color[i].pulse_ns;
			}
		} else { /* Default blinking or solid color */
			cur_color[i].pulse_ns = next_color[i].pulse_ns;
		}
	}

	led_set_color_with_pins(cur_color, pins_count, pattern->transition);
}

void led_get_brightness_range(enum ec_led_id led_id, uint8_t *brightness_range)
{
	memset(brightness_range, 0, EC_LED_COLOR_COUNT);

	for (int i = 0; i < ARRAY_SIZE(pins_node); i++) {
		int br_color = pins_node[i]->led_color - 1;

		if (pins_node[i]->led_id != led_id) {
			continue;
		}

		if (br_color != EC_LED_COLOR_INVALID) {
			brightness_range[br_color] = 100;
		}
	}
}

int led_set_brightness(enum ec_led_id led_id, const uint8_t *brightness)
{
	bool color_set = false;

	for (int i = 0; i < ARRAY_SIZE(pins_node); i++) {
		int br_color = pins_node[i]->led_color - 1;

		if (pins_node[i]->led_id != led_id) {
			continue;
		}

		if (br_color != EC_LED_COLOR_INVALID &&
		    brightness[br_color] != 0) {
			color_set = true;
			led_set_color(pins_node[i]->led_color, led_id);
		}
	}

	/* If no color was set, turn off the LED */
	if (!color_set)
		led_set_color(LED_OFF, led_id);

	board_led_apply_color();
	return EC_SUCCESS;
}

__override int led_is_supported(enum ec_led_id led_id)
{
	static int supported_leds = -1;

	if (supported_leds == -1) {
		supported_leds = 0;

		for (int i = 0; i < ARRAY_SIZE(pins_node); i++)
			supported_leds |= (1 << pins_node[i]->led_id);
	}

	return ((1 << (int)led_id) & supported_leds);
}

#define DT_SET_PULSE_WITH_DATA(pd) pwm_set_pulse_dt(&pd.pwm_spec, pd.pulse_ns);
#define PIN_APPLY_PULSE(node_id, prop, idx) \
	DT_SET_PULSE_WITH_DATA(DATA_NODE(DT_PHANDLE_BY_IDX(node_id, prop, idx)))
#define LED_APPLY_COLOR(id) DT_FOREACH_PROP_ELEM(id, led_pwms, PIN_APPLY_PULSE)

/* Called by hook task every HOOK_TICK_INTERVAL_MS */
void board_led_apply_color(void)
{
	DT_INST_FOREACH_CHILD_STATUS_OKAY(0, LED_APPLY_COLOR)
}
