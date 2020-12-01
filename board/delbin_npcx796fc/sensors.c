/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Volteer family-specific sensor configuration */
#include "common.h"
#include "accelgyro.h"
#include "driver/accel_bma2x2.h"
#include "driver/accelgyro_bmi_common.h"
#include "driver/accelgyro_bmi260.h"
#include "keyboard_scan.h"
#include "hooks.h"
#include "i2c.h"
#include "task.h"
#include "tablet_mode.h"
#include "util.h"

/******************************************************************************/
/* Sensors */
static struct mutex g_lid_accel_mutex;
static struct mutex g_base_mutex;

/* BMA253 private data */
static struct accelgyro_saved_data_t g_bma253_data;

/* BMI260 private data */
static struct bmi_drv_data_t g_bmi260_data;

/* Rotation matrix for the lid accelerometer */
static const mat33_fp_t lid_standard_ref = {
	{ FLOAT_TO_FP(-1), 0, 0},
	{ 0, FLOAT_TO_FP(-1), 0},
	{ 0, 0, FLOAT_TO_FP(1)}
};

const mat33_fp_t base_standard_ref = {
	{ FLOAT_TO_FP(1), 0, 0},
	{ 0, FLOAT_TO_FP(1), 0},
	{ 0, 0, FLOAT_TO_FP(1)}
};

struct motion_sensor_t motion_sensors[] = {
	[LID_ACCEL] = {
		.name = "Lid Accel",
		.active_mask = SENSOR_ACTIVE_S0_S3,
		.chip = MOTIONSENSE_CHIP_BMA255,
		.type = MOTIONSENSE_TYPE_ACCEL,
		.location = MOTIONSENSE_LOC_LID,
		.drv = &bma2x2_accel_drv,
		.mutex = &g_lid_accel_mutex,
		.drv_data = &g_bma253_data,
		.port = I2C_PORT_SENSOR,
		.i2c_spi_addr_flags = BMA2x2_I2C_ADDR1_FLAGS,
		.rot_standard_ref = &lid_standard_ref,
		.min_frequency = BMA255_ACCEL_MIN_FREQ,
		.max_frequency = BMA255_ACCEL_MAX_FREQ,
		.default_range = 2, /* g, to support tablet mode */
		.config = {
			/* EC use accel for angle detection */
			[SENSOR_CONFIG_EC_S0] = {
				.odr = 10000 | ROUND_UP_FLAG,
			},
			/* Sensor on in S3 */
			[SENSOR_CONFIG_EC_S3] = {
				.odr = 10000 | ROUND_UP_FLAG,
			},
		},
	},
	[BASE_ACCEL] = {
		.name = "Base Accel",
		.active_mask = SENSOR_ACTIVE_S0_S3,
		.chip = MOTIONSENSE_CHIP_BMI260,
		.type = MOTIONSENSE_TYPE_ACCEL,
		.location = MOTIONSENSE_LOC_BASE,
		.drv = &bmi260_drv,
		.mutex = &g_base_mutex,
		.drv_data = &g_bmi260_data,
		.port = I2C_PORT_SENSOR,
		.i2c_spi_addr_flags = BMI260_ADDR0_FLAGS,
		.rot_standard_ref = &base_standard_ref,
		.min_frequency = BMI_ACCEL_MIN_FREQ,
		.max_frequency = BMI_ACCEL_MAX_FREQ,
		.default_range = 4, /* g */
		.config = {
			/* EC use accel for angle detection */
			[SENSOR_CONFIG_EC_S0] = {
				.odr = 10000 | ROUND_UP_FLAG,
				.ec_rate = 100 * MSEC,
			},
			/* Sensor on in S3 */
			[SENSOR_CONFIG_EC_S3] = {
				.odr = 10000 | ROUND_UP_FLAG,
				.ec_rate = 100 * MSEC,
			},
		},
	},

	[BASE_GYRO] = {
		.name = "Base Gyro",
		.active_mask = SENSOR_ACTIVE_S0_S3,
		.chip = MOTIONSENSE_CHIP_BMI260,
		.type = MOTIONSENSE_TYPE_GYRO,
		.location = MOTIONSENSE_LOC_BASE,
		.drv = &bmi260_drv,
		.mutex = &g_base_mutex,
		.drv_data = &g_bmi260_data,
		.port = I2C_PORT_SENSOR,
		.i2c_spi_addr_flags = BMI260_ADDR0_FLAGS,
		.default_range = 1000, /* dps */
		.rot_standard_ref = &base_standard_ref,
		.min_frequency = BMI_GYRO_MIN_FREQ,
		.max_frequency = BMI_GYRO_MAX_FREQ,
	},
};
unsigned int motion_sensor_count = ARRAY_SIZE(motion_sensors);

static void baseboard_sensors_init(void)
{
	/* Note - BMA253 interrupt unused by EC */

	/* Enable interrupt for the BMI260 accel/gyro sensor */
	gpio_enable_interrupt(GPIO_EC_IMU_INT_L);
}
DECLARE_HOOK(HOOK_INIT, baseboard_sensors_init, HOOK_PRIO_DEFAULT);

#ifndef TEST_BUILD
void lid_angle_peripheral_enable(int enable)
{
	int chipset_in_s0 = chipset_in_state(CHIPSET_STATE_ON);

	if (enable) {
		keyboard_scan_enable(1, KB_SCAN_DISABLE_LID_ANGLE);
	} else {
		/*
		 * Ensure that the chipset is off before disabling the keyboard.
		 * When the chipset is on, the EC keeps the keyboard enabled and
		 * the AP decides whether to ignore input devices or not.
		 */
		if (!chipset_in_s0)
			keyboard_scan_enable(0, KB_SCAN_DISABLE_LID_ANGLE);
	}
}
#endif
