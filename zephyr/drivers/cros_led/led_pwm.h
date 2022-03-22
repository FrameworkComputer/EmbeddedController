/* Copyright 2022 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_LED_PWM_H__
#define __CROS_EC_LED_PWM_H__

#define COMPAT_PWM	cros_ec_multi_pwm_leds

#if DT_HAS_COMPAT_STATUS_OKAY(COMPAT_PWM)

/*
 * Enum representing the multi-PWM LED driver.
 * One is created for each instance of the multi-PWM LED driver.
 */
enum led_pwm_driver {
	DT_FOREACH_STATUS_OKAY(COMPAT_PWM, GEN_TYPE_INDEX_ENUM)
};

/*
 * PWM functions.
 */
void pwm_get_led_brightness_max(enum led_pwm_driver h, uint8_t *br);
void pwm_set_led_brightness(enum led_pwm_driver h, const uint8_t *br);
void pwm_set_led_colors(enum led_pwm_driver h, const uint8_t *colors);

#endif /* DT_HAS_COMPAT_STATUS_OKAY(COMPAT_PWM) */

#endif /* __CROS_EC_LED_PWM_H__ */
