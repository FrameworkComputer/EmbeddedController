/* Copyright 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Fan control module for Chrome EC */

#ifndef __CROS_EC_FAN_H
#define __CROS_EC_FAN_H

struct fan_conf {
	unsigned int flags;
	/* Hardware channel number (the meaning is chip-specific) */
	int ch;
	/* Active-high power_good input GPIO, or -1 if none */
	int pgood_gpio;
	/* Active-high power_enable output GPIO, or -1 if none */
	int enable_gpio;
};

struct fan_rpm {
	/* rpm_min is to keep turning. rpm_start is to begin turning */
	int rpm_min;
	int rpm_start;
	int rpm_max;
};

/* Characteristic of each physical fan */
struct fan_t {
	const struct fan_conf *conf;
	const struct fan_rpm *rpm;
};

/* Values for .flags field */
/*   Enable automatic RPM control using tach input */
#define FAN_USE_RPM_MODE   BIT(0)
/*   Require a higher duty cycle to start up than to keep running */
#define FAN_USE_FAST_START BIT(1)

/* The list of fans is instantiated in board.c. */
#ifdef CONFIG_FAN_DYNAMIC
extern struct fan_t fans[];
#else
extern const struct fan_t fans[];
#endif

/* For convenience */
#define FAN_CH(fan)	fans[fan].conf->ch

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

/**
 * STOPPED means not spinning.
 *
 * When setting fan rpm, some implementations in chip layer (npcx and it83xx)
 * is to adjust fan pwm duty steps by steps. In this period, fan_status will
 * be marked as CHANGING. After change is done, fan_status will become LOCKED.
 *
 * In the period of changing pwm duty, if it's trying to increase/decrease duty
 * even when duty is already in upper/lower bound. Then this action won't work,
 * and fan_status will be marked as FRUSTRATED.
 *
 * For other implementations in chip layer (mchp and mec1322), there is no
 * changing period. So they don't have CHANGING status.
 * Just return status as LOCKED in normal spinning case, return STOPPED when
 * not spinning, return FRUSTRATED when the related flags (which is read from
 * chip's register) is set.
 */
enum fan_status {
	FAN_STATUS_STOPPED = 0,
	FAN_STATUS_CHANGING = 1,
	FAN_STATUS_LOCKED = 2,
	FAN_STATUS_FRUSTRATED = 3
};
enum fan_status fan_get_status(int ch);

/* Initialize the HW according to the desired flags */
void fan_channel_setup(int ch, unsigned int flags);

int fan_get_count(void);

void fan_set_count(int count);

#endif  /* __CROS_EC_FAN_H */
