/* Copyright 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* MEC1322 fan control module. */

/* This assumes 2-pole fan. For each rotation, 5 edges are measured. */

#include "fan.h"
#include "registers.h"
#include "util.h"

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


static void clear_status(void)
{
	/* Clear DRIVE_FAIL, FAN_SPIN, and FAN_STALL bits */
	MEC1322_FAN_STATUS = 0x23;
}

void fan_set_enabled(int ch, int enabled)
{
	if (in_rpm_mode) {
		if (enabled)
			fan_set_rpm_target(ch, rpm_setting);
		else
			MEC1322_FAN_TARGET = FAN_OFF_TACH;
	} else {
		if (enabled)
			fan_set_duty(ch, duty_setting);
		else
			MEC1322_FAN_SETTING = 0;
	}
	clear_status();
}

int fan_get_enabled(int ch)
{
	if (in_rpm_mode)
		return (MEC1322_FAN_TARGET & 0xff00) != 0xff00;
	else
		return !!MEC1322_FAN_SETTING;
}

void fan_set_duty(int ch, int percent)
{
	if (percent < 0)
		percent = 0;
	else if (percent > 100)
		percent = 100;

	duty_setting = percent;
	MEC1322_FAN_SETTING = percent * 255 / 100;
	clear_status();
}

int fan_get_duty(int ch)
{
	return duty_setting;
}

int fan_get_rpm_mode(int ch)
{
	return !!(MEC1322_FAN_CFG1 & BIT(7));
}

void fan_set_rpm_mode(int ch, int rpm_mode)
{
	if (rpm_mode)
		MEC1322_FAN_CFG1 |= BIT(7);
	else
		MEC1322_FAN_CFG1 &= ~BIT(7);
	clear_status();
}

int fan_get_rpm_actual(int ch)
{
	if ((MEC1322_FAN_READING >> 8) == 0xff)
		return 0;
	else
		return TACH_TO_RPM(MEC1322_FAN_READING >> 3);
}

int fan_get_rpm_target(int ch)
{
	return rpm_setting;
}

void fan_set_rpm_target(int ch, int rpm)
{
	rpm_setting = rpm;
	MEC1322_FAN_TARGET = RPM_TO_TACH(rpm) << 3;
	clear_status();
}

enum fan_status fan_get_status(int ch)
{
	uint8_t sts = MEC1322_FAN_STATUS;

	if (sts & (BIT(5) | BIT(1)))
		return FAN_STATUS_FRUSTRATED;
	if (fan_get_rpm_actual(ch) == 0)
		return FAN_STATUS_STOPPED;
	return FAN_STATUS_LOCKED;
}

int fan_is_stalled(int ch)
{
	uint8_t sts = MEC1322_FAN_STATUS;
	if (fan_get_rpm_actual(ch)) {
		MEC1322_FAN_STATUS = 0x1;
		return 0;
	}
	return sts & 0x1;
}

void fan_channel_setup(int ch, unsigned int flags)
{
	/*
	 * Fan configuration 1 register:
	 *   0x80 = bit 7    = RPM mode (0x00 if FAN_USE_RPM_MODE not set)
	 *   0x20 = bits 6:5 = min 1000 RPM, multiplier = 2
	 *   0x08 = bits 4:3 = 5 edges, 2 poles
	 *   0x03 = bits 2:0 = 400 ms update time
	 *
	 * Fan configuration 2 register:
	 *   0x00 = bit 6    = Ramp control disabled
	 *   0x00 = bit 5    = Glitch filter enabled
	 *   0x18 = bits 4:3 = Using both derivative options
	 *   0x02 = bits 2:1 = error range is 50 RPM
	 *   0x00 = bits 0   = normal polarity
	 */
	if (flags & FAN_USE_RPM_MODE)
		MEC1322_FAN_CFG1 = 0xab;
	else
		MEC1322_FAN_CFG1 = 0x2b;
	MEC1322_FAN_CFG2 = 0x1a;
	clear_status();
}
