/* Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "accelgyro.h"
#include "adc_chip.h"
#include "common.h"
#include "driver/accel_bma422.h"
#include "driver/accel_kionix.h"
#include "driver/accelgyro_bmi260.h"
#include "driver/accelgyro_bmi_common.h"
#include "driver/accelgyro_icm426xx.h"
#include "driver/accelgyro_icm_common.h"
#include "fw_config.h"
#include "gpio.h"
#include "hooks.h"
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
};
BUILD_ASSERT(ARRAY_SIZE(adc_channels) == ADC_CH_COUNT);

static K_MUTEX_DEFINE(g_lid_accel_mutex);
static K_MUTEX_DEFINE(g_base_accel_mutex);
static struct kionix_accel_data g_kx022_data;
static struct icm_drv_data_t g_icm426xx_data;
static struct bmi_drv_data_t g_bmi260_data;
static struct accelgyro_saved_data_t g_bma422_data;

enum base_accelgyro_type {
	BASE_GYRO_NONE = 0,
	BASE_GYRO_BMI260 = 1,
	BASE_GYRO_ICM426XX = 2,
};

static enum base_accelgyro_type base_accelgyro_config;

/*
 * TODO:(b/197200940): Verify lid and base orientation
 * matrix on proto board.
 */
static const mat33_fp_t lid_standard_ref = { { FLOAT_TO_FP(-1), 0, 0 },
					     { 0, FLOAT_TO_FP(1), 0 },
					     { 0, 0, FLOAT_TO_FP(-1) } };
static const mat33_fp_t base_standard_ref = { { FLOAT_TO_FP(1), 0, 0 },
					      { 0, FLOAT_TO_FP(1), 0 },
					      { 0, 0, FLOAT_TO_FP(1) } };

static const mat33_fp_t lid_bma422_standard_ref = { { 0, FLOAT_TO_FP(-1), 0 },
						    { FLOAT_TO_FP(-1), 0, 0 },
						    { 0, 0, FLOAT_TO_FP(-1) } };
static const mat33_fp_t base_bmi260_standard_ref = { { 0, FLOAT_TO_FP(1), 0 },
						     { FLOAT_TO_FP(-1), 0, 0 },
						     { 0, 0, FLOAT_TO_FP(1) } };

