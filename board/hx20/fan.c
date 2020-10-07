/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* MCHP MEC fan control module. */

/* This assumes 2-pole fan. For each rotation, 5 edges are measured. */


#include "common.h"
#include "console.h"
#include "fan.h"
#include "gpio.h"
#include "hooks.h"
#include "pwm.h"
#include "pwm_chip.h"
#include "registers.h"
#include "tfdp_chip.h"
#include "util.h"


#define CPRINTS(format, args...) cprints(CC_THERMAL, format, ## args)
#define CPRINTF(format, args...) cprintf(CC_THERMAL, format, ## args)

/* Maximum tach reading/target value */
#define MAX_TACH 0x1fff

/* Tach target value for disable fan */
#define FAN_OFF_TACH 0xfff8

/*
 * RPM = (n - 1) * m * f * 60 / poles / TACH
 *   n = number of edges = 5
 *   m = multiplier defined by RANGE = 2 in our case
 *   f = 32.768K
 *   poles = 2
 */
#define RPM_TO_TACH(rpm) MIN((7864320 / MAX((rpm), 1)), MAX_TACH)
#define TACH_TO_RPM(tach) (7864320 / MAX((tach), 1))

static int rpm_setting;
static int duty_setting;
static int in_rpm_mode = 1;

void fan_set_enabled(int ch, int enabled)
{
	if (in_rpm_mode) {
		if (enabled) {
			fan_set_rpm_target(ch, rpm_setting);
			pwm_enable(ch, enabled);
			pwm_set_duty(ch, rpm_setting);
		}
	} else {
		if (enabled) {
			pwm_enable(ch, enabled);
			pwm_set_duty(ch, duty_setting);
		}
	else {
			pwm_enable(ch, enabled);
			pwm_set_duty(ch, 0);
		}
	}
}

int fan_get_enabled(int ch)
{
	return pwm_get_enabled(ch);
}

int fan_rpm_to_percent(int fan, int rpm)
{
	int pct, max, min;

	if (!rpm) {
		pct = 0;
	} else {
		min = fans[fan].rpm->rpm_min;
		max = fans[fan].rpm->rpm_max;

		if (rpm < min)
			rpm = min;
		else if (rpm > max)
			rpm = max;

		pct = (rpm - min) / ((max - min) / 100);
		CPRINTS(" Fan max min : %d , %d", max, min);
	}
	CPRINTS(" Fan PCT = %d ", pct);
	return pct;
}

void fan_set_duty(int ch, int percent)
{
	if (percent < 0)
		percent = 0;
	else if (percent > 100)
		percent = 100;

	duty_setting = percent;

	pwm_set_duty(ch, percent);
}

int fan_get_duty(int ch)
{
	return duty_setting;
}

int fan_get_rpm_mode(int ch)
{
	return in_rpm_mode;
}

void fan_set_rpm_mode(int ch, int rpm_mode)
{
	in_rpm_mode = rpm_mode;
}

int fan_get_rpm_actual(int ch)
{
	return pwm_get_duty(ch);
}

int fan_get_rpm_target(int ch)
{
	return rpm_setting;
}

void fan_set_rpm_target(int ch, int rpm)
{
	int pct = 0;

	pct = fan_rpm_to_percent(ch, rpm);
	rpm_setting = rpm;
	pwm_set_duty(ch, pct);
}

enum fan_status fan_get_status(int ch)
{
	/* TODO */
	return 1;
}

int fan_is_stalled(int ch)
{
	/* Must be enabled with non-zero target to stall */
	if (!fan_get_enabled(ch) || fan_get_rpm_target(ch) == 0)
		return 0;

	/* Check for stall condition */
	return fan_get_status(ch) == FAN_STATUS_STOPPED;
}

void fan_channel_setup(int ch, unsigned int flags)
{
	int i;

	for (i = 0; i < FAN_CH_COUNT; ++i) {
		pwm_slp_en(pwm_channels[i].channel, 0);
		pwm_configure(pwm_channels[i].channel,
			      pwm_channels[i].flags & PWM_CONFIG_ACTIVE_LOW,
			      pwm_channels[i].flags & PWM_CONFIG_ALT_CLOCK);
		pwm_set_duty(i, 0);
	}
}
