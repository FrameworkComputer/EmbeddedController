/* Copyright 2022 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * PWM LED control.
 */

#include "ec_commands.h"
#include "led.h"
#include "util.h"

#include <devicetree.h>
#include <drivers/pwm.h>
#include <logging/log.h>

#if DT_HAS_COMPAT_STATUS_OKAY(COMPAT_PWM_LED)

LOG_MODULE_REGISTER(pwm_led, LOG_LEVEL_ERR);

#define PWM_LED_PINS_NODE	DT_PATH(pwm_led_pins)
#define LED_PIN_COUNT		(LED_COLOR_COUNT - 1)

/*
 * Struct defining LED PWM pin and duty cycle to set.
 */
struct pwm_pin_t {
	const struct device *pwm;
	uint8_t channel;
	pwm_flags_t flags;
	uint32_t pulse_us; /* PWM Duty cycle us */
};

/*
 * Pin node contains LED color and array of PWM channels
 * to alter in order to enable the given color.
 */
struct led_pins_node_t {
	/* Link between color and pins node */
	int led_color;

	/* Brightness Range color, only used by ectool funcs for testing */
	enum ec_led_colors br_color;

	/* Array of PWM pins to set to enable particular color */
	struct pwm_pin_t pwm_pins[LED_PIN_COUNT];
};

/*
 * Period in us from frequency(Hz) defined in pins node
 * period in sec = 1/freq
 * period in usec = (1*usec_per_sec)/freq
 * This value is also used calculate duty_cycle in us (pulse_us below).
 * Duty cycle in perct defined in pin node is used to calculate pulse_us
 * pulse_us = (period_us*duty_cycle_in_perct)/100
 * Eg. freq = 500 Hz, period_us = 1000000/500 = 2000us
 * duty_cycle = 50 %, pulse_us  = (2000*50)/100 = 1000us
 */
const uint32_t period_us =
		(USEC_PER_SEC / DT_PROP(PWM_LED_PINS_NODE, pwm_frequency));

#define SET_PIN(node_id, prop, i)					\
{									\
	.pwm = DEVICE_DT_GET(						\
		DT_PWMS_CTLR(DT_PHANDLE_BY_IDX(node_id, prop, i))),	\
	.channel = DT_PWMS_CHANNEL(					\
			DT_PHANDLE_BY_IDX(node_id, prop, i)),		\
	.flags = DT_PWMS_FLAGS(DT_PHANDLE_BY_IDX(node_id, prop, i)),	\
	.pulse_us = DIV_ROUND_NEAREST(					\
	   period_us * DT_PHA_BY_IDX(node_id, prop, i, value), 100),	\
},

#define SET_PWM_PIN(node_id)						\
{									\
	DT_FOREACH_PROP_ELEM(node_id, led_pins, SET_PIN)		\
}

#define SET_PIN_NODE(node_id)						\
{									\
	.led_color = GET_PROP(node_id, led_color),			\
	.br_color = GET_BR_COLOR(node_id, br_color),			\
	.pwm_pins = SET_PWM_PIN(node_id)				\
},

struct led_pins_node_t pins_node[] = {
	DT_FOREACH_CHILD(PWM_LED_PINS_NODE, SET_PIN_NODE)
};

/*
 * Iterate through LED pins nodes to find the color matching node.
 * Set all the PWM channels defined in the node to the defined value,
 * to enable the color. Defined value is duty cycle in percentage
 * converted to duty cycle in us (pulse_us)
 */
void led_set_color(enum led_color color)
{
	for (int i = 0; i < LED_COLOR_COUNT; i++) {
		if (pins_node[i].led_color == color) {
			for (int j = 0; j < LED_PIN_COUNT; j++) {
				pwm_pin_set_usec(
					pins_node[i].pwm_pins[j].pwm,
					pins_node[i].pwm_pins[j].channel,
					period_us,
					pins_node[i].pwm_pins[j].pulse_us,
					pins_node[i].pwm_pins[j].flags);
			}
			break; /* Found the matching pin node, break here */
		}
	}
}

void led_get_brightness_range(enum ec_led_id led_id, uint8_t *brightness_range)
{
	for (int i = 0; i < ARRAY_SIZE(pins_node); i++) {
		int br_color = pins_node[i].br_color;

		if (br_color != -1)
			brightness_range[br_color] = 100;
	}
}

int led_set_brightness(enum ec_led_id led_id, const uint8_t *brightness)
{
	bool color_set = false;

	for (int i = 0; i < ARRAY_SIZE(pins_node); i++) {
		int br_color = pins_node[i].br_color;

		if ((br_color != -1) && (brightness[br_color] != 0)) {
			color_set = true;
			led_set_color(pins_node[i].led_color);
		}
	}

	/* If no color was set, turn off the LED */
	if (!color_set)
		led_set_color(LED_OFF);

	return EC_SUCCESS;
}
#endif /* DT_HAS_COMPAT_STATUS_OKAY(COMPAT_PWM_LED) */
