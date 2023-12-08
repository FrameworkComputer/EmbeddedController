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

/*
 * Period in ns from frequency(Hz) defined in pins node
 * period in sec = 1/freq
 * period in nsec = (1*nsec_per_sec)/freq
 * This value is also used calculate duty_cycle in ns (pulse_ns below).
 * Duty cycle in perct defined in pin node is used to calculate pulse_ns
 * pulse_ns = (period_ns*duty_cycle_in_perct)/100
 * e.g. freq = 500 Hz, period_ns = 1000000000/500 = 2000000ns
 * duty_cycle = 50 %, pulse_ns  = (2000000*50)/100 = 1000000ns
 */

#define SET_PIN(node_id, prop, i)                                            \
	{                                                                    \
		.pwm = PWM_DT_SPEC_GET(                                      \
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

#define SET_PIN_NODE(node_id)                                \
	{ .led_color = GET_PROP(node_id, led_color),         \
	  .led_id = GET_PROP(DT_PARENT(node_id), led_id),    \
	  .br_color = GET_COLOR_PROP_NVE(node_id, br_color), \
	  .pwm_pins = PINS_ARRAY(node_id),                   \
	  .pins_count = DT_PROP_LEN(node_id, led_values) };

/*
 * Initialize led_pins_node_t struct for each pin node defined
 */
#define GEN_PINS_NODES(id) \
	const struct led_pins_node_t PINS_NODE(id) = SET_PIN_NODE(id)

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
 * Set all the PWM channels defined in the node to the defined value,
 * to enable the color. Defined value is duty cycle in percentage
 * converted to duty cycle in ns (pulse_ns)
 */
void led_set_color_with_node(const struct led_pins_node_t *pins_node)
{
	struct pwm_pin_t *pwm_pins = pins_node->pwm_pins;
	for (int j = 0; j < pins_node->pins_count; j++) {
		pwm_set_pulse_dt(&pwm_pins[j].pwm, pwm_pins[j].pulse_ns);
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
			led_set_color_with_node(pins_node[i]);
			break;
		}
	}
}

void led_get_brightness_range(enum ec_led_id led_id, uint8_t *brightness_range)
{
	memset(brightness_range, 0, EC_LED_COLOR_COUNT);

	for (int i = 0; i < ARRAY_SIZE(pins_node); i++) {
		int br_color = pins_node[i]->br_color;

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
		int br_color = pins_node[i]->br_color;

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
