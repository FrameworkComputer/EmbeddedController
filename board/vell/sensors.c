/* Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "accelgyro.h"
#include "adc.h"
#include "common.h"
#include "driver/als_tcs3400_public.h"
#include "gpio.h"
#include "hooks.h"
#include "motion_sense.h"
#include "temp_sensor.h"
#include "temp_sensor/thermistor.h"
#include "thermal.h"

/* ADC configuration */
struct adc_t adc_channels[] = {
	[ADC_TEMP_SENSOR_1_SOC] = {
		.name = "TEMP_SOC",
		.input_ch = NPCX_ADC_CH0,
		.factor_mul = ADC_MAX_VOLT,
		.factor_div = ADC_READ_MAX + 1,
		.shift = 0,
	},
	[ADC_TEMP_SENSOR_2_CHARGER] = {
		.name = "TEMP_CHARGER",
		.input_ch = NPCX_ADC_CH1,
		.factor_mul = ADC_MAX_VOLT,
		.factor_div = ADC_READ_MAX + 1,
		.shift = 0,
	},
	[ADC_TEMP_SENSOR_3_WWAN] = {
		.name = "TEMP_WWAN",
		.input_ch = NPCX_ADC_CH6,
		.factor_mul = ADC_MAX_VOLT,
		.factor_div = ADC_READ_MAX + 1,
		.shift = 0,
	},
	[ADC_TEMP_SENSOR_4_DDR] = {
		.name = "TEMP_DDR",
		.input_ch = NPCX_ADC_CH7,
		.factor_mul = ADC_MAX_VOLT,
		.factor_div = ADC_READ_MAX + 1,
		.shift = 0,
	},
	[ADC_TEMP_SENSOR_5_REGULATOR] = {
		.name = "TEMP_REGULATOR",
		.input_ch = NPCX_ADC_CH4,
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
		.cover_scale = ALS_CHANNEL_SCALE(0.23),     /* CT */
	},
};

/*
 * TODO: b/184702900 need to calibrate ALS/RGB sensor. At default settings,
 * shining phone flashlight on sensor pegs all readings at 0xFFFF.
 */
static struct tcs3400_rgb_drv_data_t g_tcs3400_rgb_data = {
	.calibration.rgb_cal[X] = {
		.offset = 448, /* 447.5509362 */
		.coeff[TCS_RED_COEFF_IDX] = FLOAT_TO_FP(-0.45511034),
		.coeff[TCS_GREEN_COEFF_IDX] = FLOAT_TO_FP(-0.21956361),
		.coeff[TCS_BLUE_COEFF_IDX] = FLOAT_TO_FP(0.32628044),
		.coeff[TCS_CLEAR_COEFF_IDX] = FLOAT_TO_FP(0.3610898),
		.scale = {
			.k_channel_scale = ALS_CHANNEL_SCALE(1.0), /* kr */
			.cover_scale = ALS_CHANNEL_SCALE(0.08)
		}
	},
	.calibration.rgb_cal[Y] = {
		.offset = 436, /* 435.9025807*/
		.coeff[TCS_RED_COEFF_IDX] = FLOAT_TO_FP(-0.50765776),
		.coeff[TCS_GREEN_COEFF_IDX] = FLOAT_TO_FP(-0.34142269),
		.coeff[TCS_BLUE_COEFF_IDX] = FLOAT_TO_FP(0.55352908),
		.coeff[TCS_CLEAR_COEFF_IDX] = FLOAT_TO_FP(0.35923454),
		.scale = {
			.k_channel_scale = ALS_CHANNEL_SCALE(1.0), /* kg */
			.cover_scale = ALS_CHANNEL_SCALE(1.0)
		},
	},
	.calibration.rgb_cal[Z] = {
		.offset = 287, /* 286.51472391*/
		.coeff[TCS_RED_COEFF_IDX] = FLOAT_TO_FP(-0.11635731),
		.coeff[TCS_GREEN_COEFF_IDX] = FLOAT_TO_FP(-0.76700456),
		.coeff[TCS_BLUE_COEFF_IDX] = FLOAT_TO_FP(1.36663521),
		.coeff[TCS_CLEAR_COEFF_IDX] = FLOAT_TO_FP(0.18494607),
		.scale = {
			.k_channel_scale = ALS_CHANNEL_SCALE(1.0), /* kb */
			.cover_scale = ALS_CHANNEL_SCALE(0.54)
		}
	},
	.calibration.irt = FLOAT_TO_FP(0.06),
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
	[TEMP_SENSOR_1_SOC] = {
		.name = "SOC",
		.type = TEMP_SENSOR_TYPE_BOARD,
		.read = get_temp_3v3_30k9_47k_4050b,
		.idx = ADC_TEMP_SENSOR_1_SOC,
	},
	[TEMP_SENSOR_2_CHARGER] = {
		.name = "Charger",
		.type = TEMP_SENSOR_TYPE_BOARD,
		.read = get_temp_3v3_30k9_47k_4050b,
		.idx = ADC_TEMP_SENSOR_2_CHARGER,
	},
	[TEMP_SENSOR_3_WWAN] = {
		.name = "WWAN",
		.type = TEMP_SENSOR_TYPE_BOARD,
		.read = get_temp_3v3_30k9_47k_4050b,
		.idx = ADC_TEMP_SENSOR_3_WWAN,
	},
	[TEMP_SENSOR_4_DDR] = {
		.name = "DDR",
		.type = TEMP_SENSOR_TYPE_BOARD,
		.read = get_temp_3v3_30k9_47k_4050b,
		.idx = ADC_TEMP_SENSOR_4_DDR,
	},
	[TEMP_SENSOR_5_REGULATOR] = {
		.name = "Regulator",
		.type = TEMP_SENSOR_TYPE_BOARD,
		.read = get_temp_3v3_30k9_47k_4050b,
		.idx = ADC_TEMP_SENSOR_5_REGULATOR,
	},
};
BUILD_ASSERT(ARRAY_SIZE(temp_sensors) == TEMP_SENSOR_COUNT);

