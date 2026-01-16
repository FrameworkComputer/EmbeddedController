/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Quartz Fan configuration */

#include "chipset.h"
#include "common.h"
#include "fan.h"
#include "power/qcom.h"

#define FAN_1_CH 0
#define FAN_2_CH 1

/**
 * Override default fan control.
 *
 * On Quartz, Disable Fan control when AP boots for charging.
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

void board_override_fan_control(int fan, int *temp)
{
	/*
	 * In common/fan.c pwm_fan_stop() will turn off fan
	 * when chipset suspend or shutdown.
	 *
	 * This is temporary workaround before the Fan table is ready.
	 */
	if (chipset_in_state(CHIPSET_STATE_ON)) {
		fan_set_rpm_mode(FAN_1_CH, true);
		fan_set_rpm_target(FAN_1_CH, 6000);
		fan_set_rpm_mode(FAN_2_CH, true);
		fan_set_rpm_target(FAN_2_CH, 5000);
	}
}