static struct motion_sensor_t bmi260_base_accel = {
	.name = "Base Accel",
	.active_mask = SENSOR_ACTIVE_S0_S3,
	.chip = MOTIONSENSE_CHIP_BMI260,
	.type = MOTIONSENSE_TYPE_ACCEL,
	.location = MOTIONSENSE_LOC_BASE,
	.drv = &bmi260_drv,
	.mutex = &g_base_accel_mutex,
	.drv_data = &g_bmi260_data,
	.port = I2C_PORT_ACCEL,
	.i2c_spi_addr_flags = BMI260_ADDR0_FLAGS,
	.rot_standard_ref = &base_bmi260_standard_ref,
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

static struct motion_sensor_t bmi260_base_gyro = {
	.name = "Base Gyro",
	.active_mask = SENSOR_ACTIVE_S0_S3,
	.chip = MOTIONSENSE_CHIP_BMI260,
	.type = MOTIONSENSE_TYPE_GYRO,
	.location = MOTIONSENSE_LOC_BASE,
	.drv = &bmi260_drv,
	.mutex = &g_base_accel_mutex,
	.drv_data = &g_bmi260_data,
	.port = I2C_PORT_ACCEL,
	.i2c_spi_addr_flags = BMI260_ADDR0_FLAGS,
	.default_range = 1000, /* dps */
	.rot_standard_ref = &base_bmi260_standard_ref,
	.min_frequency = BMI_GYRO_MIN_FREQ,
	.max_frequency = BMI_GYRO_MAX_FREQ,
};

static struct motion_sensor_t bma422_lid_accel = {
	.name = "Lid Accel",
	.active_mask = SENSOR_ACTIVE_S0_S3,
	.chip = MOTIONSENSE_CHIP_BMA422,
	.type = MOTIONSENSE_TYPE_ACCEL,
	.location = MOTIONSENSE_LOC_LID,
	.drv = &bma4_accel_drv,
	.mutex = &g_lid_accel_mutex,
	.drv_data = &g_bma422_data,
	.port = I2C_PORT_ACCEL,
	.i2c_spi_addr_flags = BMA4_I2C_ADDR_SECONDARY,
	.rot_standard_ref = &lid_bma422_standard_ref,
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

struct motion_sensor_t motion_sensors[] = {
	[LID_ACCEL] = {
		.name = "Lid Accel",
		.active_mask = SENSOR_ACTIVE_S0_S3,
		.chip = MOTIONSENSE_CHIP_KX022,
		.type = MOTIONSENSE_TYPE_ACCEL,
		.location = MOTIONSENSE_LOC_LID,
		.drv = &kionix_accel_drv,
		.mutex = &g_lid_accel_mutex,
		.drv_data = &g_kx022_data,
		.port = I2C_PORT_ACCEL,
		.i2c_spi_addr_flags = KX022_ADDR1_FLAGS,
		.rot_standard_ref = &lid_standard_ref, /* identity matrix */
		.default_range = 2, /* g */
		.min_frequency = KX022_ACCEL_MIN_FREQ,
		.max_frequency = KX022_ACCEL_MAX_FREQ,
		.config = {
			/* EC use accel for angle detection */
			[SENSOR_CONFIG_EC_S0] = {
				.odr = 12500 | ROUND_UP_FLAG,
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
		.chip = MOTIONSENSE_CHIP_ICM426XX,
		.type = MOTIONSENSE_TYPE_ACCEL,
		.location = MOTIONSENSE_LOC_BASE,
		.drv = &icm426xx_drv,
		.mutex = &g_base_accel_mutex,
		.drv_data = &g_icm426xx_data,
		.port = I2C_PORT_ACCEL,
		.i2c_spi_addr_flags = ICM426XX_ADDR0_FLAGS,
		.rot_standard_ref = &base_standard_ref,
		.default_range = 4,  /* g */
		.min_frequency = ICM426XX_ACCEL_MIN_FREQ,
		.max_frequency = ICM426XX_ACCEL_MAX_FREQ,
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
		.chip = MOTIONSENSE_CHIP_ICM426XX,
		.type = MOTIONSENSE_TYPE_GYRO,
		.location = MOTIONSENSE_LOC_BASE,
		.drv = &icm426xx_drv,
		.mutex = &g_base_accel_mutex,
		.drv_data = &g_icm426xx_data,
		.port = I2C_PORT_ACCEL,
		.i2c_spi_addr_flags = ICM426XX_ADDR0_FLAGS,
		.default_range = 1000 | ROUND_UP_FLAG, /* dps */
		.rot_standard_ref = &base_standard_ref,
		.min_frequency = ICM426XX_GYRO_MIN_FREQ,
		.max_frequency = ICM426XX_GYRO_MAX_FREQ,
	},
};
const unsigned int motion_sensor_count = ARRAY_SIZE(motion_sensors);

static void baseboard_sensors_detect(void)
{
	int ret, val;

	if (base_accelgyro_config != BASE_GYRO_NONE)
		return;

	ret = i2c_read8(I2C_PORT_ACCEL, BMA4_I2C_ADDR_SECONDARY,
			BMA4_CHIP_ID_ADDR, &val);
	if (ret == 0 && val == BMA422_CHIP_ID) {
		motion_sensors[LID_ACCEL] = bma422_lid_accel;
		ccprints("LID_ACCEL is BMA422");
	} else
		ccprints("LID_ACCEL is KX022");

	ret = bmi_read8(I2C_PORT_ACCEL, BMI260_ADDR0_FLAGS, BMI260_CHIP_ID,
			&val);
	if (ret == 0 && val == BMI260_CHIP_ID_MAJOR) {
		motion_sensors[BASE_ACCEL] = bmi260_base_accel;
		motion_sensors[BASE_GYRO] = bmi260_base_gyro;
		base_accelgyro_config = BASE_GYRO_BMI260;
		ccprints("BASE ACCEL is BMI260");
	} else {
		base_accelgyro_config = BASE_GYRO_ICM426XX;
		ccprints("BASE ACCEL IS ICM426XX");
	}
}
DECLARE_HOOK(HOOK_CHIPSET_STARTUP, baseboard_sensors_detect, HOOK_PRIO_DEFAULT);

static void baseboard_sensors_init(void)
{
	/* Enable gpio interrupt for base accelgyro sensor */
	gpio_enable_interrupt(GPIO_EC_IMU_INT_R_L);
}
DECLARE_HOOK(HOOK_INIT, baseboard_sensors_init, HOOK_PRIO_INIT_I2C + 1);

void motion_interrupt(enum gpio_signal signal)
{
	if (base_accelgyro_config == BASE_GYRO_NONE)
		return;
	if (base_accelgyro_config == BASE_GYRO_BMI260)
		bmi260_interrupt(signal);
	else
		icm426xx_interrupt(signal);
}

/* Temperature sensor configuration */
const struct temp_sensor_t temp_sensors[] = {
	[TEMP_SENSOR_1_DDR_SOC] = { .name = "DDR and SOC",
				    .type = TEMP_SENSOR_TYPE_BOARD,
				    .read = get_temp_3v3_30k9_47k_4050b,
				    .idx = ADC_TEMP_SENSOR_1_DDR_SOC },
	[TEMP_SENSOR_2_FAN] = { .name = "FAN",
				.type = TEMP_SENSOR_TYPE_BOARD,
				.read = get_temp_3v3_30k9_47k_4050b,
				.idx = ADC_TEMP_SENSOR_2_FAN },
	[TEMP_SENSOR_3_CHARGER] = { .name = "CHARGER",
				    .type = TEMP_SENSOR_TYPE_BOARD,
				    .read = get_temp_3v3_30k9_47k_4050b,
				    .idx = ADC_TEMP_SENSOR_3_CHARGER },
};
BUILD_ASSERT(ARRAY_SIZE(temp_sensors) == TEMP_SENSOR_COUNT);

/*
 * TODO(b/180681346): update for Alder Lake/brya
 *
 * Tiger Lake specifies 100 C as maximum TDP temperature.  THRMTRIP# occurs at
 * 130 C.  However, sensor is located next to DDR, so we need to use the lower
 * DDR temperature limit (85 C)
 */
static const struct ec_thermal_config thermal_cpu = {
	.temp_host = {
			[EC_TEMP_THRESH_HIGH] = 0,
			[EC_TEMP_THRESH_HALT] = 0,
	},
	.temp_host_release = {
			[EC_TEMP_THRESH_HIGH] = 0,
	},
		.temp_fan_off = 0,
		.temp_fan_max = 0,
};

/*
 * TODO(b/180681346): update for Alder Lake/brya
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
static const struct ec_thermal_config thermal_fan = {
	.temp_host = {
			[EC_TEMP_THRESH_HIGH] = C_TO_K(85),
			[EC_TEMP_THRESH_HALT] = C_TO_K(90),
	},
	.temp_host_release = {
			[EC_TEMP_THRESH_HIGH] = C_TO_K(68),
	},
		.temp_fan_off = C_TO_K(37),
		.temp_fan_max = C_TO_K(90),
};

static const struct ec_thermal_config thermal_fan_28w = {
	.temp_host = {
		[EC_TEMP_THRESH_HIGH] = C_TO_K(85),
		[EC_TEMP_THRESH_HALT] = C_TO_K(90),
	},
	.temp_host_release = {
		[EC_TEMP_THRESH_HIGH] = C_TO_K(68),
	},
	.temp_fan_off = C_TO_K(37),
	.temp_fan_max = C_TO_K(62),
};

static const struct ec_thermal_config thermal_charger = {
	.temp_host = {
		[EC_TEMP_THRESH_HIGH] = 0,
		[EC_TEMP_THRESH_HALT] = 0,
	},
	.temp_host_release = {
		[EC_TEMP_THRESH_HIGH] = 0,
	},
	.temp_fan_off = 0,
	.temp_fan_max = 0,
};

/* this should really be "const" */
struct ec_thermal_config thermal_params[] = {
	[TEMP_SENSOR_1_DDR_SOC] = thermal_cpu,
	[TEMP_SENSOR_2_FAN] = thermal_fan,
	[TEMP_SENSOR_3_CHARGER] = thermal_charger,
};
BUILD_ASSERT(ARRAY_SIZE(thermal_params) == TEMP_SENSOR_COUNT);

static void setup_thermal(void)
{
	unsigned int table = ec_cfg_thermal_solution();
	/* Configure Fan */
	switch (table) {
	/* 28w CPU fan table */
	case THERMAL_SOLUTION_28W:
		cprints(CC_THERMAL, "Fan table set to 28w CPU scheme");
		thermal_params[TEMP_SENSOR_2_FAN] = thermal_fan_28w;
		break;
	/* Default fan table */
	case THERMAL_SOLUTION_15W:
	default:
		cprints(CC_THERMAL, "Fan table set to 15w CPU scheme");
		break;
	}
}
/* setup_thermal should be called before HOOK_INIT/HOOK_PRIO_DEFAULT */
DECLARE_HOOK(HOOK_INIT, setup_thermal, HOOK_PRIO_DEFAULT - 1);
