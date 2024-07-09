/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "cros_board_info.h"
#include "cros_cbi.h"
#include "driver/accel_bma4xx.h"
#include "driver/accel_lis2dw12_public.h"
#include "driver/accelgyro_bmi323.h"
#include "driver/accelgyro_lsm6ds0.h"
#include "driver/accelgyro_lsm6dso.h"
#include "gpio/gpio_int.h"
#include "hooks.h"
#include "motion_sense.h"
#include "motionsense_sensors.h"
#include "tablet_mode.h"

#include <zephyr/logging/log.h>

#define I2C_PORT_SENSOR 1

LOG_MODULE_DECLARE(nissa, CONFIG_NISSA_LOG_LEVEL);

static int cbi_boardversion = -1;
static int sensor_fwconfig;
static int motion_none;
static int select_bma422;
static int select_lis2dw12;
static int select_bmi323;
static int select_lsm6dso;

void motion_interrupt(enum gpio_signal signal)
{
	if (cbi_boardversion < 2) {
		if (sensor_fwconfig == LIS2DW12_LSM6DSO)
			lsm6dso_interrupt(signal);
		else
			bmi3xx_interrupt(signal);
	} else if (cbi_boardversion == 2) {
		if (sensor_fwconfig == BMA422_BMI323 ||
		    sensor_fwconfig == LIS2DW12_BMI323)
			bmi3xx_interrupt(signal);
		else if (sensor_fwconfig == BMA422_LSM6DSO ||
			 sensor_fwconfig == LIS2DW12_LSM6DSO)
			lsm6dso_interrupt(signal);
	} else {
		if (select_bmi323)
			bmi3xx_interrupt(signal);
		else if (select_lsm6dso)
			lsm6dso_interrupt(signal);
	}
}

void lid_accel_interrupt(enum gpio_signal signal)
{
	if (cbi_boardversion < 2) {
		if (sensor_fwconfig == LIS2DW12_LSM6DSO)
			lis2dw12_interrupt(signal);
		else
			bma4xx_interrupt(signal);
	} else if (cbi_boardversion == 2) {
		if (sensor_fwconfig == BMA422_LSM6DSO ||
		    sensor_fwconfig == BMA422_BMI323)
			bma4xx_interrupt(signal);
		else if (sensor_fwconfig == LIS2DW12_BMI323 ||
			 sensor_fwconfig == LIS2DW12_LSM6DSO)
			lis2dw12_interrupt(signal);
	} else {
		if (select_bma422)
			bma4xx_interrupt(signal);
		else if (select_lis2dw12)
			lis2dw12_interrupt(signal);
	}
}

static void motionsense_init(void)
{
	int ret;
	uint32_t tablet_present;

	ret = cbi_get_board_version(&cbi_boardversion);
	if (ret != EC_SUCCESS) {
		LOG_ERR("Error retrieving CBI BOARD_VER.");
		return;
	}

	ret = cros_cbi_get_fw_config(FW_TABLET, &tablet_present);
	if (ret < 0) {
		LOG_ERR("error retriving CBI config: %d", ret);
		return;
	}

	ret = cros_cbi_get_fw_config(MOTIONSENSE_SENSOR, &sensor_fwconfig);
	if (ret < 0) {
		LOG_ERR("error retriving CBI config: %d", ret);
		return;
	}

	if (cbi_boardversion < 2) {
		if (tablet_present == FW_TABLET_PRESENT) {
			if (sensor_fwconfig == LIS2DW12_LSM6DSO) {
				ccprints(
					"LID SENSOR:LIS2DW12, BASE SENSOR:LSM6DSO");
			} else {
				MOTIONSENSE_ENABLE_ALTERNATE(alt_lid_accel);
				MOTIONSENSE_ENABLE_ALTERNATE(alt_base_accel);
				MOTIONSENSE_ENABLE_ALTERNATE(alt_base_gyro);
				ccprints(
					"LID SENSOR:BMA422, BASE SENSOR:BMI323");
			}
		} else
			motion_none = 1;
	} else if (cbi_boardversion == 2) {
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
			motion_none = 1;
		}
	}

	if (motion_none) {
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

static void prob_i2c_sensor(void)
{
	int rv, ret;
	int val = 0;
	int data = 0;

	ret = cbi_get_board_version(&cbi_boardversion);
	if (ret != EC_SUCCESS) {
		LOG_ERR("Error retrieving CBI BOARD_VER.");
		return;
	}

	if (cbi_boardversion >= 3) {
		/* LID: BMA422 */
		rv = i2c_read8(I2C_PORT_SENSOR, BMA4_I2C_ADDR_PRIMARY, 0x00,
			       &val);
		if (!rv && (val == 0x12)) {
			select_bma422 = 1;
			MOTIONSENSE_ENABLE_ALTERNATE(alt_lid_accel);
		}

		/* LID: LIS2DW12 */
		rv = i2c_read8(I2C_PORT_SENSOR, LIS2DW12_ADDR1, 0x0f, &val);
		if (!rv && (val == 0x44)) {
			select_lis2dw12 = 1;
		}

		/* BASE: BMI323 */
		rv = i2c_read32(I2C_PORT_SENSOR, BMI3_ADDR_I2C_PRIM, 0x00,
				&data);
		uint8_t lower_byte = (uint8_t)(data >> 16);

		if (!rv && (lower_byte == 0x43)) {
			select_bmi323 = 1;
			MOTIONSENSE_ENABLE_ALTERNATE(alt_base_accel);
			MOTIONSENSE_ENABLE_ALTERNATE(alt_base_gyro);
		}

		/* BASE: LSM6DSO */
		rv = i2c_read8(I2C_PORT_SENSOR, LSM6DS0_ADDR0_FLAGS, 0x0f,
			       &val);
		if (!rv && (val == 0x6c)) {
			select_lsm6dso = 1;
		}

		ccprints("select bma422:%d, lis2dw12:%d, bmi323:%d, lsm6dso:%d",
			 select_bma422, select_lis2dw12, select_bmi323,
			 select_lsm6dso);
		if (!(select_bma422 || select_lis2dw12 || select_bmi323 ||
		      select_lsm6dso)) {
			motion_none = 1;
		}
	}

	if (motion_none) {
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
DECLARE_HOOK(HOOK_CHIPSET_STARTUP, prob_i2c_sensor, HOOK_PRIO_DEFAULT);
