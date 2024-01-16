/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "console.h"
#include "fan.h"
#include "math_util.h"
#include "thermal.h"
#include "amd_stt.h"

#include <zephyr/kernel.h>

#define CONFIG_FAN_START_DUTY 15

#define CPRINTS(format, args...) cprints(CC_THERMAL, format, ##args)
#define CPRINTF(format, args...) cprintf(CC_THERMAL, format, ##args)


enum fan_status board_override_fan_control_duty(int ch)
{
	struct fan_data *data = &fan_data[ch];
	int duty, new_duty, rpm_diff, duty_step;
	int rpm_actual = data->rpm_actual;
	int rpm_target = data->rpm_target;
	int deviation = fans[ch].rpm->rpm_deviation;

	duty = fan_get_duty(ch);
	if (duty == 0 && rpm_target == 0) {
		return FAN_STATUS_STOPPED;
	}

	if (duty > 0) {
		if (duty < 20) {
			deviation = 10;
		} else if (duty < 35) {
			deviation = 7;
		}
	}

	/* wait rpm is stable */
	if (ABS(rpm_actual - data->rpm_pre) > (rpm_target * deviation / 100)) {
		data->rpm_pre = rpm_actual;
		return FAN_STATUS_CHANGING;
	}

	/* Record previous rpm */
	data->rpm_pre = rpm_actual;

	/*
	 * A specific type of fan needs a longer time to output the TACH
	 * signal to EC after EC outputs the PWM signal to the fan.
	 * During this period, the driver will read two consecutive RPM = 0.
	 * In this case, don't step the PWM duty too aggressively.
	 * We subtract 200 from the start RPM as margin.
	 */
	if (rpm_actual < (fans[ch].rpm->rpm_min  - 200)) {
		fan_set_duty(ch, CONFIG_FAN_START_DUTY);
		return FAN_STATUS_CHANGING;
	}

	rpm_diff = rpm_target - rpm_actual;

	if (rpm_diff > (rpm_target * deviation / 100)) {
		/* Can't set duty higher than 100%... */
		if (duty == 100)
			return FAN_STATUS_FRUSTRATED;
	} else if (rpm_diff < -(rpm_target * deviation / 100)) {
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
	if (ABS(rpm_diff) >= 2100) {
		duty_step = 28;
	} else if (ABS(rpm_diff) >= 1100) {
		duty_step = 14;
	} else if (ABS(rpm_diff) >= 550) {
		duty_step = 6;
	} else if (ABS(rpm_diff) >= 300) {
		duty_step = 3;
	} else if (ABS(rpm_diff) >= 150) {
		duty_step = 2;
	} else {
		duty_step = 1;
	}

	if (rpm_diff > 0)
		new_duty = MIN(duty + duty_step, 100);
	else
		new_duty = MAX(duty - duty_step, 1);

	fan_set_duty(ch, new_duty);

	return FAN_STATUS_CHANGING;
}
