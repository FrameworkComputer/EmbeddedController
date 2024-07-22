/* Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * TODO(b/272518464): Work around coreboot GCC preprocessor bug.
 * #line marks the *next* line, so it is off by one.
 */
#line 11

#define DT_DRV_COMPAT cros_ec_fans

#include "fan.h"
#include "gpio_signal.h"
#include "hooks.h"
#include "math_util.h"
#include "system.h"
#include "util.h"

#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/pwm.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util_macro.h>

LOG_MODULE_REGISTER(fan_shim, LOG_LEVEL_ERR);

BUILD_ASSERT(DT_NUM_INST_STATUS_OKAY(DT_DRV_COMPAT) == 1,
	     "Exactly one instance of cros-ec,fans should be defined.");

#define FAN_CONFIGS(node_id)                                                   \
	const struct fan_conf node_id##_conf = {                               \
		.flags = (COND_CODE_1(DT_PROP(node_id, not_use_rpm_mode), (0), \
				      (FAN_USE_RPM_MODE))) |                   \
			 (COND_CODE_1(DT_PROP(node_id, use_fast_start),        \
				      (FAN_USE_FAST_START), (0))),             \
		.ch = node_id,                                                 \
		.pgood_gpio = COND_CODE_1(                                     \
			DT_NODE_HAS_PROP(node_id, pgood_gpio),                 \
			(GPIO_SIGNAL(DT_PHANDLE(node_id, pgood_gpio))),        \
			(GPIO_UNIMPLEMENTED)),                                 \
		.enable_gpio = COND_CODE_1(                                    \
			DT_NODE_HAS_PROP(node_id, enable_gpio),                \
			(GPIO_SIGNAL(DT_PHANDLE(node_id, enable_gpio))),       \
			(GPIO_UNIMPLEMENTED)),                                 \
	};                                                                     \
	const struct fan_rpm node_id##_rpm = {                                 \
		.rpm_min = DT_PROP(node_id, rpm_min),                          \
		.rpm_start = DT_PROP(node_id, rpm_start),                      \
		.rpm_max = DT_PROP(node_id, rpm_max),                          \
		.rpm_deviation = DT_PROP(node_id, rpm_deviation),              \
	};

#define FAN_INST(node_id)                \
	[node_id] = {                    \
		.conf = &node_id##_conf, \
		.rpm = &node_id##_rpm,   \
	},

#define FAN_CONTROL_INST(node_id)                                 \
	[node_id] = {                                             \
		.pwm = PWM_DT_SPEC_GET(node_id),                  \
		.tach = DEVICE_DT_GET(DT_PHANDLE(node_id, tach)), \
	},

DT_INST_FOREACH_CHILD(0, FAN_CONFIGS)

const struct fan_t fans[FAN_CH_COUNT] = { DT_INST_FOREACH_CHILD(0, FAN_INST) };

struct fan_data fan_data[FAN_CH_COUNT];

#ifndef CONFIG_FAN_DYNAMIC_CONFIG
const
#endif
	struct fan_config fan_config[FAN_CH_COUNT] = { DT_INST_FOREACH_CHILD(
		0, FAN_CONTROL_INST) };

static void fan_pwm_update(int ch)
{
	const struct fan_config *cfg = &fan_config[ch];
	struct fan_data *data = &fan_data[ch];
	const struct device *pwm_dev = cfg->pwm.dev;
	uint32_t pulse_ns;
	int ret;

	if (!device_is_ready(pwm_dev)) {
		LOG_ERR("device %s not ready", pwm_dev->name);
		return;
	}

	if (data->pwm_enabled) {
		pulse_ns = DIV_ROUND_NEAREST(
			cfg->pwm.period * data->pwm_percent, 100);
	} else {
		pulse_ns = 0;
	}

	LOG_DBG("FAN PWM %s set percent (%d), pulse %d", pwm_dev->name,
		data->pwm_percent, pulse_ns);

	ret = pwm_set_pulse_dt(&cfg->pwm, pulse_ns);
	if (ret) {
		LOG_ERR("pwm_set_pulse_dt failed %s (%d)", pwm_dev->name, ret);
	}
}

/**
 * Get fan rpm value
 *
 * @param   ch      operation channel
 * @return          Actual rpm
 */
