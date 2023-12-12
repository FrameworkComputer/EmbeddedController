/* Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "accelgyro.h"
#include "adc_chip.h"
#include "cbi_ssfc.h"
#include "common.h"
#include "driver/accel_bma2x2.h"
#include "driver/accel_bma422.h"
#include "driver/accelgyro_lsm6dsm.h"
#include "driver/als_tcs3400_public.h"
#include "gpio.h"
#include "hooks.h"
#include "motion_sense.h"
#include "temp_sensor.h"
#include "temp_sensor/thermistor.h"
#include "thermal.h"

/* ADC configuration */
const struct adc_t adc_channels[] = {
	[ADC_TEMP_SENSOR_1_DDR] = {
		.name = "TEMP_DDR",
		.input_ch = NPCX_ADC_CH0,
		.factor_mul = ADC_MAX_VOLT,
		.factor_div = ADC_READ_MAX + 1,
		.shift = 0,
	},
	[ADC_TEMP_SENSOR_2_SOC] = {
		.name = "TEMP_SOC",
		.input_ch = NPCX_ADC_CH1,
		.factor_mul = ADC_MAX_VOLT,
		.factor_div = ADC_READ_MAX + 1,
		.shift = 0,
	},
	[ADC_TEMP_SENSOR_3_CHARGER] = {
		.name = "TEMP_CHARGER",
		.input_ch = NPCX_ADC_CH6,
		.factor_mul = ADC_MAX_VOLT,
		.factor_div = ADC_READ_MAX + 1,
		.shift = 0,
	},
	[ADC_TEMP_SENSOR_4_REGULATOR] = {
		.name = "TEMP_REGULATOR",
		.input_ch = NPCX_ADC_CH7,
		.factor_mul = ADC_MAX_VOLT,
		.factor_div = ADC_READ_MAX + 1,
		.shift = 0,
	},
};
BUILD_ASSERT(ARRAY_SIZE(adc_channels) == ADC_CH_COUNT);

K_MUTEX_DEFINE(g_lid_accel_mutex);
K_MUTEX_DEFINE(g_base_accel_mutex);
static struct accelgyro_saved_data_t g_bma253_data;
static struct accelgyro_saved_data_t g_bma422_data;
static struct lsm6dsm_data lsm6dsm_data = LSM6DSM_DATA;

static const mat33_fp_t lid_standard_ref = { { FLOAT_TO_FP(-1), 0, 0 },
					     { 0, FLOAT_TO_FP(1), 0 },
					     { 0, 0, FLOAT_TO_FP(-1) } };

static const mat33_fp_t base_standard_ref = { { FLOAT_TO_FP(-1), 0, 0 },
					      { 0, FLOAT_TO_FP(1), 0 },
					      { 0, 0, FLOAT_TO_FP(-1) } };

/* TCS3400 private data */
static struct als_drv_data_t g_tcs3400_data = {
	.als_cal.scale = 1,
	.als_cal.uscale = 0,
	.als_cal.offset = 0,
	.als_cal.channel_scale = {
		.k_channel_scale = ALS_CHANNEL_SCALE(1.0), /* kc from VPD */
		.cover_scale = ALS_CHANNEL_SCALE(0.98),     /* CT */
	},
};

/*
 * TODO: b/184702900 need to calibrate ALS/RGB sensor. At default settings,
 * shining phone flashlight on sensor pegs all readings at 0xFFFF.
 */
