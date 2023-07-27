/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "cros_board_info.h"
#include "cros_cbi.h"
#include "driver/accel_bma4xx.h"
#include "driver/accel_lis2dw12.h"
#include "driver/accelgyro_bmi323.h"
#include "driver/accelgyro_lsm6dso.h"
#include "gpio/gpio_int.h"
#include "hooks.h"
#include "motion_sense.h"
#include "motionsense_sensors.h"
#include "tablet_mode.h"

static int sensor_fwconfig;

void motion_interrupt(enum gpio_signal signal)
{
	if (sensor_fwconfig == BMA422_BMI323 ||
	    sensor_fwconfig == LIS2DW12_BMI323)
		bmi3xx_interrupt(signal);
	else if (sensor_fwconfig == BMA422_LSM6DSO ||
		 sensor_fwconfig == LIS2DW12_LSM6DSO)
		lsm6dso_interrupt(signal);
}

void lid_accel_interrupt(enum gpio_signal signal)
{
	if (sensor_fwconfig == BMA422_LSM6DSO ||
	    sensor_fwconfig == BMA422_BMI323)
		bma4xx_interrupt(signal);
	else if (sensor_fwconfig == LIS2DW12_BMI323 ||
		 sensor_fwconfig == LIS2DW12_LSM6DSO)
		lis2dw12_interrupt(signal);
}

static void motionsense_init(void)
{
	cros_cbi_get_fw_config(MOTIONSENSE_SENSOR, &sensor_fwconfig);

	if (sensor_fwconfig == BMA422_LSM6DSO) {
		MOTIONSENSE_ENABLE_ALTERNATE(alt_lid_accel);
		ccprints("LID ACCEL:BMA422, BASE ACCEL:LSM6DSO");
	} else if (sensor_fwconfig == BMA422_BMI323) {
		MOTIONSENSE_ENABLE_ALTERNATE(alt_lid_accel);
		MOTIONSENSE_ENABLE_ALTERNATE(alt_base_accel);
		MOTIONSENSE_ENABLE_ALTERNATE(alt_base_gyro);
		ccprints("LID ACCEL:BMA422, BASE ACCEL:BMI323");
	} else if (sensor_fwconfig == LIS2DW12_BMI323) {
		MOTIONSENSE_ENABLE_ALTERNATE(alt_base_accel);
		MOTIONSENSE_ENABLE_ALTERNATE(alt_base_gyro);
		ccprints("LID ACCEL:LIS2DW12, BASE ACCEL:BMI323");
	} else if (sensor_fwconfig == LIS2DW12_LSM6DSO) {
		ccprints("LID ACCEL:LIS2DW12, BASE ACCEL:LSM6DSO");
	} else {
		motion_sensor_count = 0;
		gmr_tablet_switch_disable();
		gpio_disable_dt_interrupt(GPIO_INT_FROM_NODELABEL(int_imu));
		gpio_disable_dt_interrupt(GPIO_INT_FROM_NODELABEL(int_lid_imu));
		gpio_pin_configure_dt(GPIO_DT_FROM_NODELABEL(gpio_imu_int_l),
				      GPIO_DISCONNECTED);
		gpio_pin_configure_dt(GPIO_DT_FROM_NODELABEL(gpio_acc_int_l),
				      GPIO_DISCONNECTED);
		ccprints("NO MOTIONSENSE");
	}
}
DECLARE_HOOK(HOOK_INIT, motionsense_init, HOOK_PRIO_DEFAULT);
