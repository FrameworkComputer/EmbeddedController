/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Quenbi Fan configuration */

#include "common.h"
#include "fan.h"
#include "power/qcom.h"

/*
 * Set Fan Duty cycle to 50%. Anything less than 50% may
 * result in the fan not spinning, which would prevent us
 * from being able to read the RPM from the tachometer.
 */
#define FAN_STALLED_DUTY_CYCLE 50

/**
 * Override default fan control.
 *
 * On Quenbi, the TACH signal's pull-up rail (+3V_VREG_MISC)
 * takes time to stabilize after AP power-on. During this
 * time, the TACH reports 0 RPM.
 *
 * This function locks the fan to a moderate duty cycle
 * until a valid RPM is detected, preventing the default
 * logic from interpreting the 0 RPM as a stall and
 * ramping the fan to 100%.
 */
enum fan_status board_override_fan_control_duty(int ch)
{
	/* If the last power-on is due to AC, which enters the charging loop in
	 * the AP firmware stop the fan */
	if (POWER_ON_BY_AC_ON == chipset_get_power_on_reason()) {
		fan_set_duty(ch, 0);
		return FAN_STATUS_STOPPED;
	}

	/* Wait for tachometer to report a valid RPM. */
	if (!fan_get_rpm_actual(ch)) {
		/*
		 * Set a moderate duty cycle while we wait for the
		 * +3V_VREG_MISC rail to power on and pull-up the TACH
		 * sensor.
		 */
		fan_set_duty(ch, FAN_STALLED_DUTY_CYCLE);
		return FAN_STATUS_LOCKED;
	}
	/* Valid RPM detected, return to default to RPM mode logic */
	fan_set_rpm_mode(ch, true);
	fan_set_rpm_target(ch, 6000);

	return FAN_STATUS_LOCKED;
}
