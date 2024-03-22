/* Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "accelgyro.h"
#include "adc_chip.h"
#include "common.h"
#include "driver/accel_bma2x2.h"
#include "driver/accel_bma2x2_public.h"
#include "driver/accel_bma422.h"
#include "driver/accelgyro_bmi_common.h"
#include "driver/accelgyro_lsm6dsm.h"
#include "driver/tcpm/ps8xxx_public.h"
#include "driver/tcpm/tcpci.h"
#include "gpio.h"
#include "hooks.h"
#include "keyboard_scan.h"
#include "motion_sense.h"
#include "temp_sensor.h"
#include "temp_sensor/thermistor.h"
#include "thermal.h"

/* ADC configuration */
const struct adc_t adc_channels[] = {
	[ADC_TEMP_SENSOR_1_DDR_SOC] = {
		.name = "TEMP_DDR_SOC",
		.input_ch = NPCX_ADC_CH0,
		.factor_mul = ADC_MAX_VOLT,
		.factor_div = ADC_READ_MAX + 1,
		.shift = 0,
	},
	[ADC_TEMP_SENSOR_2_FAN] = {
		.name = "TEMP_FAN",
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
	[ADC_IADPT] = {
		.name = "CHARGER_IADPT",
		.input_ch = NPCX_ADC_CH3,
		.factor_mul = ADC_MAX_VOLT,
		.factor_div = ADC_READ_MAX + 1,
		.shift = 0,
	},
};
BUILD_ASSERT(ARRAY_SIZE(adc_channels) == ADC_CH_COUNT);

static K_MUTEX_DEFINE(g_lid_accel_mutex);
static K_MUTEX_DEFINE(g_base_accel_mutex);
/* BMA253 private data */
static struct accelgyro_saved_data_t g_bma253_data;

/* BMI160 private data */
static struct bmi_drv_data_t g_bmi160_data;

/* LSM6DSM private data */
static struct lsm6dsm_data lsm6dsm_data = LSM6DSM_DATA;

/* BMA422 private data */
static struct accelgyro_saved_data_t g_bma422_data;

/* TODO(b/192477578): calibrate the orientation matrix on later board stage */
static const mat33_fp_t lid_standard_ref = { { 0, FLOAT_TO_FP(1), 0 },
					     { FLOAT_TO_FP(1), 0, 0 },
					     { 0, 0, FLOAT_TO_FP(-1) } };

static const mat33_fp_t lid_standard_ref_id_1 = { { 0, FLOAT_TO_FP(1), 0 },
						  { FLOAT_TO_FP(1), 0, 0 },
						  { 0, 0, FLOAT_TO_FP(-1) } };

/* TODO(b/192477578): calibrate the orientation matrix on later board stage */
static const mat33_fp_t base_standard_ref = { { FLOAT_TO_FP(-1), 0, 0 },
					      { 0, FLOAT_TO_FP(-1), 0 },
					      { 0, 0, FLOAT_TO_FP(1) } };

static const mat33_fp_t base_standard_ref_id_1 = { { 0, FLOAT_TO_FP(1), 0 },
						   { FLOAT_TO_FP(-1), 0, 0 },
						   { 0, 0, FLOAT_TO_FP(1) } };

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
		.i2c_spi_addr_flags = BMA2x2_I2C_ADDR2_FLAGS,
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
		.chip = MOTIONSENSE_CHIP_BMI160,
		.type = MOTIONSENSE_TYPE_ACCEL,
		.location = MOTIONSENSE_LOC_BASE,
		.drv = &bmi160_drv,
		.mutex = &g_base_accel_mutex,
		.drv_data = &g_bmi160_data,
		.port = I2C_PORT_SENSOR,
		.i2c_spi_addr_flags = BMI160_ADDR0_FLAGS,
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
		.chip = MOTIONSENSE_CHIP_BMI160,
		.type = MOTIONSENSE_TYPE_GYRO,
		.location = MOTIONSENSE_LOC_BASE,
		.drv = &bmi160_drv,
		.mutex = &g_base_accel_mutex,
		.drv_data = &g_bmi160_data,
		.port = I2C_PORT_SENSOR,
		.i2c_spi_addr_flags = BMI160_ADDR0_FLAGS,
		.default_range = 1000, /* dps */
		.rot_standard_ref = &base_standard_ref,
		.min_frequency = BMI_GYRO_MIN_FREQ,
		.max_frequency = BMI_GYRO_MAX_FREQ,
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
	.i2c_spi_addr_flags = BMA4_I2C_ADDR_SECONDARY,
	.rot_standard_ref = &lid_standard_ref_id_1,
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

struct motion_sensor_t lsm6dsm_base_accel = {
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
	.rot_standard_ref = &base_standard_ref_id_1,
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
};

struct motion_sensor_t lsm6dsm_base_gyro = {
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
	.rot_standard_ref = &base_standard_ref_id_1,
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
};

void motion_interrupt(enum gpio_signal signal)
{
	if (get_board_id() >= 1)
		lsm6dsm_interrupt(signal);
	else
		bmi160_interrupt(signal);
}

static void update_sensor_array(void)
{
	if (get_board_id() >= 1) {
		motion_sensors[LID_ACCEL] = bma422_lid_accel;
		motion_sensors[BASE_ACCEL] = lsm6dsm_base_accel;
		motion_sensors[BASE_GYRO] = lsm6dsm_base_gyro;
		ccprints("LID ACCEL is BMA422");
		ccprints("BASE IMU is LSM6DSM");
	} else {
		ccprints("LID ACCEL is BMA253");
		ccprints("BASE IMU is BMI160");
	}
}
DECLARE_HOOK(HOOK_INIT, update_sensor_array, HOOK_PRIO_INIT_I2C);

static void baseboard_sensors_init(void)
{
	/* Enable gpio interrupt for base accelgyro sensor */
	gpio_enable_interrupt(GPIO_EC_IMU_INT_R_L);
}
DECLARE_HOOK(HOOK_INIT, baseboard_sensors_init, HOOK_PRIO_INIT_I2C + 1);

/* Temperature sensor configuration */
const struct temp_sensor_t temp_sensors[] = {
	[TEMP_SENSOR_1_DDR_SOC] = { .name = "DDR and SOC",
				    .type = TEMP_SENSOR_TYPE_BOARD,
				    .read = get_temp_3v3_30k9_47k_4050b,
				    .idx = ADC_TEMP_SENSOR_1_DDR_SOC },
	[TEMP_SENSOR_2_FAN] = { .name = "Fan",
				.type = TEMP_SENSOR_TYPE_BOARD,
				.read = get_temp_3v3_30k9_47k_4050b,
				.idx = ADC_TEMP_SENSOR_2_FAN },
	[TEMP_SENSOR_3_CHARGER] = { .name = "Charger",
				    .type = TEMP_SENSOR_TYPE_BOARD,
				    .read = get_temp_3v3_30k9_47k_4050b,
				    .idx = ADC_TEMP_SENSOR_3_CHARGER },
};
BUILD_ASSERT(ARRAY_SIZE(temp_sensors) == TEMP_SENSOR_COUNT);

/*
 * TODO(b/194318801): confirm thermal limits setting for gimble
 *
 * Tiger Lake specifies 100 C as maximum TDP temperature.  THRMTRIP# occurs at
 * 130 C.  However, sensor is located next to DDR, so we need to use the lower
 * DDR temperature limit (85 C)
 */
static const struct ec_thermal_config thermal_cpu = {
	.temp_host = {
		[EC_TEMP_THRESH_HIGH] = C_TO_K(70),
		[EC_TEMP_THRESH_HALT] = C_TO_K(80),
	},
	.temp_host_release = {
		[EC_TEMP_THRESH_HIGH] = C_TO_K(65),
	},
};

/*
 * TODO(b/194318801): confirm thermal limits setting for gimble
 *
 * Inductor limits - used for both charger and PP3300 regulator
 *
 * Need to use the lower of the charger IC, PP3300 regulator, and the inductors
 *
 * Charger max recommended temperature 100C, max absolute temperature 125C
 * PP3300 regulator: operating range -40 C to 145 C
 *
 * Inductors: limit of 125c
 * PCB: limit is 80c
 */
static const struct ec_thermal_config thermal_inductor = {
	.temp_host = {
		[EC_TEMP_THRESH_HIGH] = C_TO_K(75),
		[EC_TEMP_THRESH_HALT] = C_TO_K(80),
	},
	.temp_host_release = {
		[EC_TEMP_THRESH_HIGH] = C_TO_K(65),
	},
};

#define THERMAL_FAN_MISSING      \
	{                        \
		.temp_host = { \
			[EC_TEMP_THRESH_HIGH] = C_TO_K(75), \
			[EC_TEMP_THRESH_HALT] = C_TO_K(100), \
		}, \
		.temp_host_release = { \
			[EC_TEMP_THRESH_HIGH] = C_TO_K(65), \
		}, \
	}
__maybe_unused static const struct ec_thermal_config thermal_fan_missing =
	THERMAL_FAN_MISSING;

/* this should really be "const" */
struct ec_thermal_config thermal_params[] = {
	[TEMP_SENSOR_1_DDR_SOC] = thermal_cpu,
	/* TODO(b/194318801): confirm thermal limits setting for gimble */
	[TEMP_SENSOR_2_FAN] = thermal_inductor,
	[TEMP_SENSOR_3_CHARGER] = thermal_inductor,
};

struct ec_thermal_config temp_sensor_2_fan_set[] = {
	[TEMP_SENSOR_2_FAN] = THERMAL_FAN_MISSING,
};

static void config_thermal_params(void)
{
	int rv, val;

	rv = tcpc_addr_read16_no_lpm_exit(USBC_PORT_C1, PS8XXX_I2C_ADDR1_FLAGS,
					  TCPC_REG_VENDOR_ID, &val);

	if (rv != 0) {
		thermal_params[TEMP_SENSOR_2_FAN] =
			temp_sensor_2_fan_set[TEMP_SENSOR_2_FAN];
	}
}
DECLARE_HOOK(HOOK_INIT, config_thermal_params, HOOK_PRIO_INIT_I2C + 1);

BUILD_ASSERT(ARRAY_SIZE(thermal_params) == TEMP_SENSOR_COUNT);
