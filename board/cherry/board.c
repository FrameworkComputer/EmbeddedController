/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
/* Cherry board configuration */

#include "common.h"
#include "console.h"
#include "driver/accel_bma422.h"
#include "driver/accel_kionix.h"
#include "driver/accel_kx022.h"
#include "driver/accelgyro_icm42607.h"
#include "driver/accelgyro_icm_common.h"
#include "gpio.h"
#include "hooks.h"
#include "motion_sense.h"
#include "pwm.h"
#include "pwm_chip.h"
#include "system.h"

#define CPRINTS(format, args...) cprints(CC_USBCHARGE, format, ## args)
#define CPRINTF(format, args...) cprintf(CC_USBCHARGE, format, ## args)

/* Sensor */
static struct mutex g_base_mutex;
static struct mutex g_lid_mutex;

static struct icm_drv_data_t g_icm42607_data;
static struct kionix_accel_data g_kx022_data;
static struct accelgyro_saved_data_t g_bma422_data;

/* Matrix to rotate accelrator into standard reference frame */
static const mat33_fp_t base_standard_ref = {
	{ 0, FLOAT_TO_FP(1), 0},
	{ FLOAT_TO_FP(1), 0, 0},
	{ 0, 0, FLOAT_TO_FP(-1)}
};

static const mat33_fp_t lid_standard_ref = {
	{ FLOAT_TO_FP(-1), 0, 0},
	{ 0, FLOAT_TO_FP(-1), 0},
	{ 0, 0, FLOAT_TO_FP(1)}
};

struct motion_sensor_t motion_sensors[] = {
	/*
	 * Note: bmi160: supports accelerometer and gyro sensor
	 * Requirement: accelerometer sensor must init before gyro sensor
	 * DO NOT change the order of the following table.
	 */
	[BASE_ACCEL] = {
		.name = "Base Accel",
		.active_mask = SENSOR_ACTIVE_S0_S3,
		.chip = MOTIONSENSE_CHIP_ICM42607,
		.type = MOTIONSENSE_TYPE_ACCEL,
		.location = MOTIONSENSE_LOC_BASE,
		.drv = &icm42607_drv,
		.mutex = &g_base_mutex,
		.drv_data = &g_icm42607_data,
		.port = I2C_PORT_ACCEL,
		.i2c_spi_addr_flags = ICM42607_ADDR0_FLAGS,
		.default_range = 4, /* g, to meet CDD 7.3.1/C-1-4 reqs.*/
		.rot_standard_ref = &base_standard_ref,
		.min_frequency = ICM42607_ACCEL_MIN_FREQ,
		.max_frequency = ICM42607_ACCEL_MAX_FREQ,
		.config = {
			/* EC use accel for angle detection */
			[SENSOR_CONFIG_EC_S0] = {
				.odr = 10000 | ROUND_UP_FLAG,
			},
			/* EC use accel for angle detection */
			[SENSOR_CONFIG_EC_S3] = {
				.odr = 10000 | ROUND_UP_FLAG,
			},
		},
	},
	[BASE_GYRO] = {
		.name = "Base Gyro",
		.active_mask = SENSOR_ACTIVE_S0_S3,
		.chip = MOTIONSENSE_CHIP_ICM42607,
		.type = MOTIONSENSE_TYPE_GYRO,
		.location = MOTIONSENSE_LOC_BASE,
		.drv = &icm42607_drv,
		.mutex = &g_base_mutex,
		.drv_data = &g_icm42607_data,
		.port = I2C_PORT_ACCEL,
		.i2c_spi_addr_flags = ICM42607_ADDR0_FLAGS,
		.default_range = 1000, /* dps */
		.rot_standard_ref = &base_standard_ref,
		.min_frequency = ICM42607_GYRO_MIN_FREQ,
		.max_frequency = ICM42607_GYRO_MAX_FREQ,
	},
	[LID_ACCEL] = {
		.name = "Lid Accel",
		.active_mask = SENSOR_ACTIVE_S0_S3,
		.chip = MOTIONSENSE_CHIP_KX022,
		.type = MOTIONSENSE_TYPE_ACCEL,
		.location = MOTIONSENSE_LOC_LID,
		.drv = &kionix_accel_drv,
		.mutex = &g_lid_mutex,
		.drv_data = &g_kx022_data,
		.port = I2C_PORT_ACCEL,
		.i2c_spi_addr_flags = KX022_ADDR1_FLAGS,
		.rot_standard_ref = &lid_standard_ref,
		.default_range = 2, /* g, enough for laptop. */
		.min_frequency = KX022_ACCEL_MIN_FREQ,
		.max_frequency = KX022_ACCEL_MAX_FREQ,
		.config = {
			 /* EC use accel for angle detection */
			[SENSOR_CONFIG_EC_S0] = {
				.odr = 10000 | ROUND_UP_FLAG,
				.ec_rate = 100,
			},
			/* EC use accel for angle detection */
			[SENSOR_CONFIG_EC_S3] = {
				.odr = 10000 | ROUND_UP_FLAG,
			},
		},
	},
};
const unsigned int motion_sensor_count = ARRAY_SIZE(motion_sensors);

