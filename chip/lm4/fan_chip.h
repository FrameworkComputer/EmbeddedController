/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* LM4-specific fan control module */

#ifndef __CROS_EC_LM4_FAN_CHIP_H
#define __CROS_EC_LM4_FAN_CHIP_H

void fan_chip_set_enabled(int ch, int enabled);
int fan_chip_get_enabled(int ch);
void fan_chip_set_duty(int ch, int percent);
int fan_chip_get_duty(enum pwm_channel ch);
int fan_chip_get_rpm_mode(int ch);
void fan_chip_set_rpm_mode(int ch, int rpm_mode);
int fan_chip_get_rpm_actual(int ch);
int fan_chip_get_rpm_target(int ch);
void fan_chip_set_rpm_target(int ch, int rpm);
int fan_chip_get_status(int ch);
int fan_chip_is_stalled(int ch);

/* Maintain target RPM using tach input */
#define FAN_CHIP_USE_RPM_MODE (1 << 0)
void fan_chip_channel_setup(int ch, unsigned int flags);

#endif /* __CROS_EC_LM4_FAN_CHIP_H */
