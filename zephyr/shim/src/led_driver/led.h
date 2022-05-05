/* Copyright 2022 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_LED_H__
#define __CROS_EC_LED_H__

#include <zephyr/devicetree.h>
#include <drivers/gpio.h>
#include <drivers/pwm.h>

#define COMPAT_GPIO_LED cros_ec_gpio_led_pins
#define COMPAT_PWM_LED  cros_ec_pwm_led_pins

#define PINS_NODE(id)	DT_CAT(PIN_NODE_, id)
#define PINS_ARRAY(id)	DT_CAT(PINS_ARRAY_, id)

/*
 * Return string-token if the property exists, otherwise return 0
 */
#define GET_PROP(id, prop)						\
	COND_CODE_1(DT_NODE_HAS_PROP(id, prop),				\
		    (DT_STRING_UPPER_TOKEN(id, prop)),			\
		    (0))

/*
 * Return string-token if the property exists, otherwise return -1
 */
#define GET_PROP_NVE(id, prop)						\
	COND_CODE_1(DT_NODE_HAS_PROP(id, prop),				\
		    (DT_STRING_UPPER_TOKEN(id, prop)),			\
		    (-1))

#define LED_ENUM(id, enum_name)	 DT_STRING_TOKEN(id, enum_name)
#define LED_ENUM_WITH_COMMA(id, enum_name)				\
	COND_CODE_1(DT_NODE_HAS_PROP(id, enum_name),			\
		    (LED_ENUM(id, enum_name),), ())

#define GPIO_LED_PINS_NODE DT_PATH(gpio_led_pins)
#define PWM_LED_PINS_NODE  DT_PATH(pwm_led_pins)

enum led_color {
	LED_OFF,
	LED_RED,
	LED_GREEN,
	LED_BLUE,
	LED_YELLOW,
	LED_WHITE,
	LED_AMBER,
	LED_COLOR_COUNT  /* Number of colors, not a color itself */
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
	const struct device *pwm;
	uint8_t channel;
	pwm_flags_t flags;
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

#if DT_HAS_COMPAT_STATUS_OKAY(COMPAT_GPIO_LED)
	/* Array of GPIO pins to set to enable particular color */
	struct gpio_pin_t *gpio_pins;
#endif

#if DT_HAS_COMPAT_STATUS_OKAY(COMPAT_PWM_LED)
	/* Array of PWM pins to set to enable particular color */
	struct pwm_pin_t *pwm_pins;
#endif

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

#endif /* __CROS_EC_LED_H__ */