struct motion_sensor_t bma422_lid_accel = {
	.name = "Lid Accel",
	.active_mask = SENSOR_ACTIVE_S0_S3,
	.chip = MOTIONSENSE_CHIP_BMA422,
	.type = MOTIONSENSE_TYPE_ACCEL,
	.location = MOTIONSENSE_LOC_LID,
	.drv = &bma4_accel_drv,
	.mutex = &g_lid_mutex,
	.drv_data = &g_bma422_data,
	.port = I2C_PORT_ACCEL,
	.i2c_spi_addr_flags = BMA4_I2C_ADDR_PRIMARY,
	.rot_standard_ref = &lid_standard_ref,
	.min_frequency = BMA4_ACCEL_MIN_FREQ,
	.max_frequency = BMA4_ACCEL_MAX_FREQ,
	.default_range = 2, /* g, enough for laptop. */
	.config = {
		/* EC use accel for angle detection */
		[SENSOR_CONFIG_EC_S0] = {
			.odr = 12500 | ROUND_UP_FLAG,
			.ec_rate = 100 * MSEC,
		},
		/* Sensor on in S3 */
		[SENSOR_CONFIG_EC_S3] = {
			.odr = 12500 | ROUND_UP_FLAG,
			.ec_rate = 0,
		},
	},
};

static void board_update_motion_sensor_config(void)
{
	if (system_get_board_version() >= 2) {
		motion_sensors[LID_ACCEL] = bma422_lid_accel;
		ccprints("LID ACCEL is BMA422");
	} else {
		ccprints("LID ACCEL is KX022");
	}
}

/* Initialize board. */
static void board_init(void)
{
	/* Enable motion sensor interrupt */
	gpio_enable_interrupt(GPIO_BASE_IMU_INT_L);
	gpio_enable_interrupt(GPIO_LID_ACCEL_INT_L);

	/* Disable PWM_CH_LED2(Green) for unuse */
	pwm_enable(PWM_CH_LED2, 0);

	board_update_motion_sensor_config();

	if (board_get_version() >= 2) {
		gpio_set_flags(GPIO_I2C_H_SCL, GPIO_INPUT | GPIO_PULL_DOWN);
		gpio_set_flags(GPIO_I2C_H_SDA, GPIO_INPUT | GPIO_PULL_DOWN);
	}
}
DECLARE_HOOK(HOOK_INIT, board_init, HOOK_PRIO_DEFAULT);

static void board_do_chipset_resume(void)
{
	gpio_set_level(GPIO_EN_KB_BL, 1);
}
DECLARE_HOOK(HOOK_CHIPSET_RESUME, board_do_chipset_resume, HOOK_PRIO_DEFAULT);

static void board_do_chipset_suspend(void)
{
	gpio_set_level(GPIO_EN_KB_BL, 0);
}
DECLARE_HOOK(HOOK_CHIPSET_SUSPEND, board_do_chipset_suspend, HOOK_PRIO_DEFAULT);
