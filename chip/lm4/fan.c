/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* LM4 fan control module. */

#include "clock.h"
#include "fan.h"
#include "gpio.h"
#include "hooks.h"
#include "registers.h"
#include "util.h"

/* Maximum RPM for fan controller */
#define MAX_RPM 0x1fff

/* Maximum PWM for PWM controller */
#define MAX_PWM 0x1ff

/*
 * Scaling factor for requested/actual RPM for CPU fan.  We need this because
 * the fan controller on Blizzard filters tach pulses that are less than 64
 * 15625Hz ticks apart, which works out to ~7000rpm on an unscaled fan.  By
 * telling the controller we actually have twice as many edges per revolution,
 * the controller can handle fans that actually go twice as fast.  See
 * crosbug.com/p/7718.
 */
#define RPM_SCALE 2


void fan_set_enabled(int ch, int enabled)
{
	if (enabled)
		LM4_FAN_FANCTL |= (1 << ch);
	else
		LM4_FAN_FANCTL &= ~(1 << ch);
}

int fan_get_enabled(int ch)
{
	return (LM4_FAN_FANCTL & (1 << ch)) ? 1 : 0;
}

void fan_set_duty(int ch, int percent)
{
	int duty;

	if (percent < 0)
		percent = 0;
	else if (percent > 100)
		percent = 100;

	duty = (MAX_PWM * percent + 50) / 100;

	/* Always enable the channel */
	fan_set_enabled(ch, 1);

	/* Set the duty cycle */
	LM4_FAN_FANCMD(ch) = duty << 16;
}

int fan_get_duty(int ch)
{
	return ((LM4_FAN_FANCMD(ch) >> 16) * 100 + MAX_PWM / 2) / MAX_PWM;
}

int fan_get_rpm_mode(int ch)
{
	return (LM4_FAN_FANCH(ch) & 0x0001) ? 0 : 1;
}

void fan_set_rpm_mode(int ch, int rpm_mode)
{
	int was_enabled = fan_get_enabled(ch);
	int was_rpm = fan_get_rpm_mode(ch);

	if (!was_rpm && rpm_mode) {
		/* Enable RPM control */
		fan_set_enabled(ch, 0);
		LM4_FAN_FANCH(ch) &= ~0x0001;
		fan_set_enabled(ch, was_enabled);
	} else if (was_rpm && !rpm_mode) {
		/* Disable RPM mode */
		fan_set_enabled(ch, 0);
		LM4_FAN_FANCH(ch) |= 0x0001;
		fan_set_enabled(ch, was_enabled);
	}
}

int fan_get_rpm_actual(int ch)
{
	return (LM4_FAN_FANCST(ch) & MAX_RPM) * RPM_SCALE;
}

int fan_get_rpm_target(int ch)
{
	return (LM4_FAN_FANCMD(ch) & MAX_RPM) * RPM_SCALE;
}

void fan_set_rpm_target(int ch, int rpm)
{
	/* Apply fan scaling */
	if (rpm > 0)
		rpm /= RPM_SCALE;

	/* Treat out-of-range requests as requests for maximum fan speed */
	if (rpm < 0 || rpm > MAX_RPM)
		rpm = MAX_RPM;

	LM4_FAN_FANCMD(ch) = rpm;
}

/* The LM4 status is the original definition of enum fan_status */
enum fan_status fan_get_status(int ch)
{
	return (LM4_FAN_FANSTS >> (2 * ch)) & 0x03;
}

/**
 * Return non-zero if fan is enabled but stalled.
 */
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
	uint32_t init;

	if (flags & FAN_USE_RPM_MODE)
		/*
		 * Configure automatic/feedback mode:
		 * 0x8000 = bit 15     = auto-restart
		 * 0x0000 = bit 14     = slow acceleration
		 * 0x0000 = bits 13:11 = no hysteresis
		 * 0x0000 = bits 10:8  = start period (2<<0) edges
		 * 0x0000 = bits 7:6   = no fast start
		 * 0x0020 = bits 5:4   = average 4 edges when
		 *                       calculating RPM
		 * 0x000c = bits 3:2   = 8 pulses per revolution
		 *                       (see note at top of file)
		 * 0x0000 = bit 0      = automatic control
		 */
		init = 0x802c;
	else
		/*
		 * Configure drive-only mode:
		 * 0x0000 = bit 15     = no auto-restart
		 * 0x0000 = bit 14     = slow acceleration
		 * 0x0000 = bits 13:11 = no hysteresis
		 * 0x0000 = bits 10:8  = start period (2<<0) edges
		 * 0x0000 = bits 7:6   = no fast start
		 * 0x0000 = bits 5:4   = no RPM averaging
		 * 0x0000 = bits 3:2   = 1 pulses per revolution
		 * 0x0001 = bit 0      = manual control
		 */
		init = 0x0001;

	if (flags & FAN_USE_FAST_START)
		/*
		 * Configure fast-start mode
		 * 0x0000 = bits 10:8  = start period (2<<0) edges
		 * 0x0040 = bits 7:6   = fast start at 50% duty
		 */
		init |= 0x0040;

	LM4_FAN_FANCH(ch) = init;
}

static void fan_init(void)
{

#ifdef CONFIG_PWM_DSLEEP

	/* Enable the fan module and delay a few clocks */
	clock_enable_peripheral(CGC_OFFSET_FAN, 0x1, CGC_MODE_ALL);

#else

	/* Enable the fan module and delay a few clocks */
	clock_enable_peripheral(CGC_OFFSET_FAN, 0x1,
			CGC_MODE_RUN | CGC_MODE_SLEEP);
#endif
	/* Disable all fans */
	LM4_FAN_FANCTL = 0;
}
DECLARE_HOOK(HOOK_INIT, fan_init, HOOK_PRIO_INIT_PWM);
