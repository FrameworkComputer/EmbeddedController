/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "accelgyro.h"
#include "adc.h"
#include "common.h"
#include "driver/als_cm32183.h"
#include "hooks.h"
#include "motion_sense.h"
#include "temp_sensor.h"
#include "temp_sensor/thermistor.h"
#include "thermal.h"

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

};
BUILD_ASSERT(ARRAY_SIZE(adc_channels) == ADC_CH_COUNT);

/* CM32183 private data */
static struct als_drv_data_t g_cm32183_data = {
	/**
	 * adjusted_value = raw_value * scale + raw_value * uscale / 10000
	 * the coeficient we need is 3.2
	 */
	.als_cal.scale = 3,
	.als_cal.uscale = 2000,
	.als_cal.offset = 0,
	.als_cal.channel_scale = {
		/* TODO(b/219424210):  Calibrate ALS CM32183A3OP */
	},
};

struct motion_sensor_t motion_sensors[] = {
	[CLEAR_ALS] = {
		.name = "Clear Light",
		.active_mask = SENSOR_ACTIVE_S0_S3,
		.chip = MOTIONSENSE_CHIP_CM32183,
		.type = MOTIONSENSE_TYPE_LIGHT,
		.location = MOTIONSENSE_LOC_CAMERA,
		.drv = &cm32183_drv,
		.drv_data = &g_cm32183_data,
		.port = I2C_PORT_SENSOR,
		.i2c_spi_addr_flags = CM32183_I2C_ADDR,
		.rot_standard_ref = NULL,
		.default_range = 0x10000, /* scale = 1x, uscale = 0 */
		.config = {
			/* Run ALS sensor in S0 */
			[SENSOR_CONFIG_EC_S0] = {
				.odr = 1000,
			},
		},
	},
};
const unsigned int motion_sensor_count = ARRAY_SIZE(motion_sensors);

/* ALS instances when LPC mapping is needed. Each entry directs to a sensor. */
const struct motion_sensor_t *motion_als_sensors[] = {
	&motion_sensors[CLEAR_ALS],
};
BUILD_ASSERT(ARRAY_SIZE(motion_als_sensors) == ALS_COUNT);

/*TODO(b/208721153): check CM32183 interrupt method */

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

};
BUILD_ASSERT(ARRAY_SIZE(temp_sensors) == TEMP_SENSOR_COUNT);

/*
 * TODO(b/180681346): update for Alder Lake/brya
 *
 * Alder Lake specifies 100 C as maximum TDP temperature.  THRMTRIP# occurs at
 * 130 C.  However, sensor is located next to DDR, so we need to use the lower
 * DDR temperature limit (85 C)
 */
static const struct ec_thermal_config thermal_cpu = {
	.temp_host = {
		[EC_TEMP_THRESH_WARN] = C_TO_K(80),
		[EC_TEMP_THRESH_HIGH] = C_TO_K(90),
		[EC_TEMP_THRESH_HALT] = C_TO_K(100),
	},
	.temp_host_release = {
		[EC_TEMP_THRESH_WARN] = C_TO_K(75),
		[EC_TEMP_THRESH_HIGH] = C_TO_K(81),
	},
	.temp_fan_off = C_TO_K(50),
	.temp_fan_max = C_TO_K(58),
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
static const struct ec_thermal_config thermal_ambient = {
	.temp_host = {
		[EC_TEMP_THRESH_WARN] = C_TO_K(85),
		[EC_TEMP_THRESH_HIGH] = C_TO_K(95),
		[EC_TEMP_THRESH_HALT] = C_TO_K(105),
	},
	.temp_host_release = {
		[EC_TEMP_THRESH_WARN] = C_TO_K(70),
		[EC_TEMP_THRESH_HIGH] = C_TO_K(86),
	},
	.temp_fan_off = C_TO_K(50),
	.temp_fan_max = C_TO_K(58),
};

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
static const struct ec_thermal_config thermal_charger = {
	.temp_host = {
		[EC_TEMP_THRESH_WARN] = C_TO_K(90),
		[EC_TEMP_THRESH_HIGH] = C_TO_K(100),
		[EC_TEMP_THRESH_HALT] = C_TO_K(110),
	},
	.temp_host_release = {
		[EC_TEMP_THRESH_WARN] = C_TO_K(85),
		[EC_TEMP_THRESH_HIGH] = C_TO_K(91),
	},
	.temp_fan_off = C_TO_K(50),
	.temp_fan_max = C_TO_K(58),
};

/*
 * TODO(b/180681346): update for brya WWAN module
 */
__maybe_unused static const struct ec_thermal_config thermal_wwan = {
	.temp_host = {
		[EC_TEMP_THRESH_HIGH] = C_TO_K(130),
		[EC_TEMP_THRESH_HALT] = C_TO_K(130),
	},
	.temp_host_release = {
		[EC_TEMP_THRESH_HIGH] = C_TO_K(100),
	},
	.temp_fan_off = C_TO_K(35),
	.temp_fan_max = C_TO_K(60),
};

struct ec_thermal_config thermal_params[] = {
	[TEMP_SENSOR_1_DDR_SOC] = thermal_cpu,
	[TEMP_SENSOR_2_AMBIENT] = thermal_ambient,
	[TEMP_SENSOR_3_CHARGER] = thermal_charger,
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
	}
}

DECLARE_HOOK(HOOK_INIT, board_thermals_init, HOOK_PRIO_INIT_CHIPSET);
