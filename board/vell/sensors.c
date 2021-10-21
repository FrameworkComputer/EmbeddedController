/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "accelgyro.h"
#include "adc.h"
#include "driver/als_tcs3400_public.h"
#include "hooks.h"
#include "motion_sense.h"
#include "temp_sensor.h"
#include "thermal.h"
#include "temp_sensor/thermistor.h"

/* ADC configuration */
struct adc_t adc_channels[] = {
	[ADC_TEMP_SENSOR_1_DDR_SOC] = {
		.name = "TEMP_DDR_SOC",
		.input_ch = NPCX_ADC_CH0,
		.factor_mul = ADC_MAX_VOLT,
		.factor_div = ADC_READ_MAX + 1,
		.shift = 0,
	},
	[ADC_TEMP_SENSOR_2_AMBIENT] = {
		.name = "TEMP_AMBIENT",
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
	[ADC_TEMP_SENSOR_4_WWAN] = {
		.name = "TEMP_WWAN",
		.input_ch = NPCX_ADC_CH7,
		.factor_mul = ADC_MAX_VOLT,
		.factor_div = ADC_READ_MAX + 1,
		.shift = 0,
	},
};
BUILD_ASSERT(ARRAY_SIZE(adc_channels) == ADC_CH_COUNT);

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
 * TODO: b/184702900 need to calibrate ALS/RGB sensor. At default settings,
 * shining phone flashlight on sensor pegs all readings at 0xFFFF.
 */
static struct tcs3400_rgb_drv_data_t g_tcs3400_rgb_data = {
	.calibration.rgb_cal[X] = {
		.offset = 0,
		.coeff[TCS_RED_COEFF_IDX] = FLOAT_TO_FP(0),
		.coeff[TCS_GREEN_COEFF_IDX] = FLOAT_TO_FP(0),
		.coeff[TCS_BLUE_COEFF_IDX] = FLOAT_TO_FP(0),
		.coeff[TCS_CLEAR_COEFF_IDX] = FLOAT_TO_FP(1.0),
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
		.coeff[TCS_CLEAR_COEFF_IDX] = FLOAT_TO_FP(1.0),
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
		.coeff[TCS_CLEAR_COEFF_IDX] = FLOAT_TO_FP(1.0),
		.scale = {
			.k_channel_scale = ALS_CHANNEL_SCALE(1.0), /* kb */
			.cover_scale = ALS_CHANNEL_SCALE(1.0)
		}
	},
	.calibration.irt = INT_TO_FP(1),
	.saturation.again = TCS_DEFAULT_AGAIN,
	.saturation.atime = TCS_DEFAULT_ATIME,
};

struct motion_sensor_t motion_sensors[] = {
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

/* ALS instances when LPC mapping is needed. Each entry directs to a sensor. */
const struct motion_sensor_t *motion_als_sensors[] = {
	&motion_sensors[CLEAR_ALS],
};
BUILD_ASSERT(ARRAY_SIZE(motion_als_sensors) == ALS_COUNT);

static void baseboard_sensors_init(void)
{
	/* Enable interrupt for the TCS3400 color light sensor */
	gpio_enable_interrupt(GPIO_EC_ALS_RGB_INT_R_L);
}
DECLARE_HOOK(HOOK_INIT, baseboard_sensors_init, HOOK_PRIO_INIT_I2C + 1);

/* Temperature sensor configuration */
const struct temp_sensor_t temp_sensors[] = {
	[TEMP_SENSOR_1_DDR_SOC] = {
		.name = "DDR and SOC",
		.type = TEMP_SENSOR_TYPE_BOARD,
		.read = get_temp_3v3_30k9_47k_4050b,
		.idx = ADC_TEMP_SENSOR_1_DDR_SOC,
	},
	[TEMP_SENSOR_2_AMBIENT] = {
		.name = "Ambient",
		.type = TEMP_SENSOR_TYPE_BOARD,
		.read = get_temp_3v3_30k9_47k_4050b,
		.idx = ADC_TEMP_SENSOR_2_AMBIENT,
	},
	[TEMP_SENSOR_3_CHARGER] = {
		.name = "Charger",
		.type = TEMP_SENSOR_TYPE_BOARD,
		.read = get_temp_3v3_30k9_47k_4050b,
		.idx = ADC_TEMP_SENSOR_3_CHARGER,
	},
	[TEMP_SENSOR_4_WWAN] = {
		.name = "WWAN",
		.type = TEMP_SENSOR_TYPE_BOARD,
		.read = get_temp_3v3_30k9_47k_4050b,
		.idx = ADC_TEMP_SENSOR_4_WWAN,
	},
};
BUILD_ASSERT(ARRAY_SIZE(temp_sensors) == TEMP_SENSOR_COUNT);

/*
 * TODO(b/180681346): update for Alder Lake/brya
 *
 * Alder Lake specifies 100 C as maximum TDP temperature.  THRMTRIP# occurs at
 * 130 C.  However, sensor is located next to DDR, so we need to use the lower
 * DDR temperature limit (85 C)
 */
/*
 * TODO(b/202062363): Remove when clang is fixed.
 */
#define THERMAL_CPU \
	{ \
		.temp_host = { \
			[EC_TEMP_THRESH_HIGH] = C_TO_K(85), \
			[EC_TEMP_THRESH_HALT] = C_TO_K(90), \
		}, \
		.temp_host_release = { \
			[EC_TEMP_THRESH_HIGH] = C_TO_K(80), \
		}, \
		.temp_fan_off = C_TO_K(35), \
		.temp_fan_max = C_TO_K(60), \
	}
__maybe_unused static const struct ec_thermal_config thermal_cpu = THERMAL_CPU;

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
/*
 * TODO(b/202062363): Remove when clang is fixed.
 */
#define THERMAL_AMBIENT \
	{ \
		.temp_host = { \
			[EC_TEMP_THRESH_HIGH] = C_TO_K(85), \
			[EC_TEMP_THRESH_HALT] = C_TO_K(90), \
		}, \
		.temp_host_release = { \
			[EC_TEMP_THRESH_HIGH] = C_TO_K(80), \
		}, \
		.temp_fan_off = C_TO_K(35), \
		.temp_fan_max = C_TO_K(60), \
	}
__maybe_unused static const struct ec_thermal_config thermal_ambient =
	THERMAL_AMBIENT;

/*
 * Inductor limits - used for both charger and PP3300 regulator
 *
 * Need to use the lower of the charger IC, PP3300 regulator, and the inductors
 *
 * Charger max recommended temperature 125C, max absolute temperature 150C
 * PP3300 regulator: operating range -40 C to 125 C
 *
 * Inductors: limit of 125c
 * PCB: limit is 80c
 */
/*
 * TODO(b/202062363): Remove when clang is fixed.
 */
#define THERMAL_CHARGER \
	{ \
		.temp_host = { \
			[EC_TEMP_THRESH_HIGH] = C_TO_K(105), \
			[EC_TEMP_THRESH_HALT] = C_TO_K(120), \
		}, \
		.temp_host_release = { \
			[EC_TEMP_THRESH_HIGH] = C_TO_K(90), \
		}, \
		.temp_fan_off = C_TO_K(35), \
		.temp_fan_max = C_TO_K(65), \
	}
__maybe_unused static const struct ec_thermal_config thermal_charger =
	THERMAL_CHARGER;

/*
 * TODO(b/180681346): update for brya WWAN module
 */
/*
 * TODO(b/202062363): Remove when clang is fixed.
 */
#define THERMAL_WWAN \
	{ \
		.temp_host = { \
			[EC_TEMP_THRESH_HIGH] = C_TO_K(130), \
			[EC_TEMP_THRESH_HALT] = C_TO_K(130), \
		}, \
		.temp_host_release = { \
			[EC_TEMP_THRESH_HIGH] = C_TO_K(100), \
		}, \
		.temp_fan_off = C_TO_K(35), \
		.temp_fan_max = C_TO_K(60), \
	}
__maybe_unused static const struct ec_thermal_config thermal_wwan =
	THERMAL_WWAN;

struct ec_thermal_config thermal_params[] = {
	[TEMP_SENSOR_1_DDR_SOC] = THERMAL_CPU,
	[TEMP_SENSOR_2_AMBIENT] = THERMAL_AMBIENT,
	[TEMP_SENSOR_3_CHARGER] = THERMAL_CHARGER,
	[TEMP_SENSOR_4_WWAN] = THERMAL_WWAN,
};
BUILD_ASSERT(ARRAY_SIZE(thermal_params) == TEMP_SENSOR_COUNT);

static void board_thermals_init(void)
{
	if (get_board_id() == 1) {
		/*
		 * Board ID 1 only has 3 sensors and the AMBIENT sensor
		 * ADC pins have been reassigned, so we're down to 2
		 * sensors that can easily be configured. So, alias the
		 * AMBIENT sensor ADC channel to the unimplemented ADC
		 * slots.
		 */
		adc_channels[ADC_TEMP_SENSOR_3_CHARGER].input_ch = NPCX_ADC_CH1;
		adc_channels[ADC_TEMP_SENSOR_4_WWAN].input_ch = NPCX_ADC_CH1;
	}
}

DECLARE_HOOK(HOOK_INIT, board_thermals_init, HOOK_PRIO_INIT_CHIPSET);