static struct tcs3400_rgb_drv_data_t g_tcs3400_rgb_data = {
	.calibration.rgb_cal[X] = {
		.offset = 66, /* 66.47729532 */
		.coeff[TCS_RED_COEFF_IDX] = FLOAT_TO_FP(0.00222243),
		.coeff[TCS_GREEN_COEFF_IDX] = FLOAT_TO_FP(0.51877192),
		.coeff[TCS_BLUE_COEFF_IDX] = FLOAT_TO_FP(-0.28664117),
		.coeff[TCS_CLEAR_COEFF_IDX] = FLOAT_TO_FP(0.0586877),
		.scale = {
			.k_channel_scale = ALS_CHANNEL_SCALE(1.0), /* kr */
			.cover_scale = ALS_CHANNEL_SCALE(0.61)
		}
	},
	.calibration.rgb_cal[Y] = {
		.offset = 41, /* 40.95355984 */
		.coeff[TCS_RED_COEFF_IDX] = FLOAT_TO_FP(-0.15384715),
		.coeff[TCS_GREEN_COEFF_IDX] = FLOAT_TO_FP(0.40454969),
		.coeff[TCS_BLUE_COEFF_IDX] = FLOAT_TO_FP(-0.237452),
		.coeff[TCS_CLEAR_COEFF_IDX] = FLOAT_TO_FP(0.13102168),
		.scale = {
			.k_channel_scale = ALS_CHANNEL_SCALE(1.0), /* kg */
			.cover_scale = ALS_CHANNEL_SCALE(1.0)
		},
	},
	.calibration.rgb_cal[Z] = {
		.offset = 5, /* 5.08596128 */
		.coeff[TCS_RED_COEFF_IDX] = FLOAT_TO_FP(-0.79005309),
		.coeff[TCS_GREEN_COEFF_IDX] = FLOAT_TO_FP(-0.35553576),
		.coeff[TCS_BLUE_COEFF_IDX] = FLOAT_TO_FP(0.13997097),
		.coeff[TCS_CLEAR_COEFF_IDX] = FLOAT_TO_FP(0.40223911),
		.scale = {
			.k_channel_scale = ALS_CHANNEL_SCALE(1.0), /* kb */
			.cover_scale = ALS_CHANNEL_SCALE(1.6)
		}
	},
	.calibration.irt = FLOAT_TO_FP(0.41),
	.saturation.again = TCS_DEFAULT_AGAIN,
	.saturation.atime = TCS_DEFAULT_ATIME,
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
		.rot_standard_ref = &lid_standard_ref, /* identity matrix */
		.default_range = 2, /* g */
		.min_frequency = BMA255_ACCEL_MIN_FREQ,
		.max_frequency = BMA255_ACCEL_MAX_FREQ,
		.config = {
			/* EC use accel for angle detection */
			[SENSOR_CONFIG_EC_S0] = {
				.odr = 10000 | ROUND_UP_FLAG,
			},
			/* Sensor on for lid angle detection */
			[SENSOR_CONFIG_EC_S3] = {
				.odr = 10000 | ROUND_UP_FLAG,
			},
		},
	},

	[BASE_ACCEL] = {
		.name = "Base Accel",
		.active_mask = SENSOR_ACTIVE_S0_S3,
		.chip = MOTIONSENSE_CHIP_LSM6DSM,
		.type = MOTIONSENSE_TYPE_ACCEL,
		.location = MOTIONSENSE_LOC_BASE,
		.drv = &lsm6dsm_drv,
		.mutex = &g_base_accel_mutex,
		.drv_data = LSM6DSM_ST_DATA(lsm6dsm_data,
				MOTIONSENSE_TYPE_ACCEL),
		.port = I2C_PORT_SENSOR,
		.i2c_spi_addr_flags = LSM6DSM_ADDR0_FLAGS,
		.rot_standard_ref = &base_standard_ref,
		.default_range = 4,  /* g */
		.min_frequency = LSM6DSM_ODR_MIN_VAL,
		.max_frequency = LSM6DSM_ODR_MAX_VAL,
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
		.chip = MOTIONSENSE_CHIP_LSM6DSM,
		.type = MOTIONSENSE_TYPE_GYRO,
		.location = MOTIONSENSE_LOC_BASE,
		.drv = &lsm6dsm_drv,
		.mutex = &g_base_accel_mutex,
		.drv_data = LSM6DSM_ST_DATA(lsm6dsm_data,
				MOTIONSENSE_TYPE_GYRO),
		.port = I2C_PORT_SENSOR,
		.i2c_spi_addr_flags = LSM6DSM_ADDR0_FLAGS,
		.default_range = 1000 | ROUND_UP_FLAG, /* dps */
		.rot_standard_ref = &base_standard_ref,
		.min_frequency = LSM6DSM_ODR_MIN_VAL,
		.max_frequency = LSM6DSM_ODR_MAX_VAL,
	},

	[CLEAR_ALS] = {
		.name = "Clear Light",
		.active_mask = SENSOR_ACTIVE_S0_S3,
		.chip = MOTIONSENSE_CHIP_TCS3400,
		.type = MOTIONSENSE_TYPE_LIGHT,
		.location = MOTIONSENSE_LOC_CAMERA,
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
		.location = MOTIONSENSE_LOC_CAMERA,
		.drv = &tcs3400_rgb_drv,
		.drv_data = &g_tcs3400_rgb_data,
		.rot_standard_ref = NULL,
		.default_range = 0x10000, /* scale = 1x, uscale = 0 */
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
	.mutex = &g_lid_accel_mutex,
	.drv_data = &g_bma422_data,
	.port = I2C_PORT_SENSOR,
	.i2c_spi_addr_flags = BMA4_I2C_ADDR_PRIMARY,
	.rot_standard_ref = &lid_standard_ref,
	.default_range = 2, /* g, enough for laptop. */
	.min_frequency = BMA4_ACCEL_MIN_FREQ,
	.max_frequency = BMA4_ACCEL_MAX_FREQ,
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
	if (get_cbi_ssfc_lid_sensor() == SSFC_SENSOR_LID_BMA422)
		motion_sensors[LID_ACCEL] = bma422_lid_accel;
}

