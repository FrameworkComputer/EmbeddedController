/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "accelgyro.h"
#include "button.h"
#include "cros_board_info.h"
#include "cros_cbi.h"
#include "driver/accel_bma4xx.h"
#include "driver/accel_lis2dw12_public.h"
#include "driver/accelgyro_bmi323.h"
#include "driver/accelgyro_lsm6dso.h"
#include "gpio/gpio_int.h"
#include "hooks.h"
#include "motion_sense.h"
#include "motionsense_sensors.h"
#include "nissa_common.h"
#include "tablet_mode.h"

#include <zephyr/devicetree.h>
#include <zephyr/logging/log.h>

LOG_MODULE_DECLARE(nissa, CONFIG_NISSA_LOG_LEVEL);

static bool use_alt_sensor;
static bool use_alt_lid_accel;

void motion_interrupt(enum gpio_signal signal)
{
	if (use_alt_sensor)
		bmi3xx_interrupt(signal);
	else
		lsm6dso_interrupt(signal);
}

void lid_accel_interrupt(enum gpio_signal signal)
{
	if (use_alt_lid_accel)
		bma4xx_interrupt(signal);
	else
		lis2dw12_interrupt(signal);
}

static void form_factor_init(void)
{
	if (cros_cbi_ssfc_check_match(
		    CBI_SSFC_VALUE_ID(DT_NODELABEL(base_sensor_bmi323)))) {
		use_alt_sensor = true;
		MOTIONSENSE_ENABLE_ALTERNATE(alt_base_accel);
		MOTIONSENSE_ENABLE_ALTERNATE(alt_base_gyro);
		ccprints("BASE ACCEL IS BMI323");
	} else if (cros_cbi_ssfc_check_match(CBI_SSFC_VALUE_ID(
			   DT_NODELABEL(base_sensor_lsm6dso)))) {
		use_alt_sensor = false;
		ccprints("BASE ACCEL IS LSM6DSO");
	} else {
		use_alt_sensor = false;
		ccprints("no motionsense");
	}

	if (cros_cbi_ssfc_check_match(
		    CBI_SSFC_VALUE_ID(DT_NODELABEL(lid_sensor_bma422)))) {
		use_alt_lid_accel = true;
		MOTIONSENSE_ENABLE_ALTERNATE(alt_lid_accel);
		ccprints("LID SENSOR IS BMA422");
	} else if (cros_cbi_ssfc_check_match(CBI_SSFC_VALUE_ID(
			   DT_NODELABEL(lid_sensor_lis2dw12)))) {
		use_alt_lid_accel = false;
		ccprints("LID SENSOR IS LIS2DW12");
	} else {
		use_alt_lid_accel = false;
		ccprints("no lid sensor");
	}

	motion_sensors_check_ssfc();
}
DECLARE_HOOK(HOOK_INIT, form_factor_init, HOOK_PRIO_POST_I2C);
