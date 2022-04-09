/* Copyright 2022 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_LED_H__
#define __CROS_EC_LED_H__

#include <devicetree.h>

#define COMPAT_GPIO_LED cros_ec_gpio_led_pins
#define COMPAT_PWM_LED  cros_ec_pwm_led_pins

#define GET_PROP(id, prop)						\
	COND_CODE_1(DT_NODE_HAS_PROP(id, prop),				\
		    (DT_STRING_UPPER_TOKEN(id, prop)),			\
		    (0))

#define GET_BR_COLOR(id, prop)						\
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
#if DT_NODE_EXISTS(GPIO_LED_PINS_NODE)
	DT_FOREACH_CHILD_VARGS(GPIO_LED_PINS_NODE,
			LED_ENUM_WITH_COMMA, led_color)
#elif DT_NODE_EXISTS(PWM_LED_PINS_NODE)
	DT_FOREACH_CHILD_VARGS(PWM_LED_PINS_NODE,
			LED_ENUM_WITH_COMMA, led_color)
#endif
	LED_COLOR_COUNT  /* Number of colors, not a color itself */
};

/**
 * Set LEDs to enable given color
 *
 * @param color LED Color to enable
 */
void led_set_color(enum led_color color);

#endif /* __CROS_EC_LED_H__ */
