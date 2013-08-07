/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_PWM_H
#define __CROS_EC_PWM_H

/**
 * Set the fan PWM duty cycle (0-100), disabling the automatic control.
 */
void pwm_set_fan_duty(int percent);

/**
 * Enable/disable the keyboard backlight.
 */
void pwm_enable_keyboard_backlight(int enable);

/**
 * Get the keyboard backlight enable/disable status (1=enabled, 0=disabled).
 */
int pwm_get_keyboard_backlight_enabled(void);

/**
 * Get the keyboard backlight percentage (0=off, 100=max).
 */
int pwm_get_keyboard_backlight(void);

/**
 * Set the keyboard backlight percentage (0=off, 100=max).
 */
void pwm_set_keyboard_backlight(int percent);

#endif  /* __CROS_EC_PWM_H */
