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
#include "tablet_mode.h"

#include <zephyr/devicetree.h>
#include <zephyr/logging/log.h>

LOG_MODULE_DECLARE(nissa, CONFIG_NISSA_LOG_LEVEL);

enum motionsense_type {
	motion_none = 0,
	motion_bmi323,
	motion_lsm6dso,
};

enum lid_accel_type {
	lid_none = 0,
	lid_bma422,
	lid_lis2dw12,
};

static int use_sensor = motion_bmi323;
static int use_lid_accel = lid_bma422;

void motion_interrupt(enum gpio_signal signal)
{
	if (use_sensor == motion_bmi323)
		bmi3xx_interrupt(signal);
	else if (use_sensor == motion_lsm6dso)
		lsm6dso_interrupt(signal);
}

void lid_accel_interrupt(enum gpio_signal signal)
{
	if (use_lid_accel == lid_bma422)
		bma4xx_interrupt(signal);
	else if (use_lid_accel == lid_lis2dw12)
		lis2dw12_interrupt(signal);
}

static void form_factor_init(void)
{
	if (cros_cbi_ssfc_check_match(
		    CBI_SSFC_VALUE_ID(DT_NODELABEL(base_sensor_bmi323)))) {
		use_sensor = motion_bmi323;
		MOTIONSENSE_ENABLE_ALTERNATE(alt_base_accel);
		MOTIONSENSE_ENABLE_ALTERNATE(alt_base_gyro);
		ccprints("BASE ACCEL IS BMI323");
	} else if (cros_cbi_ssfc_check_match(CBI_SSFC_VALUE_ID(
			   DT_NODELABEL(base_sensor_lsm6dso)))) {
		use_sensor = motion_lsm6dso;
		ccprints("BASE ACCEL IS LSM6DSO");
	} else {
		use_sensor = motion_none;
		ccprints("no motionsense");
	}

	if (cros_cbi_ssfc_check_match(
		    CBI_SSFC_VALUE_ID(DT_NODELABEL(lid_sensor_bma422)))) {
		use_lid_accel = lid_bma422;
		MOTIONSENSE_ENABLE_ALTERNATE(alt_lid_accel);
		ccprints("LID SENSOR IS BMA422");
	} else if (cros_cbi_ssfc_check_match(CBI_SSFC_VALUE_ID(
			   DT_NODELABEL(lid_sensor_lis2dw12)))) {
		use_lid_accel = lid_lis2dw12;
		ccprints("LID SENSOR IS LIS2DW12");
	} else {
		use_lid_accel = lid_none;
		ccprints("no lid sensor");
	}

	if (use_sensor && use_lid_accel) {
		motion_sensors_check_ssfc();
	} else {
		motion_sensor_count = 0;
		gmr_tablet_switch_disable();
		gpio_disable_dt_interrupt(GPIO_INT_FROM_NODELABEL(int_imu));
		gpio_pin_configure_dt(GPIO_DT_FROM_NODELABEL(gpio_imu_int_l),
				      GPIO_DISCONNECTED);
		ccprints("Clamshell: disable motionsense function.");
	}
}
DECLARE_HOOK(HOOK_INIT, form_factor_init, HOOK_PRIO_POST_I2C);
