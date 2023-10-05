/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_LED_H__
#define __CROS_EC_LED_H__

#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/pwm.h>

/*
 * Return string-token if the property exists, otherwise return 0
 */
#define GET_PROP(id, prop)                      \
	COND_CODE_1(DT_NODE_HAS_PROP(id, prop), \
		    (DT_STRING_UPPER_TOKEN(id, prop)), (0))

#define PINS_NODE_HELPER(parent_id, color_token) \
	DT_CAT4(PIN_NODE_, parent_id, _COLOR_, color_token)
#define PINS_NODE(id)                                     \
	PINS_NODE_HELPER(GET_PROP(DT_PARENT(id), led_id), \
			 GET_PROP(id, led_color))
#define PINS_ARRAY(id) DT_CAT(PINS_ARRAY_, id)
#define DATA_NODE(node_id) DT_CAT(DATA_NODE_, node_id)

enum led_color {
	LED_OFF,
	LED_RED,
	LED_GREEN,
	LED_BLUE,
	LED_YELLOW,
	LED_WHITE,
	LED_AMBER,
	LED_COLOR_COUNT /* Number of colors, not a color itself */
};

/* EC_LED_COLOR maps to LED_COLOR - 1 */
BUILD_ASSERT((LED_RED - 1) == EC_LED_COLOR_RED);
BUILD_ASSERT((LED_GREEN - 1) == EC_LED_COLOR_GREEN);
BUILD_ASSERT((LED_BLUE - 1) == EC_LED_COLOR_BLUE);
BUILD_ASSERT((LED_YELLOW - 1) == EC_LED_COLOR_YELLOW);
BUILD_ASSERT((LED_WHITE - 1) == EC_LED_COLOR_WHITE);
BUILD_ASSERT((LED_AMBER - 1) == EC_LED_COLOR_AMBER);
BUILD_ASSERT((LED_COLOR_COUNT - 1) == EC_LED_COLOR_COUNT);

enum led_transition {
	LED_TRANSITION_STEP,
	LED_TRANSITION_LINEAR,
	LED_TRANSITION_EXPONENTIAL,

	LED_TRANSITION_COUNT
};

#define LED_ENUM(id, enum_name) DT_STRING_TOKEN(id, enum_name)
#define LED_ENUM_WITH_COMMA(id, enum_name)           \
	COND_CODE_1(DT_NODE_HAS_PROP(id, enum_name), \
		    (LED_ENUM(id, enum_name), ), ())

/*
 * Struct defining LED GPIO pin and value to set.
 */
struct gpio_pin_t {
	enum gpio_signal signal;
	int val;
};

/* current state of the pwm pin */
struct pwm_data_t {
	struct pwm_dt_spec pwm_spec;
	uint32_t pulse_ns;
	enum led_transition transition;
};

/*
 * Struct defining LED PWM pin and duty cycle to set.
 */
struct pwm_pin_t {
	struct pwm_data_t *pwm;
	/*
	 * PWM Duty cycle ns.
	 * Not strictly positive because intermediate values may be negative
	 * while calculating transitions. For example, in a linear transition
	 * from a brightness of 100 to 0, we will calculate the decrease in
	 * brightness as a negative transition and add the negative pulse_ns to
	 * the current pulse_ns to calculate the new pulse_ns.
	 */
	int32_t pulse_ns;
};

/*
 * Pin node contains LED color and array of gpio/pwm pins
 * to alter in order to enable the given color.
 */
struct led_pins_node_t {
	/*
	 * Link between color and pins node. Only used to support
	 * ectool functionality.
	 */
	int led_color;

	/*
	 * Link between color and pins node. Only used to support
	 * ectool functionality.
	 */
	enum ec_led_id led_id;

#if CONFIG_PLATFORM_EC_LED_DT_GPIO
	/* Array of GPIO pins to set to enable particular color */
	struct gpio_pin_t *gpio_pins;
#endif

#if CONFIG_PLATFORM_EC_LED_DT_PWM
	/* Array of PWM pins to set to enable particular color */
	struct pwm_pin_t *pwm_pins;
#endif

	/* Number of pins per color */
	uint8_t pins_count;
};

struct pattern_color_node_t {
	struct led_pins_node_t *led_color_node;
	uint8_t duration;
};

struct led_pattern_node_t {
	uint8_t cur_color;
	uint8_t ticks;
	enum led_transition transition;
	struct pattern_color_node_t *pattern_color;
	uint8_t pattern_len;
};

#define GET_COLOR(pattern_element, color_index) \
	pattern_element.pattern_color[color_index].led_color
#define GET_DURATION(pattern_element, color_index) \
	pattern_element.pattern_color[color_index].duration

/**
 * Set LED color using color enum
 *
 * @param color		LED Color to enable
 * @param led_id	LED ID to set the color for
 */
void led_set_color(enum led_color color, enum ec_led_id led_id);

/**
 * Set LED color using pattern node
 *
 * @param *pins_node	Pins node to enable the color corresponding
 *			to the node.
 */
void led_set_color_with_pattern(const struct led_pattern_node_t *led);

/**
 * For pwms only.
 * The set function only sets the LED color into the pwm data.
 * the data is applied to the pin using this function. This allows the pwm LEDs
 * to be set and applied at different timings, allowing for a smoother
 * transition of pwm color without repeatedly checking the policy.
 */
void board_led_apply_color(void);

#ifdef TEST_BUILD
const struct led_pins_node_t *led_get_node(enum led_color color,
					   enum ec_led_id led_id);

enum power_state get_chipset_state(void);
#endif /* TEST_BUILD */

#endif /* __CROS_EC_LED_H__ */
