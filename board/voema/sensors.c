/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Volteer family-specific sensor configuration */
#include "common.h"
#include "accelgyro.h"
#include "cbi_ssfc.h"
#include "driver/accel_bma2x2.h"
#include "driver/accelgyro_icm426xx.h"
#include "driver/accelgyro_icm_common.h"
#include "driver/accel_kionix.h"
#include "driver/als_tcs3400.h"
#include "driver/sync.h"
#include "keyboard_scan.h"
#include "hooks.h"
#include "i2c.h"
#include "system.h"
#include "task.h"
#include "tablet_mode.h"
#include "util.h"

/******************************************************************************/
/* Sensors */
static struct mutex g_lid_accel_mutex;
static struct mutex g_base_mutex;

/* BMA253 private data */
static struct accelgyro_saved_data_t g_bma253_base_data;
static struct accelgyro_saved_data_t g_bma253_lid_data;

static struct icm_drv_data_t g_icm426xx_data;

static struct kionix_accel_data g_kx022_lid_data;

/* TCS3400 private data */
static struct als_drv_data_t g_tcs3400_data = {
	.als_cal.scale = 1,
	.als_cal.uscale = 0,
	.als_cal.offset = 0,
	.als_cal.channel_scale = {
		.k_channel_scale = ALS_CHANNEL_SCALE(1.0), /* kc from VPD */
		.cover_scale = ALS_CHANNEL_SCALE(1.0),     /* CT */
	},
};

/*
 * TODO: b/146166425 need to calibrate ALS/RGB sensor. At default settings,
 * shining phone flashlight on sensor pegs all readings at 0xFFFF.
 */
static struct tcs3400_rgb_drv_data_t g_tcs3400_rgb_data = {
	.calibration.rgb_cal[X] = {
		.offset = 0,
		.coeff[TCS_RED_COEFF_IDX] = FLOAT_TO_FP(0),
		.coeff[TCS_GREEN_COEFF_IDX] = FLOAT_TO_FP(0),
		.coeff[TCS_BLUE_COEFF_IDX] = FLOAT_TO_FP(0),
		.coeff[TCS_CLEAR_COEFF_IDX] = FLOAT_TO_FP(0),
		.scale = {
			.k_channel_scale = ALS_CHANNEL_SCALE(1.0), /* kr */
			.cover_scale = ALS_CHANNEL_SCALE(1.0)
		}
	},
	.calibration.rgb_cal[Y] = {
		.offset = 0,
		.coeff[TCS_RED_COEFF_IDX] = FLOAT_TO_FP(0),
		.coeff[TCS_GREEN_COEFF_IDX] = FLOAT_TO_FP(0),
		.coeff[TCS_BLUE_COEFF_IDX] = FLOAT_TO_FP(0),
		.coeff[TCS_CLEAR_COEFF_IDX] = FLOAT_TO_FP(0),
		.scale = {
			.k_channel_scale = ALS_CHANNEL_SCALE(1.0), /* kg */
			.cover_scale = ALS_CHANNEL_SCALE(1.0)
		},
	},
	.calibration.rgb_cal[Z] = {
		.offset = 0,
		.coeff[TCS_RED_COEFF_IDX] = FLOAT_TO_FP(0),
		.coeff[TCS_GREEN_COEFF_IDX] = FLOAT_TO_FP(0),
		.coeff[TCS_BLUE_COEFF_IDX] = FLOAT_TO_FP(0),
		.coeff[TCS_CLEAR_COEFF_IDX] = FLOAT_TO_FP(0),
		.scale = {
			.k_channel_scale = ALS_CHANNEL_SCALE(1.0), /* kb */
			.cover_scale = ALS_CHANNEL_SCALE(1.0)
		}
	},
	.calibration.irt = INT_TO_FP(1),
	.saturation.again = TCS_DEFAULT_AGAIN,
	.saturation.atime = TCS_DEFAULT_ATIME,
};

