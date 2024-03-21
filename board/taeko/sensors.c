/* Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "accelgyro.h"
#include "adc_chip.h"
#include "common.h"
#include "driver/accel_bma422.h"
#include "driver/accel_bma4xx.h"
#include "driver/accel_lis2dw12.h"
#include "driver/accelgyro_lsm6dsm.h"
#include "driver/accelgyro_lsm6dso.h"
#include "fw_config.h"
#include "gpio.h"
#include "hooks.h"
#include "i2c.h"
#include "motion_sense.h"
#include "tablet_mode.h"
#include "temp_sensor.h"
#include "temp_sensor/thermistor.h"
#include "thermal.h"

#if 0
#define CPRINTS(format, args...) ccprints(format, ##args)
#define CPRINTF(format, args...) ccprintf(format, ##args)
#else
#define CPRINTS(format, args...)
#define CPRINTF(format, args...)
#endif

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
	[ADC_TEMP_SENSOR_4_CPUCHOKE] = {
		.name = "CPU_CHOKE",
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
static struct accelgyro_saved_data_t g_bma422_data;
static struct lsm6dso_data lsm6dso_data;
static struct lsm6dsm_data lsm6dsm_data = LSM6DSM_DATA;

/* The matrix for new DB */
static const mat33_fp_t lid_ref_for_new_DB = { { FLOAT_TO_FP(-1), 0, 0 },
					       { 0, FLOAT_TO_FP(1), 0 },
					       { 0, 0, FLOAT_TO_FP(-1) } };
/* Matrix to rotate lid and base sensor into standard reference frame */
static const mat33_fp_t lid_standard_ref = { { 0, FLOAT_TO_FP(1), 0 },
					     { FLOAT_TO_FP(-1), 0, 0 },
					     { 0, 0, FLOAT_TO_FP(1) } };

static const mat33_fp_t base_standard_ref = { { FLOAT_TO_FP(-1), 0, 0 },
					      { 0, FLOAT_TO_FP(1), 0 },
					      { 0, 0, FLOAT_TO_FP(-1) } };