/*
 * TODO(b/203839956): update for Alder Lake/vell
 *
 * Alder Lake specifies 100 C as maximum TDP temperature.  THRMTRIP# occurs at
 * 130 C.  However, sensor is located next to DDR, so we need to use the lower
 * DDR temperature limit (85 C)
 */
static const struct ec_thermal_config thermal_cpu = {
	.temp_host = {
		[EC_TEMP_THRESH_HIGH] = C_TO_K(90),
		[EC_TEMP_THRESH_HALT] = C_TO_K(95),
	},
	.temp_host_release = {
		[EC_TEMP_THRESH_HIGH] = C_TO_K(85),
	},
};

/*
 * TODO(b/203839956): update for Alder Lake/vell
 */
static const struct ec_thermal_config thermal_charger = {
	.temp_host = {
		[EC_TEMP_THRESH_HIGH] = C_TO_K(90),
		[EC_TEMP_THRESH_HALT] = C_TO_K(95),
	},
	.temp_host_release = {
		[EC_TEMP_THRESH_HIGH] = C_TO_K(85),
	},
};

/*
 * TODO(b/203839956): update for vell WWAN module
 */
static const struct ec_thermal_config thermal_wwan = {
	.temp_host = {
		[EC_TEMP_THRESH_HIGH] = C_TO_K(70),
		[EC_TEMP_THRESH_HALT] = C_TO_K(80),
	},
	.temp_host_release = {
		[EC_TEMP_THRESH_HIGH] = C_TO_K(65),
	},
};

/*
 * TODO(b/203839956): update for Alder Lake/vell
 */
static const struct ec_thermal_config thermal_ddr = {
	.temp_host = {
		[EC_TEMP_THRESH_HIGH] = C_TO_K(80),
		[EC_TEMP_THRESH_HALT] = C_TO_K(85),
	},
	.temp_host_release = {
		[EC_TEMP_THRESH_HIGH] = C_TO_K(75),
	},
};

/*
 * TODO(b/203839956): update for Alder Lake/vell
 */
static const struct ec_thermal_config thermal_regulator = {
	.temp_host = {
		[EC_TEMP_THRESH_HIGH] = C_TO_K(80),
		[EC_TEMP_THRESH_HALT] = C_TO_K(85),
	},
	.temp_host_release = {
		[EC_TEMP_THRESH_HIGH] = C_TO_K(75),
	},
};

struct ec_thermal_config thermal_params[] = {
	[TEMP_SENSOR_1_SOC] = thermal_cpu,
	[TEMP_SENSOR_2_CHARGER] = thermal_charger,
	[TEMP_SENSOR_3_WWAN] = thermal_wwan,
	[TEMP_SENSOR_4_DDR] = thermal_ddr,
	[TEMP_SENSOR_5_REGULATOR] = thermal_regulator,
};