/* Rotation matrix for the lid accelerometer */
static const mat33_fp_t lid_standard_ref = {
	{ FLOAT_TO_FP(-1), 0, 0},
	{ 0, FLOAT_TO_FP(-1), 0},
	{ 0, 0, FLOAT_TO_FP(1)}
};

const mat33_fp_t base_standard_ref = {
	{ 0, FLOAT_TO_FP(-1), 0},
	{ FLOAT_TO_FP(-1), 0, 0},
	{ 0, 0, FLOAT_TO_FP(-1)}
};

const mat33_fp_t base_icm_ref = {
	{ FLOAT_TO_FP(1), 0,  0},
	{ 0, FLOAT_TO_FP(-1), 0},
	{ 0, 0,  FLOAT_TO_FP(-1)}
};

struct motion_sensor_t kx022_lid_accel = {
	.name = "Lid Accel",
	.active_mask = SENSOR_ACTIVE_S0_S3,
	.chip = MOTIONSENSE_CHIP_KX022,
	.type = MOTIONSENSE_TYPE_ACCEL,
	.location = MOTIONSENSE_LOC_LID,
	.drv = &kionix_accel_drv,
	.mutex = &g_lid_accel_mutex,
	.drv_data = &g_kx022_lid_data,
	.port = I2C_PORT_SENSOR,
	.i2c_spi_addr_flags = KX022_ADDR0_FLAGS,
	.rot_standard_ref = &lid_standard_ref,
	.min_frequency = KX022_ACCEL_MIN_FREQ,
	.max_frequency = KX022_ACCEL_MAX_FREQ,
	.default_range = 2, /* g, to support tablet mode */
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
		.drv_data = &g_bma253_lid_data,
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
		.chip = MOTIONSENSE_CHIP_BMA255,
		.type = MOTIONSENSE_TYPE_ACCEL,
		.location = MOTIONSENSE_LOC_BASE,
		.drv = &bma2x2_accel_drv,
		.mutex = &g_base_mutex,
		.drv_data = &g_bma253_base_data,
		.port = I2C_PORT_SENSOR,
		.i2c_spi_addr_flags = BMA2x2_I2C_ADDR2_FLAGS,
		.rot_standard_ref = &base_standard_ref,
		.min_frequency = BMA255_ACCEL_MIN_FREQ,
		.max_frequency = BMA255_ACCEL_MAX_FREQ,
		.default_range = 4, /* g */
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
	[BASE_GYRO] = {
	},
	[CLEAR_ALS] = {
		.name = "Clear Light",
		.active_mask = SENSOR_ACTIVE_S0_S3,
		.chip = MOTIONSENSE_CHIP_TCS3400,
		.type = MOTIONSENSE_TYPE_LIGHT,
		.location = MOTIONSENSE_LOC_BASE,
		.drv = &tcs3400_drv,
		.drv_data = &g_tcs3400_data,
		.port = I2C_PORT_SENSOR,
		.i2c_spi_addr_flags = TCS3400_I2C_ADDR_FLAGS,
		.rot_standard_ref = NULL,
		.default_range = 0x10000, /* scale = 1x, uscale = 0 */
		.min_frequency = TCS3400_LIGHT_MIN_FREQ,
		.max_frequency = TCS3400_LIGHT_MAX_FREQ,
		.config = {
			/* Run ALS sensor in S0 */
			[SENSOR_CONFIG_EC_S0] = {
				.odr = 1000,
			},
		},
	},

	[RGB_ALS] = {
		/*
		 * RGB channels read by CLEAR_ALS and so the i2c port and
		 * address do not need to be defined for RGB_ALS.
		 */
		.name = "RGB Light",
		.active_mask = SENSOR_ACTIVE_S0_S3,
		.chip = MOTIONSENSE_CHIP_TCS3400,
		.type = MOTIONSENSE_TYPE_LIGHT_RGB,
		.location = MOTIONSENSE_LOC_BASE,
		.drv = &tcs3400_rgb_drv,
		.drv_data = &g_tcs3400_rgb_data,
		.rot_standard_ref = NULL,
		.default_range = 0x10000, /* scale = 1x, uscale = 0 */
	},
};
unsigned int motion_sensor_count = ARRAY_SIZE(motion_sensors);

