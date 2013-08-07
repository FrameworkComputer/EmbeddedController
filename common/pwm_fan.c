/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "fan.h"

#ifndef CONFIG_PWM_FAN_RPM_CUSTOM
/* This is the default implementation. It's only called over [0,100].
 * Convert the percentage to a target RPM. We can't simply scale all
 * the way down to zero because most fans won't turn that slowly, so
 * we'll map [1,100] => [FAN_MIN,FAN_MAX], and [0] => "off".
*/
int pwm_fan_percent_to_rpm(int pct)
{
	int rpm;

	if (!pct)
		rpm = 0;
	else
		rpm = ((pct - 1) * CONFIG_PWM_FAN_RPM_MAX +
		       (100 - pct) * CONFIG_PWM_FAN_RPM_MIN) / 99;

	return rpm;
}
#endif	/* CONFIG_PWM_FAN_RPM_CUSTOM */
