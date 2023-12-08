/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_LED_H__
#define __CROS_EC_LED_H__

#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/pwm.h>

#define PINS_NODE(id) DT_CAT(PIN_NODE_, id)
#define PINS_ARRAY(id) DT_CAT(PINS_ARRAY_, id)

/*
 * Return string-token if the property exists, otherwise return 0
 */
#define GET_PROP(id, prop)                      \
	COND_CODE_1(DT_NODE_HAS_PROP(id, prop), \
		    (DT_STRING_UPPER_TOKEN(id, prop)), (0))

/*
 * Return string-token if the property exists, otherwise return
 * EC_LED_COLOR_INVALID.
 */
#define GET_COLOR_PROP_NVE(id, prop)                  \
	COND_CODE_1(DT_NODE_HAS_PROP(id, prop), \
		    (DT_STRING_UPPER_TOKEN(id, prop)), (EC_LED_COLOR_INVALID))

#define LED_ENUM(id, enum_name) DT_STRING_TOKEN(id, enum_name)
#define LED_ENUM_WITH_COMMA(id, enum_name)           \
	COND_CODE_1(DT_NODE_HAS_PROP(id, enum_name), \
		    (LED_ENUM(id, enum_name), ), ())

#define FP_LED_HIGH 55
#define FP_LED_MEDIUM 40
#define FP_LED_LOW 15

#define BREATH_ON_LENGTH_HIGH	62
#define BREATH_ON_LENGTH_MID	72
#define BREATH_ON_LENGTH_LOW	90

#define BREATH_OFF_LENGTH 200

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

enum fp_led_brightness_level {
	FP_LED_BRIGHTNESS_HIGH = 0,
	FP_LED_BRIGHTNESS_MEDIUM = 1,
	FP_LED_BRIGHTNESS_LOW = 2,
};

/*
 * Struct defining LED GPIO pin and value to set.
 */
struct gpio_pin_t {
	enum gpio_signal signal;
	int val;
};

/*
 * Struct defining LED PWM pin and duty cycle to set.
 */
struct pwm_pin_t {
	struct pwm_dt_spec pwm;
	uint32_t pulse_ns; /* PWM Duty cycle ns */
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

	/* Brightness Range color, only used to support ectool functionality */
	enum ec_led_colors br_color;

	/* Array of PWM pins to set to enable particular color */
	struct pwm_pin_t *pwm_pins;

	/* Number of pins per color */
	uint8_t pins_count;
};

/**
 * Set LED color using color enum
 *
 * @param color		LED Color to enable
 * @param led_id	LED ID to set the color for
 */
void led_set_color(enum led_color color, enum ec_led_id led_id);

/**
 * Set LED color using pins node
 *
 * @param *pins_node	Pins node to enable the color corresponding
 *			to the node.
 */
void led_set_color_with_node(const struct led_pins_node_t *pins_node);

#ifdef TEST_BUILD
const struct led_pins_node_t *led_get_node(enum led_color color,
					   enum ec_led_id led_id);

enum power_state get_chipset_state(void);
#endif /* TEST_BUILD */

#endif /* __CROS_EC_LED_H__ */