static int fan_rpm(int ch)
{
	const struct device *dev = fan_config[ch].tach;
	struct sensor_value val = { 0 };

	if (!device_is_ready(dev)) {
		LOG_ERR("device %s not ready", dev->name);
		return 0;
	}

	sensor_sample_fetch_chan(dev, SENSOR_CHAN_RPM);
	sensor_channel_get(dev, SENSOR_CHAN_RPM, &val);

	return (int)val.val1;
}

/**
 * Check all fans are stopped
 *
 * @return   1: all fans are stopped. 0: else.
 */
static int fan_all_disabled(void)
{
	int ch;

	for (ch = 0; ch < fan_get_count(); ch++) {
		if (fan_data[ch].auto_status != FAN_STATUS_STOPPED) {
			return 0;
		}
	}
	return 1;
}

/**
 * Adjust fan duty by difference between target and actual rpm
 *
 * @param   ch        operation channel
 * @param   rpm_diff  difference between target and actual rpm
 * @param   duty      current fan duty
 */
static void fan_adjust_duty(int ch, int rpm_diff, int duty)
{
	int duty_step = 0;

	/* Find suitable duty step */
	if (ABS(rpm_diff) >= 2000) {
		duty_step = 20;
	} else if (ABS(rpm_diff) >= 1000) {
		duty_step = 10;
	} else if (ABS(rpm_diff) >= 500) {
		duty_step = 5;
	} else if (ABS(rpm_diff) >= 250) {
		duty_step = 3;
	} else {
		duty_step = 1;
	}

	/* Adjust fan duty step by step */
	if (rpm_diff > 0) {
		duty = MIN(duty + duty_step, 100);
	} else {
		duty = MAX(duty - duty_step, 1);
	}

	fan_set_duty(ch, duty);

	LOG_DBG("fan%d: duty %d, rpm_diff %d", ch, duty, rpm_diff);
}

/**
 * Smart fan control function.
 *
 * The function sets the pwm duty to reach the target rpm
 *
 * @param   ch         operation channel
 */
enum fan_status fan_smart_control(int ch)
{
	struct fan_data *data = &fan_data[ch];
	int duty, rpm_diff;
	int rpm_actual = data->rpm_actual;
	int rpm_target = data->rpm_target;
	int deviation = fans[ch].rpm->rpm_deviation;

	/* wait rpm is stable */
	if (ABS(rpm_actual - data->rpm_pre) > (rpm_target * deviation / 100)) {
		data->rpm_pre = rpm_actual;
		return FAN_STATUS_CHANGING;
	}

	/* Record previous rpm */
	data->rpm_pre = rpm_actual;

	/* Adjust PWM duty */
	rpm_diff = rpm_target - rpm_actual;
	duty = fan_get_duty(ch);
	if (duty == 0 && rpm_target == 0) {
		return FAN_STATUS_STOPPED;
	}

	if (rpm_diff > (rpm_target * deviation / 100)) {
		/* Increase PWM duty */
		if (duty == 100) {
			return FAN_STATUS_FRUSTRATED;
		}

		fan_adjust_duty(ch, rpm_diff, duty);
		return FAN_STATUS_CHANGING;
	} else if (rpm_diff < -(rpm_target * deviation / 100)) {
		/* Decrease PWM duty */
		if (duty == 1 && rpm_target != 0) {
			return FAN_STATUS_FRUSTRATED;
		}

		fan_adjust_duty(ch, rpm_diff, duty);
		return FAN_STATUS_CHANGING;
	}

	return FAN_STATUS_LOCKED;
}

static void fan_tick_func_rpm(int ch)
{
	struct fan_data *data = &fan_data[ch];

	if (!fan_get_enabled(ch))
		return;

	/* Get actual rpm */
	data->rpm_actual = fan_rpm(ch);

	/* TODO: b/279132492 */
#ifdef CONFIG_PLATFORM_EC_CUSTOM_FAN_DUTY_CONTROL
	data->auto_status = board_override_fan_control_duty(ch);
#else
	/* Do smart fan stuff */
	data->auto_status = fan_smart_control(ch);
#endif
}

