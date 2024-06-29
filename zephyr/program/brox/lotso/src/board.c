/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "accelgyro.h"
#include "common.h"
#include "cros_cbi.h"
#include "driver/accel_bma4xx.h"
#include "driver/accel_lis2dw12_public.h"
#include "hooks.h"
#include "motion_sense.h"
#include "motionsense_sensors.h"

static bool lid_uses_lis2dw12;

void motion_interrupt(enum gpio_signal signal)
{
	if (lid_uses_lis2dw12) {
		lis2dw12_interrupt(signal);
	} else {
		bma4xx_interrupt(signal);
	}
}

static void alt_sensor_init(void)
{
	lid_uses_lis2dw12 = cros_cbi_ssfc_check_match(
		CBI_SSFC_VALUE_ID(DT_NODELABEL(lid_sensor_lis2dw12)));

	motion_sensors_check_ssfc();
}
DECLARE_HOOK(HOOK_INIT, alt_sensor_init, HOOK_PRIO_POST_I2C);
