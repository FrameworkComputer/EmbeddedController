/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Waddledoo board-specific configuration */

#include "adc_chip.h"
#include "button.h"
#include "common.h"
#include "compile_time_macros.h"
#include "driver/accel_bma2x2.h"
#include "driver/accelgyro_bmi160.h"
#include "driver/sync.h"
#include "extpower.h"
#include "gpio.h"
#include "i2c.h"
#include "lid_switch.h"
#include "motion_sense.h"
#include "power.h"
#include "power_button.h"
#include "switch.h"
#include "tablet_mode.h"
#include "task.h"

#include "gpio_list.h"

/* Sensors */
static struct mutex g_lid_mutex;
static struct mutex g_base_mutex;

static struct accelgyro_saved_data_t g_bma253_data;
static struct bmi160_drv_data_t g_bmi160_data;

struct motion_sensor_t motion_sensors[] = {
	[LID_ACCEL] = {
		.name = "Lid Accel",
		.active_mask = SENSOR_ACTIVE_S0_S3,
		.chip = MOTIONSENSE_CHIP_BMA255,
		.type = MOTIONSENSE_TYPE_ACCEL,
		.location = MOTIONSENSE_LOC_LID,
		.drv = &bma2x2_accel_drv,
		.mutex = &g_lid_mutex,
		.drv_data = &g_bma253_data,
		.port = I2C_PORT_SENSOR,
		.i2c_spi_addr_flags = BMA2x2_I2C_ADDR1_FLAGS,
		.rot_standard_ref = NULL,
		.default_range = 2,
		.min_frequency = BMA255_ACCEL_MIN_FREQ,
		.max_frequency = BMA255_ACCEL_MAX_FREQ,
		.config = {
			[SENSOR_CONFIG_EC_S0] = {
				.odr = 10000 | ROUND_UP_FLAG,
			},
			[SENSOR_CONFIG_EC_S3] = {
				.odr = 10000 | ROUND_UP_FLAG,
			},
		},
	},
	[BASE_ACCEL] = {
		.name = "Base Accel",
		.active_mask = SENSOR_ACTIVE_S0_S3,
		.chip = MOTIONSENSE_CHIP_BMI160,
		.type = MOTIONSENSE_TYPE_ACCEL,
		.location = MOTIONSENSE_LOC_BASE,
		.drv = &bmi160_drv,
		.mutex = &g_base_mutex,
		.drv_data = &g_bmi160_data,
		.port = I2C_PORT_SENSOR,
		.i2c_spi_addr_flags = BMI160_ADDR0_FLAGS,
		.rot_standard_ref = NULL,
		.default_range = 4,
		.min_frequency = BMI160_ACCEL_MIN_FREQ,
		.max_frequency = BMI160_ACCEL_MAX_FREQ,
		.config = {
			[SENSOR_CONFIG_EC_S0] = {
				.odr = 13000 | ROUND_UP_FLAG,
				.ec_rate = 100 * MSEC,
			},
			[SENSOR_CONFIG_EC_S3] = {
				.odr = 10000 | ROUND_UP_FLAG,
				.ec_rate = 100 * MSEC,
			},
		},
	},
	[BASE_GYRO] = {
		.name = "Base Gyro",
		.active_mask = SENSOR_ACTIVE_S0_S3,
		.chip = MOTIONSENSE_CHIP_BMI160,
		.type = MOTIONSENSE_TYPE_GYRO,
		.location = MOTIONSENSE_LOC_BASE,
		.drv = &bmi160_drv,
		.mutex = &g_base_mutex,
		.drv_data = &g_bmi160_data,
		.port = I2C_PORT_SENSOR,
		.i2c_spi_addr_flags = BMI160_ADDR0_FLAGS,
		.default_range = 1000, /* dps */
		.rot_standard_ref = NULL,
		.min_frequency = BMI160_GYRO_MIN_FREQ,
		.max_frequency = BMI160_GYRO_MAX_FREQ,
	},
	[VSYNC] = {
		.name = "Camera VSYNC",
		.active_mask = SENSOR_ACTIVE_S0,
		.chip = MOTIONSENSE_CHIP_GPIO,
		.type = MOTIONSENSE_TYPE_SYNC,
		.location = MOTIONSENSE_LOC_CAMERA,
		.drv = &sync_drv,
		.default_range = 0,
		.min_frequency = 0,
		.max_frequency = 1,
	},
};

const unsigned int motion_sensor_count = ARRAY_SIZE(motion_sensors);


int extpower_is_present(void)
{
	/*
	 * TODO(b:146651593) We can likely use the charger IC to determine VBUS
	 * presence.
	 */
	return 1;
}

#ifndef TEST_BUILD
/* This callback disables keyboard when convertibles are fully open */
void lid_angle_peripheral_enable(int enable)
{
	/*
	 * If the lid is in tablet position via other sensors,
	 * ignore the lid angle, which might be faulty then
	 * disable keyboard.
	 */
	if (tablet_get_mode())
		enable = 0;
	/*
	 * TODO(b/147453459): Waddledoo: add keyboard support
	 * disable keyboard here when AP is off
	 *
	 */
}
#endif