static void fan_tick_func_duty(int ch)
{
	struct fan_data *data = &fan_data[ch];

	/* Fan in duty mode still want rpm_actual being updated. */
	if (data->flags & FAN_USE_RPM_MODE) {
		data->rpm_actual = fan_rpm(ch);
		if (data->rpm_actual > 0) {
			data->auto_status = FAN_STATUS_LOCKED;
		} else {
			data->auto_status = FAN_STATUS_STOPPED;
		}
	} else {
		if (fan_get_duty(ch) > 0) {
			data->auto_status = FAN_STATUS_LOCKED;
		} else {
			data->auto_status = FAN_STATUS_STOPPED;
		}
	}
}

void fan_tick_func(void)
{
	int ch;

	for (ch = 0; ch < fan_get_count(); ch++) {
		switch (fan_data[ch].current_fan_mode) {
		case FAN_RPM:
			fan_tick_func_rpm(ch);
			break;
		case FAN_DUTY:
			fan_tick_func_duty(ch);
			break;
		default:
			LOG_ERR("Invalid fan %d mode: %d", ch,
				fan_data[ch].current_fan_mode);
		}
	}
}
DECLARE_HOOK(HOOK_TICK, fan_tick_func, HOOK_PRIO_DEFAULT);

int fan_get_duty(int ch)
{
	return fan_data[ch].pwm_percent;
}

int fan_get_rpm_mode(int ch)
{
	return fan_data[ch].current_fan_mode == FAN_RPM ? 1 : 0;
}

void fan_set_rpm_mode(int ch, int rpm_mode)
{
	struct fan_data *data = &fan_data[ch];

	if (rpm_mode && (data->flags & FAN_USE_RPM_MODE)) {
		data->current_fan_mode = FAN_RPM;
	} else {
		data->current_fan_mode = FAN_DUTY;
	}
}

int fan_get_rpm_actual(int ch)
{
	/* Check PWM is enabled first */
	if (fan_get_duty(ch) == 0) {
		return 0;
	}

	LOG_DBG("fan %d: get actual rpm = %d", ch, fan_data[ch].rpm_actual);
	return fan_data[ch].rpm_actual;
}

int fan_get_enabled(int ch)
{
	return fan_data[ch].pwm_enabled;
}

void fan_set_enabled(int ch, int enabled)
{
	if (!enabled) {
		fan_data[ch].auto_status = FAN_STATUS_STOPPED;
	}

	fan_data[ch].pwm_enabled = enabled;

	fan_pwm_update(ch);
}

void fan_channel_setup(int ch, unsigned int flags)
{
	struct fan_data *data = &fan_data[ch];

	data->flags = flags;
	/* Set default fan states */
	data->current_fan_mode = FAN_DUTY;
	data->auto_status = FAN_STATUS_STOPPED;
}

void fan_set_duty(int ch, int percent)
{
	/* duty is zero */
	if (!percent) {
		fan_data[ch].auto_status = FAN_STATUS_STOPPED;
		if (fan_all_disabled()) {
			enable_sleep(SLEEP_MASK_FAN);
		}
	} else {
		disable_sleep(SLEEP_MASK_FAN);
	}

	fan_data[ch].pwm_percent = percent;

	fan_pwm_update(ch);
}

int fan_get_rpm_target(int ch)
{
	return fan_data[ch].rpm_target;
}

enum fan_status fan_get_status(int ch)
{
	return fan_data[ch].auto_status;
}

void fan_set_rpm_target(int ch, int rpm)
{
	if (rpm == 0) {
		/* If rpm = 0, disable PWM immediately. Why?*/
		fan_set_duty(ch, 0);
	} else {
		/* This is the counterpart of disabling PWM above. */
		if (!fan_get_enabled(ch)) {
			fan_set_enabled(ch, 1);
		}
		if (rpm > fans[ch].rpm->rpm_max) {
			rpm = fans[ch].rpm->rpm_max;
		} else if (rpm < fans[ch].rpm->rpm_min) {
			rpm = fans[ch].rpm->rpm_min;
		}
	}

	/* Set target rpm */
	fan_data[ch].rpm_target = rpm;
	LOG_DBG("fan %d: set target rpm = %d", ch, fan_data[ch].rpm_target);
}

int fan_is_stalled(int ch)
{
	int is_pgood = 1;
	const struct gpio_dt_spec *gp =
		gpio_get_dt_spec(fans[ch].conf->enable_gpio);

	if (gp != NULL) {
		is_pgood = gpio_pin_get_dt(gp);
	}

	return fan_get_enabled(ch) && fan_get_duty(ch) &&
	       !fan_get_rpm_actual(ch) && is_pgood;
}
