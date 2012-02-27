/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* PWM module for Chrome EC */

#ifndef __CROS_EC_PWM_H
#define __CROS_EC_PWM_H

#include "common.h"

/* Initializes the module. */
int pwm_init(void);

/* Enables/disables the fan.  This should be called by whatever function
 * enables the power supply to the fan. */
int pwm_enable_fan(int enable);

/* Gets the current fan RPM. */
int pwm_get_fan_rpm(void);

/* Get the target fan RPM. */
int pwm_get_fan_target_rpm(void);

/* Sets the target fan RPM.  Pass -1 to set fan to maximum. */
int pwm_set_fan_target_rpm(int rpm);

/* Gets the keyboard backlight percentage (0=off, 100=max). */
int pwm_get_keyboard_backlight(void);

/* Sets the keyboard backlight percentage (0=off, 100=max). */
int pwm_set_keyboard_backlight(int percent);

/* Sets the power LED brightness to the specified percent (0=off, 100=max). */
int pwm_set_power_led(int percent);

#endif  /* __CROS_EC_PWM_H */
