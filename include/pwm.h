/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* PWM module for Chrome EC */

#ifndef __CROS_EC_PWM_H
#define __CROS_EC_PWM_H

#include "common.h"

/**
 * Enable/disable the fan.
 *
 * Should be called by whatever function enables the power supply to the fan.
 */
void pwm_enable_fan(int enable);

/**
 * Enable/disable fan RPM control logic.
 *
 * @param rpm_mode	Enable (1) or disable (0) RPM control loop; when
 *			disabled, fan duty cycle will be used.
 */
void pwm_set_fan_rpm_mode(int enable);

/**
 * Get the current fan RPM.
 */
int pwm_get_fan_rpm(void);

/**
 * Get the target fan RPM.
 */
int pwm_get_fan_target_rpm(void);

/**
 * Set the target fan RPM.
 *
 * @param rpm   Target RPM; pass -1 to set fan to maximum.
 */
void pwm_set_fan_target_rpm(int rpm);

/**
 * Set the fan PWM duty cycle (0-100), disabling the automatic control.
 */
void pwm_set_fan_duty(int percent);

/**
 * Set up the keyboard gpios.
 */
void configure_kblight_gpios(void);

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

/**
 * Configure the GPIOs for the pwm module -- board-specific.
 */
void configure_fan_gpios(void);

#endif  /* __CROS_EC_PWM_H */
