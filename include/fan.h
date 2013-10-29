/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Fan control module for Chrome EC */

#ifndef __CROS_EC_FAN_H
#define __CROS_EC_FAN_H

/**
 * Set the amount of active cooling needed. The thermal control task will call
 * this frequently, and the fan control logic will attempt to provide it.
 *
 * @param pct   Percentage of cooling effort needed (0 - 100)
 */
void fan_set_percent_needed(int pct);

/**
 * This function translates the percentage of cooling needed into a target RPM.
 * The default implementation should be sufficient for most needs, but
 * individual boards may provide a custom version if needed (see config.h).
 *
 * @param pct   Percentage of cooling effort needed (always in [0,100])
 * Return       Target RPM for fan
 */
int fan_percent_to_rpm(int pct);


/**
 * These functions require chip-specific implementations.
 */

void fan_set_enabled(int ch, int enabled);
int fan_get_enabled(int ch);
void fan_set_duty(int ch, int percent);
int fan_get_duty(int ch);
void fan_set_rpm_mode(int ch, int rpm_mode);
int fan_get_rpm_mode(int ch);
void fan_set_rpm_target(int ch, int rpm);
int fan_get_rpm_actual(int ch);
int fan_get_rpm_target(int ch);
int fan_is_stalled(int ch);

enum fan_status {
	FAN_STATUS_STOPPED = 0,
	FAN_STATUS_CHANGING = 1,
	FAN_STATUS_LOCKED = 2,
	FAN_STATUS_FRUSTRATED = 3
};
enum fan_status fan_get_status(int ch);

/* Maintain target RPM using tach input */
#define FAN_USE_RPM_MODE (1 << 0)
void fan_channel_setup(int ch, unsigned int flags);

#endif  /* __CROS_EC_FAN_H */
