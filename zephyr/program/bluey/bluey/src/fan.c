/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Bluey Fan configuration */

#include "common.h"
#include "fan.h"
#include "power/qcom.h"

/**
 * Override default fan control.
 *
 * On Bluey, Disable Fan control when AP boots for charging.
 */
enum fan_status board_override_fan_control_duty(int ch)
{
	/* If the last power-on is due to AC, which enters the charging loop in
	 * the AP firmware stop the fan */
	if (POWER_ON_BY_AC_ON == chipset_get_power_on_reason()) {
		fan_set_duty(ch, 0);
		return FAN_STATUS_STOPPED;
	}

	return fan_smart_control(ch);
}
