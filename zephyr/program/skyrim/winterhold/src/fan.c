/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "console.h"
#include "fan.h"
#include "math_util.h"
#include "thermal.h"

#include <zephyr/kernel.h>

#define CPRINTS(format, args...) cprints(CC_THERMAL, format, ##args)
#define CPRINTF(format, args...) cprintf(CC_THERMAL, format, ##args)

K_TIMER_DEFINE(grace_period_timer, NULL, NULL);

enum fan_status board_override_fan_control_duty(int ch)
{
	int duty, rpm_diff, deviation, duty_step;
	struct fan_data *data = &fan_data[ch];
	int rpm_actual = data->rpm_actual;
	int rpm_target = data->rpm_target;

	/* This works with one fan only. */
	if (ch != 0) {
		CPRINTS("Only FAN0 is supported!");
		return FAN_STATUS_FRUSTRATED;
	}

	/* Wait for fan RPM to catch up after its duty has been changed. */
	if (k_timer_remaining_ticks(&grace_period_timer) != 0)
		return FAN_STATUS_LOCKED;

	duty = fan_get_duty(ch);
	if (duty == 0 && rpm_target == 0)
		return FAN_STATUS_STOPPED;

	/*
	 * If the current RPM is close enough to the target just leave it.
	 * It's always going to fluctuate a bit anyway.
	 */
	deviation = fans[ch].rpm->rpm_deviation * rpm_target / 100;
	rpm_diff = rpm_target - rpm_actual;
	if (rpm_diff > deviation) {
		/* Can't set duty higher than 100%... */
		if (duty == 100)
			return FAN_STATUS_FRUSTRATED;
	} else if (rpm_diff < -deviation) {
		/* Can't set duty lower than 1%... */
		if (duty == 1 && rpm_target != 0)
			return FAN_STATUS_FRUSTRATED;
	} else {
		return FAN_STATUS_LOCKED;
	}

	/*
	 * The rpm_diff -> duty_step conversion is specific to a specific
	 * whiterun fan.
	 * It has been determined empirically.
	 */
	if (ABS(rpm_diff) >= 2500) {
		duty_step = 35;
		k_timer_start(&grace_period_timer, K_MSEC(800), K_NO_WAIT);
	} else if (ABS(rpm_diff) >= 2000) {
		duty_step = 28;
		k_timer_start(&grace_period_timer, K_MSEC(800), K_NO_WAIT);
	} else if (ABS(rpm_diff) >= 1000) {
		duty_step = 14;
		k_timer_start(&grace_period_timer, K_MSEC(800), K_NO_WAIT);
	} else if (ABS(rpm_diff) >= 500) {
		duty_step = 6;
		k_timer_start(&grace_period_timer, K_MSEC(800), K_NO_WAIT);
	} else if (ABS(rpm_diff) >= 250) {
		duty_step = 3;
		k_timer_start(&grace_period_timer, K_MSEC(600), K_NO_WAIT);
	} else {
		duty_step = 1;
		k_timer_start(&grace_period_timer, K_MSEC(600), K_NO_WAIT);
	}

	if (rpm_diff > 0)
		duty = MIN(duty + duty_step, 100);
	else
		duty = MAX(duty - duty_step, 1);

	fan_set_duty(ch, duty);

	return FAN_STATUS_CHANGING;
}
