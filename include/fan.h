/* Copyright 2013 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Fan control module for Chrome EC */

#ifndef __CROS_EC_FAN_H
#define __CROS_EC_FAN_H

#include "config.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef CONFIG_ZEPHYR
#ifdef CONFIG_PLATFORM_EC_FAN

#include <zephyr/devicetree.h>
#include <zephyr/drivers/pwm.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NODE_ID_AND_COMMA(node_id) node_id,
enum fan_channel {
#if DT_NODE_EXISTS(DT_INST(0, cros_ec_fans))
	DT_FOREACH_CHILD(DT_INST(0, cros_ec_fans), NODE_ID_AND_COMMA)
#endif /* cros_ec_fans */
		FAN_CH_COUNT
};

BUILD_ASSERT(FAN_CH_COUNT == CONFIG_PLATFORM_EC_NUM_FANS);

/* Data structure to define PWM and tachometer. */
struct fan_config {
	struct pwm_dt_spec pwm;

	const struct device *tach;
};

#ifdef CONFIG_FAN_DYNAMIC_CONFIG
extern struct fan_config fan_config[FAN_CH_COUNT];
#endif

#endif /* CONFIG_PLATFORM_EC_FAN */
#endif /* CONFIG_ZEPHYR */

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
 * For other implementations in chip layer (mchp), there is no
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

/* Fan mode */
enum fan_mode {
	/* FAN rpm mode */
	FAN_RPM = 0,
	/* FAN duty mode */
	FAN_DUTY,
};

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
	uint8_t rpm_deviation;
};

/* Characteristic of each physical fan */
struct fan_t {
	const struct fan_conf *conf;
	const struct fan_rpm *rpm;
};

/* Fan status data structure */
struct fan_data {
	/* Fan mode */
	enum fan_mode current_fan_mode;
	/* Actual rpm */
	int rpm_actual;
	/* Previous rpm */
	int rpm_pre;
	/* Target rpm */
	int rpm_target;
	/* Fan config flags */
	unsigned int flags;
	/* Automatic fan status */
	enum fan_status auto_status;
	/* Current PWM duty cycle percentage */
	int pwm_percent;
	/* Whether the PWM channel is enabled */
	bool pwm_enabled;
};

/* Values for .flags field */
/*   Enable automatic RPM control using tach input */
#define FAN_USE_RPM_MODE BIT(0)
/*   Require a higher duty cycle to start up than to keep running */
#define FAN_USE_FAST_START BIT(1)

/* The list of fans is instantiated in board.c. */
#ifdef CONFIG_FAN_DYNAMIC
extern struct fan_t fans[];
#else
extern const struct fan_t fans[];
#endif

/* For convenience */
#define FAN_CH(fan) fans[fan].conf->ch
/* Calculate temp_ratio as a macro. common/thermal.c defines the same
 * function, but it cannot be used at file scope.
 */
#define THERMAL_FAN_PERCENT(low, high, cur)                   \
	(((low) < (cur) && (cur) < (high)) ?                  \
		 (100 * ((cur) - (low)) / ((high) - (low))) : \
		 ((cur) <= (low) ? 0 : 100))
/* Convert a temperature in centigrade to a temp_ratio, assuming constants
 * temp_fan_off, temp_fan_max, already in Kelvin.  Helpful for fan tables.
 */
#define TEMP_TO_RATIO(temp_c) \
	(THERMAL_FAN_PERCENT((temp_fan_off), (temp_fan_max), (C_TO_K(temp_c))))

/**
 * Set the amount of active cooling needed. The thermal control task will call
 * this frequently, and the fan control logic will attempt to provide it.
 *
 * @param fan   Fan number (index into fans[])
 * @param pct   Percentage of cooling effort needed (0 - 100)
 */
void fan_set_percent_needed(int fan, int pct);

/**
 * Convert temp_ratio (temperature as a percentage of the ec_thermal_config
 * .temp_fan_off to .temp_fan_max range, also cooling effort needed) into a
 * target fan RPM.
 * The default implementation should be sufficient for most needs, but
 * individual boards may provide a custom version if needed (see config.h).
 *
 * @param fan          Fan number (index into fans[])
 * @param temp_ratio   Temperature as fraction of temp_fan_off to
 *                     temp_fan_max range, expressed as a percent ([0,100]).
 * Return              Target RPM for fan
 */
int fan_percent_to_rpm(int fan, int temp_ratio);
/* Data structure to hold a tuple of parameters for one sensor and one fan. */
struct fan_step_1_1 {
	/* lowest temp_ratio (exclusive) to apply this rpm when decreasing.
	 * Use this rpm until temp_ratio falls to or below this threshold.
	 */
	int decreasing_temp_ratio_threshold;
	/* lowest temp_ratio (inclusive) to apply this rpm when increasing.
	 * Use this rpm when temp_ratio exceeds this threshold.
	 */
	int increasing_temp_ratio_threshold;
	int rpm;
};
/**
 * Convert temp_ratio (temperature as a percentage of the ec_thermal_config
 * .temp_fan_off to .temp_fan_max range) into a target fan RPM.
 *
 * This function adapts the most popular custom version of fan_percent_to_rpm,
 * which provides hysteresis to reduce temperature/fan speed oscillations.
 *
 * To refactor to this, convert the fan_step-based fan_table to fan_step_1_1 by
 * removing the first (.rpm = 0) element and using
 * decreasing/increasing_temp_ratio_threshold for off/on respectively.
 * See example in ../test/fan.c.
 *
 * @param fan_table        Pointer to ordered array of fan_step_1_1 structs.
 *                         There is no need to have any element with .rpm = 0.
 *                         Function assumes 0 when temp_ratio is below the
 *                         thresholds in the index-0 element.
 * @param num_fan_levels   Size of fan_table
 * @param fan_index        Fan number (index into fans[])
 * @param temp_ratio       Temperature as fraction of temp_fan_off to
 *                         temp_fan_max range, expressed as a percent ([0,100]).
 * @param on_change        Pointer to function to be run when the target fan
 *                         rpm changes, such as ezkinil board_print_temps().
 * Return                  Target RPM for fan
 */
int temp_ratio_to_rpm_hysteresis(const struct fan_step_1_1 *fan_table,
				 int num_fan_levels, int fan_index,
				 int temp_ratio, void (*on_change)(void));

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

enum fan_status fan_get_status(int ch);

/* Initialize the HW according to the desired flags */
void fan_channel_setup(int ch, unsigned int flags);

int fan_get_count(void);

void fan_set_count(int count);

int is_thermal_control_enabled(int idx);

#ifdef CONFIG_ZEPHYR
extern struct fan_data fan_data[];

/**
 * This function sets PWM duty based on target RPM.
 *
 * The target and current RPM values in fan_data entry that
 * corresponds to selected fan has to be updated before this
 * function is called.
 *
 * @param ch    Fan number (index into fan_data[] and fans[])
 * Return       Fan status (see fan_status enum definition)
 */
enum fan_status board_override_fan_control_duty(int ch);
#endif

#ifdef __cplusplus
}
#endif

#endif /* __CROS_EC_FAN_H */
