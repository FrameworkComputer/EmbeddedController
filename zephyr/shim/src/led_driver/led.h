/* Copyright 2022 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_LED_H__
#define __CROS_EC_LED_H__

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

/* TODO(b/227798487): Use DT to generate this enum instead of hardcoding */
enum led_color {
	LED_OFF = 0,
	LED_AMBER,
	LED_BLUE,
	LED_COLOR_COUNT  /* Number of colors, not a color itself */
};

/**
 * Set LEDs to enable given color
 *
 * @param color LED Color to enable
 */
void led_set_color(enum led_color color);

#endif /* __CROS_EC_LED_H__ */