/* ALS instances when LPC mapping is needed. Each entry directs to a sensor. */
const struct motion_sensor_t *motion_als_sensors[] = {
	&motion_sensors[CLEAR_ALS],
};
BUILD_ASSERT(ARRAY_SIZE(motion_als_sensors) == ALS_COUNT);

static void board_sensors_init(void)
{
	/* Enable interrupt for the TCS3400 color light sensor */
	gpio_enable_interrupt(GPIO_EC_ALS_RGB_INT_R_L);
	/* Enable gpio interrupt for base accelgyro sensor */
	gpio_enable_interrupt(GPIO_EC_IMU_INT_R_L);

	board_update_motion_sensor_config();
}
DECLARE_HOOK(HOOK_INIT, board_sensors_init, HOOK_PRIO_INIT_I2C + 1);

/* Temperature sensor configuration */
const struct temp_sensor_t temp_sensors[] = {
	[TEMP_SENSOR_1_DDR] = { .name = "DDR",
				.type = TEMP_SENSOR_TYPE_BOARD,
				.read = get_temp_3v3_30k9_47k_4050b,
				.idx = ADC_TEMP_SENSOR_1_DDR },
	[TEMP_SENSOR_2_SOC] = { .name = "SOC",
				.type = TEMP_SENSOR_TYPE_BOARD,
				.read = get_temp_3v3_30k9_47k_4050b,
				.idx = ADC_TEMP_SENSOR_2_SOC },
	[TEMP_SENSOR_3_CHARGER] = { .name = "Charger",
				    .type = TEMP_SENSOR_TYPE_BOARD,
				    .read = get_temp_3v3_30k9_47k_4050b,
				    .idx = ADC_TEMP_SENSOR_3_CHARGER },
	[TEMP_SENSOR_4_REGULATOR] = { .name = "Regulator",
				      .type = TEMP_SENSOR_TYPE_BOARD,
				      .read = get_temp_3v3_30k9_47k_4050b,
				      .idx = ADC_TEMP_SENSOR_4_REGULATOR },
};
BUILD_ASSERT(ARRAY_SIZE(temp_sensors) == TEMP_SENSOR_COUNT);

static const struct ec_thermal_config thermal_ddr = {
	.temp_host = {
		[EC_TEMP_THRESH_HIGH] = C_TO_K(75),
		[EC_TEMP_THRESH_HALT] = C_TO_K(80),
	},
	.temp_host_release = {
		[EC_TEMP_THRESH_HIGH] = C_TO_K(70),
	},
};

/*
 * Tiger Lake specifies 100 C as maximum TDP temperature.  THRMTRIP# occurs at
 * 130 C.  However, sensor is located next to SOC, so we need to use the lower
 * SOC temperature limit (85 C)
 */
static const struct ec_thermal_config thermal_cpu = {
	.temp_host = {
		[EC_TEMP_THRESH_HIGH] = C_TO_K(75),
		[EC_TEMP_THRESH_HALT] = C_TO_K(80),
	},
	.temp_host_release = {
		[EC_TEMP_THRESH_HIGH] = C_TO_K(70),
	},
};

static const struct ec_thermal_config thermal_charger = {
	.temp_host = {
		[EC_TEMP_THRESH_HIGH] = C_TO_K(80),
		[EC_TEMP_THRESH_HALT] = C_TO_K(85),
	},
	.temp_host_release = {
		[EC_TEMP_THRESH_HIGH] = C_TO_K(75),
	},
};

static const struct ec_thermal_config thermal_regulator = {
	.temp_host = {
		[EC_TEMP_THRESH_HIGH] = C_TO_K(80),
		[EC_TEMP_THRESH_HALT] = C_TO_K(85),
	},
	.temp_host_release = {
		[EC_TEMP_THRESH_HIGH] = C_TO_K(75),
	},
};

/* this should really be "const" */
struct ec_thermal_config thermal_params[] = {
	[TEMP_SENSOR_1_DDR] = thermal_ddr,
	[TEMP_SENSOR_2_SOC] = thermal_cpu,
	[TEMP_SENSOR_3_CHARGER] = thermal_charger,
	[TEMP_SENSOR_4_REGULATOR] = thermal_regulator,
};
BUILD_ASSERT(ARRAY_SIZE(thermal_params) == TEMP_SENSOR_COUNT);