struct motion_sensor_t icm_base_accel = {
		.name = "Base Accel",
		.active_mask = SENSOR_ACTIVE_S0_S3,
		.chip = MOTIONSENSE_CHIP_ICM426XX,
		.type = MOTIONSENSE_TYPE_ACCEL,
		.location = MOTIONSENSE_LOC_BASE,
		.drv = &icm426xx_drv,
		.mutex = &g_base_mutex,
		.drv_data = &g_icm426xx_data,
		.port = I2C_PORT_SENSOR,
		.i2c_spi_addr_flags = ICM426XX_ADDR0_FLAGS,
		.rot_standard_ref = &base_icm_ref,
		.min_frequency = ICM426XX_ACCEL_MIN_FREQ,
		.max_frequency = ICM426XX_ACCEL_MAX_FREQ,
		.default_range = 4, /* g */
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
};

struct motion_sensor_t icm_base_gyro = {
		 .name = "Base Gyro",
		 .active_mask = SENSOR_ACTIVE_S0_S3,
		 .chip = MOTIONSENSE_CHIP_ICM426XX,
		 .type = MOTIONSENSE_TYPE_GYRO,
		 .location = MOTIONSENSE_LOC_BASE,
		 .drv = &icm426xx_drv,
		 .mutex = &g_base_mutex,
		 .drv_data = &g_icm426xx_data,
		 .port = I2C_PORT_SENSOR,
		 .i2c_spi_addr_flags = ICM426XX_ADDR0_FLAGS,
		 .default_range = 1000, /* dps */
		 .rot_standard_ref = &base_icm_ref,
		 .min_frequency = ICM426XX_GYRO_MIN_FREQ,
		 .max_frequency = ICM426XX_GYRO_MAX_FREQ,
};

/* ALS instances when LPC mapping is needed. Each entry directs to a sensor. */
const struct motion_sensor_t *motion_als_sensors[] = {
	&motion_sensors[CLEAR_ALS],
};
BUILD_ASSERT(ARRAY_SIZE(motion_als_sensors) == ALS_COUNT);

static void baseboard_sensors_init(void)
{
	/* Enable interrupt for the TCS3400 color light sensor */
	gpio_enable_interrupt(GPIO_EC_ALS_RGB_INT_L);

	/*
	 * TODO: If a SSFC for the base sensor is added, add the check
	 * here.
	 */
	if (IS_ENABLED(BOARD_VOEMA) && get_cbi_ssfc_base_sensor() ==
			SSFC_SENSOR_BASE_ICM426XX) {
		gpio_enable_interrupt(GPIO_EC_MB_ACCEL_INT_L);
		motion_sensors[BASE_ACCEL] = icm_base_accel;
		motion_sensors[BASE_GYRO] = icm_base_gyro;
		ccprints("BASE ACCEL/GYRO is ICM426XX");
	} else
		ccprints("BASE_ACCEL is BMA253");

	if (get_cbi_ssfc_lid_sensor() == SSFC_SENSOR_LID_KX022) {
		motion_sensors[LID_ACCEL] = kx022_lid_accel;
		ccprints("LID_ACCEL is KX022");
	} else
		ccprints("LID_ACCEL is BMA253");
}
DECLARE_HOOK(HOOK_INIT, baseboard_sensors_init, HOOK_PRIO_DEFAULT);

#ifndef BOARD_VOEMA_NPCX796FC
void motion_interrupt(enum gpio_signal signal)
{
	icm426xx_interrupt(signal);
}

int board_accel_force_mode_mask(void)
{
	if (system_get_board_version() <= 2)
		return (BIT(LID_ACCEL) | BIT(CLEAR_ALS) | BIT(BASE_ACCEL));
	else
		return (BIT(LID_ACCEL) | BIT(CLEAR_ALS));
}
#endif