struct motion_sensor_t bma422_lid_accel = {
	.name = "Lid Accel - BMA",
	.active_mask = SENSOR_ACTIVE_S0_S3,
	.chip = MOTIONSENSE_CHIP_BMA422,
	.type = MOTIONSENSE_TYPE_ACCEL,
	.location = MOTIONSENSE_LOC_LID,
	.drv = &bma4_accel_drv,
	.mutex = &g_lid_accel_mutex,
	.drv_data = &g_bma422_data,
	.port = I2C_PORT_SENSOR,
	.i2c_spi_addr_flags = BMA4_I2C_ADDR_PRIMARY, /* 0x18 */
	.rot_standard_ref = &lid_standard_ref, /* identity matrix */
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
	.rot_standard_ref = &base_standard_ref,
	.default_range = 4,  /* g, to meet CDD 7.3.1/C-1-4 reqs */
	.min_frequency = LSM6DSM_ODR_MIN_VAL,
	.max_frequency = LSM6DSM_ODR_MAX_VAL,
	.config = {
		/* EC use accel for angle detection */
		[SENSOR_CONFIG_EC_S0] = {
			.odr = 13000 | ROUND_UP_FLAG,
			.ec_rate = 100 * MSEC,
		},
		/* Sensor on for angle detection */
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
	.drv_data = LSM6DSM_ST_DATA(lsm6dsm_data, MOTIONSENSE_TYPE_GYRO),
	.port = I2C_PORT_SENSOR,
	.i2c_spi_addr_flags = LSM6DSM_ADDR0_FLAGS,
	.default_range = 1000 | ROUND_UP_FLAG, /* dps */
	.rot_standard_ref = &base_standard_ref,
	.min_frequency = LSM6DSM_ODR_MIN_VAL,
	.max_frequency = LSM6DSM_ODR_MAX_VAL,
};

struct motion_sensor_t motion_sensors[] = {
	[LID_ACCEL] = {
		.name = "Lid Accel - ST",
		.active_mask = SENSOR_ACTIVE_S0_S3,
		.chip = MOTIONSENSE_CHIP_LIS2DW12,
		.type = MOTIONSENSE_TYPE_ACCEL,
		.location = MOTIONSENSE_LOC_LID,
		.drv = &lis2dw12_drv,
		.mutex = &g_lid_accel_mutex,
		.drv_data = &g_lis2dw12_data,
		.port = I2C_PORT_SENSOR,
		.i2c_spi_addr_flags = LIS2DW12_ADDR1, /* 0x19 */
		.rot_standard_ref = &lid_standard_ref, /* identity matrix */
		.default_range = 2, /* g */
		.min_frequency = LIS2DW12_ODR_MIN_VAL,
		.max_frequency = LIS2DW12_ODR_MAX_VAL,
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
		.chip = MOTIONSENSE_CHIP_LSM6DSO,
		.type = MOTIONSENSE_TYPE_ACCEL,
		.location = MOTIONSENSE_LOC_BASE,
		.drv = &lsm6dso_drv,
		.mutex = &g_base_accel_mutex,
		.drv_data = &lsm6dso_data,
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
		.drv_data = &lsm6dso_data,
		.port = I2C_PORT_SENSOR,
		.i2c_spi_addr_flags = LSM6DSO_ADDR0_FLAGS,
		.default_range = 1000 | ROUND_UP_FLAG, /* dps */
		.rot_standard_ref = &base_standard_ref,
		.min_frequency = LSM6DSO_ODR_MIN_VAL,
		.max_frequency = LSM6DSO_ODR_MAX_VAL,
	},
};

unsigned int motion_sensor_count = ARRAY_SIZE(motion_sensors);

static void board_detect_motionsensor(void)
{
	int ret;
	int val;

	if (chipset_in_state(CHIPSET_STATE_ANY_OFF))
		return;

	/*
	 * b/194765820 - Dynamic motion sensor count
	 * All board supports tablet mode if board id > 0
	 */
	if (get_board_id() == 0 && !ec_cfg_has_tabletmode())
		return;

	/* Check lid accel chip */
	ret = i2c_read8(I2C_PORT_SENSOR, LIS2DW12_ADDR1, LIS2DW12_WHO_AM_I_REG,
			&val);
	if (ret == 0 && val == LIS2DW12_WHO_AM_I) {
		CPRINTS("LID_ACCEL is LIS2DW12");
		return;
	}

	ret = i2c_read8(I2C_PORT_SENSOR, BMA4_I2C_ADDR_PRIMARY,
			BMA4_CHIP_ID_ADDR, &val);
	if (ret == 0 && val == BMA422_CHIP_ID) {
		CPRINTS("LID_ACCEL is BMA422");
		motion_sensors[LID_ACCEL] = bma422_lid_accel;
		/*
		 * The driver for BMA422 doesn't have code to support
		 * INT1. So, it doesn't need to enable interrupt.
		 * Vendor recommend to configure EC gpio as high-z if
		 * we don't use INT1. Keep this pin as input w/o enable
		 * interrupt.
		 */
		if (get_board_id() >= 2) {
			/* Need to change matrix when board ID >= 2 */
			bma422_lid_accel.rot_standard_ref = &lid_ref_for_new_DB;
		}
		return;
	}

	/* Lid accel is not stuffed, don't allow line to float */
	gpio_disable_interrupt(GPIO_EC_ACCEL_INT_R_L);
	gpio_set_flags(GPIO_EC_ACCEL_INT_R_L, GPIO_INPUT | GPIO_PULL_DOWN);
	CPRINTS("No LID_ACCEL are detected");
}
DECLARE_HOOK(HOOK_CHIPSET_STARTUP, board_detect_motionsensor,
	     HOOK_PRIO_DEFAULT);

static void baseboard_sensors_init(void)
{
	CPRINTS("baseboard_sensors_init");
	/* b/194765820
	 * Dynamic motion sensor count
	 * All board supports tablet mode if board id > 0
	 */
	if (ec_cfg_has_tabletmode()) {
		/* Change Request (b/199529373)
		 * GYRO sensor change from ST LSM6DSOETR3TR to ST LSM6DS3TR-C
		 *	LSM6DSOETR3TR base accel/gyro if board id = 0
		 *	LSM6DS3TR-C Base accel/gyro if board id > 0
		 */
		if (get_board_id() > 0) {
			motion_sensors[BASE_ACCEL] = lsm6dsm_base_accel;
			motion_sensors[BASE_GYRO] = lsm6dsm_base_gyro;
		}

		if (get_board_id() >= 2) {
			/* Need to change matrix when board ID >= 2 */
			motion_sensors[LID_ACCEL].rot_standard_ref =
				&lid_ref_for_new_DB;
		}

		/* Enable gpio interrupt for base accelgyro sensor */
		gpio_enable_interrupt(GPIO_EC_IMU_INT_R_L);
	} else {
		CPRINTS("Clamshell");
		motion_sensor_count = 0;
		gmr_tablet_switch_disable();
		gpio_set_flags(GPIO_TABLET_MODE_L, GPIO_INPUT | GPIO_PULL_DOWN);
		/* Gyro is not present, don't allow line to float */
		gpio_set_flags(GPIO_EC_IMU_INT_R_L,
			       GPIO_INPUT | GPIO_PULL_DOWN);
	}
}
DECLARE_HOOK(HOOK_INIT, baseboard_sensors_init, HOOK_PRIO_INIT_I2C + 1);

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
	[TEMP_SENSOR_4_CPUCHOKE] = { .name = "CPU CHOKE",
				     .type = TEMP_SENSOR_TYPE_BOARD,
				     .read = get_temp_3v3_30k9_47k_4050b,
				     .idx = ADC_TEMP_SENSOR_4_CPUCHOKE },
};

