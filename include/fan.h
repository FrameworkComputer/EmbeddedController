/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Fan control module for Chrome EC */

#ifndef __CROS_EC_FAN_H
#define __CROS_EC_FAN_H

/* Characteristic of each physical fan */
struct fan_t {
	unsigned int flags;
	/* rpm_min is to keep turning. rpm_start is to begin turning */
	int rpm_min;
	int rpm_start;
	int rpm_max;
	/* Hardware channel number (the meaning is chip-specific) */
	int ch;
	/* Active-high power_good input GPIO, or -1 if none */
	int pgood_gpio;
	/* Active-high power_enable output GPIO, or -1 if none */
	int enable_gpio;
};

/* Values for .flags field */
/*   Enable automatic RPM control using tach input */
#define FAN_USE_RPM_MODE   (1 << 0)
/*   Require a higher duty cycle to start up than to keep running */
#define FAN_USE_FAST_START (1 << 1)

/* The list of fans is instantiated in board.c. */
extern const struct fan_t fans[];


/**
 * Set the amount of active cooling needed. The thermal control task will call
 * this frequently, and the fan control logic will attempt to provide it.
 *
 * @param fan   Fan number (index into fans[])
 * @param pct   Percentage of cooling effort needed (0 - 100)
 */
void fan_set_percent_needed(int fan, int pct);

/**
 * This function translates the percentage of cooling needed into a target RPM.
 * The default implementation should be sufficient for most needs, but
 * individual boards may provide a custom version if needed (see config.h).
 *
 * @param fan   Fan number (index into fans[])
 * @param pct   Percentage of cooling effort needed (always in [0,100])
 * Return       Target RPM for fan
 */
int fan_percent_to_rpm(int fan, int pct);


/**
 * These functions require chip-specific implementations.
 */

/* Enable/Disable the fan controller */
void fan_set_enabled(int ch, int enabled);
int fan_get_enabled(int ch);

/* Fixed pwm duty cycle (0-100%) */
void fan_set_duty(int ch, int percent);
int fan_get_duty(int ch);

/* Enable/Disable automatic RPM control using tach feedback */
void fan_set_rpm_mode(int ch, int rpm_mode);
int fan_get_rpm_mode(int ch);

/* Set the target for the automatic RPM control */
void fan_set_rpm_target(int ch, int rpm);
int fan_get_rpm_actual(int ch);
int fan_get_rpm_target(int ch);

/* Is the fan stalled when it shouldn't be? */
int fan_is_stalled(int ch);

/* How is the automatic RPM control doing? */
enum fan_status {
	FAN_STATUS_STOPPED = 0,
	FAN_STATUS_CHANGING = 1,
	FAN_STATUS_LOCKED = 2,
	FAN_STATUS_FRUSTRATED = 3
};
enum fan_status fan_get_status(int ch);

/* Initialize the HW according to the desired flags */
void fan_channel_setup(int ch, unsigned int flags);

#endif  /* __CROS_EC_FAN_H */
