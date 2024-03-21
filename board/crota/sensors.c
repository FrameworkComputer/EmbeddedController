/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "accelgyro.h"
#include "adc.h"
#include "common.h"
#include "console.h"
#include "driver/accel_lis2dw12.h"
#include "driver/accelgyro_bmi_common.h"
#include "driver/accelgyro_lsm6dso.h"
#include "fw_config.h"
#include "gpio.h"
#include "hooks.h"
#include "motion_sense.h"
#include "tablet_mode.h"
#include "temp_sensor.h"
#include "temp_sensor/thermistor.h"
#include "thermal.h"

/* ADC configuration */
const struct adc_t adc_channels[] = {
	[ADC_TEMP_SENSOR_1_SOC] = {
		.name = "TEMP_SOC",
		.input_ch = NPCX_ADC_CH0,
		.factor_mul = ADC_MAX_VOLT,
		.factor_div = ADC_READ_MAX + 1,
		.shift = 0,
	},
	[ADC_TEMP_SENSOR_2_DDR] = {
		.name = "TEMP_DDR",
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
	[ADC_TEMP_SENSOR_4_AMBIENT] = {
		.name = "TEMP_AMBIENT",
		.input_ch = NPCX_ADC_CH7,
		.factor_mul = ADC_MAX_VOLT,
		.factor_div = ADC_READ_MAX + 1,
		.shift = 0,
	},
};
BUILD_ASSERT(ARRAY_SIZE(adc_channels) == ADC_CH_COUNT);

static K_MUTEX_DEFINE(g_lid_accel_mutex);
static K_MUTEX_DEFINE(g_base_accel_mutex);
static struct stprivate_data g_lis2dw12_data;
static struct lsm6dso_data lsm6dso_data;
static struct bmi_drv_data_t g_bmi260_data;

static const mat33_fp_t lid_standard_ref = { { FLOAT_TO_FP(1), 0, 0 },
					     { 0, FLOAT_TO_FP(-1), 0 },
					     { 0, 0, FLOAT_TO_FP(-1) } };

static const mat33_fp_t base_standard_ref = { { FLOAT_TO_FP(-1), 0, 0 },
					      { 0, FLOAT_TO_FP(1), 0 },
					      { 0, 0, FLOAT_TO_FP(-1) } };

static const mat33_fp_t base_standard_ref_id_1 = { { 0, FLOAT_TO_FP(1), 0 },
						   { FLOAT_TO_FP(1), 0, 0 },
						   { 0, 0, FLOAT_TO_FP(-1) } };

struct motion_sensor_t motion_sensors[] = {
	[LID_ACCEL] = {
		.name = "Lid Accel",
		.active_mask = SENSOR_ACTIVE_S0_S3,
		.chip = MOTIONSENSE_CHIP_LIS2DW12,
		.type = MOTIONSENSE_TYPE_ACCEL,
		.location = MOTIONSENSE_LOC_LID,
		.drv = &lis2dw12_drv,
		.mutex = &g_lid_accel_mutex,
		.drv_data = &g_lis2dw12_data,
		.port = I2C_PORT_SENSOR,
		.i2c_spi_addr_flags = LIS2DW12_ADDR0,
		.rot_standard_ref = &lid_standard_ref, /* identity matrix */
		.default_range = 2, /* g */
		.min_frequency = LIS2DW12_ODR_MIN_VAL,
		.max_frequency = LIS2DW12_ODR_MAX_VAL,
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
		.chip = MOTIONSENSE_CHIP_LSM6DSO,
		.type = MOTIONSENSE_TYPE_ACCEL,
		.location = MOTIONSENSE_LOC_BASE,
		.drv = &lsm6dso_drv,
		.mutex = &g_base_accel_mutex,
		.drv_data = LSM6DSO_ST_DATA(lsm6dso_data,
				MOTIONSENSE_TYPE_ACCEL),
		.port = I2C_PORT_SENSOR,
		.i2c_spi_addr_flags = LSM6DSO_ADDR0_FLAGS,
		.rot_standard_ref = &base_standard_ref,
		.default_range = 4,  /* g */
		.min_frequency = LSM6DSO_ODR_MIN_VAL,
		.max_frequency = LSM6DSO_ODR_MAX_VAL,
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
		.chip = MOTIONSENSE_CHIP_LSM6DSO,
		.type = MOTIONSENSE_TYPE_GYRO,
		.location = MOTIONSENSE_LOC_BASE,
		.drv = &lsm6dso_drv,
		.mutex = &g_base_accel_mutex,
		.drv_data = LSM6DSO_ST_DATA(lsm6dso_data,
				MOTIONSENSE_TYPE_GYRO),
		.port = I2C_PORT_SENSOR,
		.i2c_spi_addr_flags = LSM6DSO_ADDR0_FLAGS,
		.default_range = 1000 | ROUND_UP_FLAG, /* dps */
		.rot_standard_ref = &base_standard_ref,
		.min_frequency = LSM6DSO_ODR_MIN_VAL,
		.max_frequency = LSM6DSO_ODR_MAX_VAL,
	},
};
const unsigned int motion_sensor_count = ARRAY_SIZE(motion_sensors);

struct motion_sensor_t bmi260_base_accel = {
	.name = "Base Accel",
	.active_mask = SENSOR_ACTIVE_S0_S3,
	.chip = MOTIONSENSE_CHIP_BMI260,
	.type = MOTIONSENSE_TYPE_ACCEL,
	.location = MOTIONSENSE_LOC_BASE,
	.drv = &bmi260_drv,
	.mutex = &g_base_accel_mutex,
	.drv_data = &g_bmi260_data,
	.port = I2C_PORT_SENSOR,
	.i2c_spi_addr_flags = BMI260_ADDR0_FLAGS,
	.rot_standard_ref = &base_standard_ref_id_1,
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
};
struct motion_sensor_t bmi260_base_gyro = {
	.name = "Base Gyro",
	.active_mask = SENSOR_ACTIVE_S0_S3,
	.chip = MOTIONSENSE_CHIP_BMI260,
	.type = MOTIONSENSE_TYPE_GYRO,
	.location = MOTIONSENSE_LOC_BASE,
	.drv = &bmi260_drv,
	.mutex = &g_base_accel_mutex,
	.drv_data = &g_bmi260_data,
	.port = I2C_PORT_SENSOR,
	.i2c_spi_addr_flags = BMI260_ADDR0_FLAGS,
	.default_range = 1000, /* dps */
	.rot_standard_ref = &base_standard_ref_id_1,
	.min_frequency = BMI_GYRO_MIN_FREQ,
	.max_frequency = BMI_GYRO_MAX_FREQ,
};

void motion_interrupt(enum gpio_signal signal)
{
	if (get_board_id() > 1) {
		if (gpio_get_level(GPIO_EC_SENSOR_STRAP) == 0)
			bmi260_interrupt(signal);
		else
			lsm6dso_interrupt(signal);
	} else
		lsm6dso_interrupt(signal);
}

static void board_update_motion_sensor_config(void)
{
	if (get_board_id() > 1 && gpio_get_level(GPIO_EC_SENSOR_STRAP) == 0) {
		motion_sensors[BASE_ACCEL] = bmi260_base_accel;
		motion_sensors[BASE_GYRO] = bmi260_base_gyro;
		ccprints("BASE IMU is BMI260");
	} else {
		ccprints("BASE IMU is LSM6DSO");
	}

	if (!board_is_convertible()) {
		tablet_set_mode(0, TABLET_TRIGGER_LID);
		gmr_tablet_switch_disable();
		/* Make sure tablet mode detection is not trigger by mistake. */
		gpio_set_flags(GPIO_TABLET_MODE_L, GPIO_INPUT | GPIO_PULL_UP);
		/*
		 * Make sure we don't even try to initialize the lid accel, it
		 * is not present.
		 */
		motion_sensors[LID_ACCEL].active_mask = 0;
		gpio_set_flags(GPIO_EC_ACCEL_INT_R_L,
			       GPIO_INPUT | GPIO_PULL_UP);
	}
}
DECLARE_HOOK(HOOK_CHIPSET_STARTUP, board_update_motion_sensor_config,
	     HOOK_PRIO_INIT_I2C + 1);

__override int sensor_board_is_lid_angle_available(void)
{
	return board_is_convertible();
}

static void baseboard_sensors_init(void)
{
	/* Enable gpio interrupt for lid accel sensor */
	gpio_enable_interrupt(GPIO_EC_ACCEL_INT_R_L);
	/* Enable gpio interrupt for base accelgyro sensor */
	gpio_enable_interrupt(GPIO_EC_IMU_INT_R_L);
}
DECLARE_HOOK(HOOK_INIT, baseboard_sensors_init, HOOK_PRIO_INIT_I2C + 1);

/* Temperature sensor configuration */
const struct temp_sensor_t temp_sensors[] = {
	[TEMP_SENSOR_1_SOC] = {
		.name = "SOC",
		.type = TEMP_SENSOR_TYPE_BOARD,
		.read = get_temp_3v3_30k9_47k_4050b,
		.idx = ADC_TEMP_SENSOR_1_SOC,
	},
	[TEMP_SENSOR_2_DDR] = {
		.name = "DDR",
		.type = TEMP_SENSOR_TYPE_BOARD,
		.read = get_temp_3v3_30k9_47k_4050b,
		.idx = ADC_TEMP_SENSOR_2_DDR,
	},
	[TEMP_SENSOR_3_CHARGER] = {
		.name = "Charger",
		.type = TEMP_SENSOR_TYPE_BOARD,
		.read = get_temp_3v3_30k9_47k_4050b,
		.idx = ADC_TEMP_SENSOR_3_CHARGER,
	},
	[TEMP_SENSOR_4_AMBIENT] = {
		.name = "Ambient",
		.type = TEMP_SENSOR_TYPE_BOARD,
		.read = get_temp_3v3_30k9_47k_4050b,
		.idx = ADC_TEMP_SENSOR_4_AMBIENT,
	},
};
BUILD_ASSERT(ARRAY_SIZE(temp_sensors) == TEMP_SENSOR_COUNT);

#define THERMAL_CPU              \
	{                        \
		.temp_host = { \
			[EC_TEMP_THRESH_HIGH] = C_TO_K(77), \
			[EC_TEMP_THRESH_HALT] = C_TO_K(80), \
		}, \
		.temp_host_release = { \
			[EC_TEMP_THRESH_HIGH] = C_TO_K(77), \
		}, \
		.temp_fan_off = C_TO_K(24), \
		.temp_fan_max = C_TO_K(51), \
	}
__maybe_unused static const struct ec_thermal_config thermal_cpu = THERMAL_CPU;

#define THERMAL_DDR              \
	{                        \
		.temp_host = { \
			[EC_TEMP_THRESH_HIGH] = C_TO_K(75), \
			[EC_TEMP_THRESH_HALT] = C_TO_K(78), \
		}, \
		.temp_host_release = { \
			[EC_TEMP_THRESH_HIGH] = C_TO_K(75), \
		}, \
		.temp_fan_off = C_TO_K(56), \
		.temp_fan_max = C_TO_K(59), \
	}
__maybe_unused static const struct ec_thermal_config thermal_ddr = THERMAL_DDR;

#define THERMAL_CHARGER          \
	{                        \
		.temp_host = { \
			[EC_TEMP_THRESH_HIGH] = C_TO_K(86), \
			[EC_TEMP_THRESH_HALT] = C_TO_K(89), \
		}, \
		.temp_host_release = { \
			[EC_TEMP_THRESH_HIGH] = C_TO_K(86), \
		}, \
		.temp_fan_off = C_TO_K(67), \
		.temp_fan_max = C_TO_K(70), \
	}
__maybe_unused static const struct ec_thermal_config thermal_charger =
	THERMAL_CHARGER;

#define THERMAL_AMBIENT          \
	{                        \
		.temp_host = { \
			[EC_TEMP_THRESH_HIGH] = C_TO_K(57), \
			[EC_TEMP_THRESH_HALT] = C_TO_K(60), \
		}, \
		.temp_host_release = { \
			[EC_TEMP_THRESH_HIGH] = C_TO_K(57), \
		}, \
		.temp_fan_off = C_TO_K(38), \
		.temp_fan_max = C_TO_K(45), \
	}
__maybe_unused static const struct ec_thermal_config thermal_ambient =
	THERMAL_AMBIENT;

struct ec_thermal_config thermal_params[] = {
	[TEMP_SENSOR_1_SOC] = THERMAL_CPU,
	[TEMP_SENSOR_2_DDR] = THERMAL_DDR,
	[TEMP_SENSOR_3_CHARGER] = THERMAL_CHARGER,
	[TEMP_SENSOR_4_AMBIENT] = THERMAL_AMBIENT,
};
BUILD_ASSERT(ARRAY_SIZE(thermal_params) == TEMP_SENSOR_COUNT);