BUILD_ASSERT(ARRAY_SIZE(temp_sensors) == TEMP_SENSOR_COUNT);

/*
 * TODO(b/201021109): update for Alder Lake/brya
 *
 * Tiger Lake specifies 100 C as maximum TDP temperature.  THRMTRIP# occurs at
 * 130 C.  However, sensor is located next to DDR, so we need to use the lower
 * DDR temperature limit (100 C)
 */
static const struct ec_thermal_config thermal_cpu = {
	.temp_host = {
		[EC_TEMP_THRESH_HIGH] = C_TO_K(90),
		[EC_TEMP_THRESH_HALT] = C_TO_K(100),
	},
	.temp_host_release = {
		[EC_TEMP_THRESH_HIGH] = C_TO_K(85),
	},
	.temp_fan_off = C_TO_K(35),
		.temp_fan_max = C_TO_K(70),
};

/*
 * TODO(b/201021109): update for Alder Lake/brya
 *
 * Inductor limits - used for both charger and PP3300 regulator
 *
 * Need to use the lower of the charger IC, PP3300 regulator, and the inductors
 *
 * Charger max recommended temperature 100C, max absolute temperature 125C
 * PP3300 regulator: operating range -40 C to 145 C
 *
 * Inductors: limit of 125c
 * PCB: limit is 100c
 */
static const struct ec_thermal_config thermal_fan = {
	.temp_host = {
		[EC_TEMP_THRESH_HIGH] = C_TO_K(90),
		[EC_TEMP_THRESH_HALT] = C_TO_K(100),
	},
	.temp_host_release = {
		[EC_TEMP_THRESH_HIGH] = C_TO_K(85),
	},
	.temp_fan_off = C_TO_K(35),
		.temp_fan_max = C_TO_K(70),
};

/* this should really be "const" */
struct ec_thermal_config thermal_params[] = {
	[TEMP_SENSOR_1_DDR_SOC] = thermal_cpu,
	[TEMP_SENSOR_2_FAN] = thermal_fan,
	[TEMP_SENSOR_3_CHARGER] = thermal_fan,
	[TEMP_SENSOR_4_CPUCHOKE] = thermal_fan,
};

BUILD_ASSERT(ARRAY_SIZE(thermal_params) == TEMP_SENSOR_COUNT);
