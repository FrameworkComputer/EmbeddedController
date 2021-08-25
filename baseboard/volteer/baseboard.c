/* Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Volteer family-specific configuration */
#include "adc.h"
#include "button.h"
#include "cbi_ec_fw_config.h"
#include "charger.h"
#include "charge_ramp.h"
#include "cros_board_info.h"
#include "driver/charger/isl9241.h"
#include "driver/tcpm/ps8xxx.h"
#include "driver/temp_sensor/thermistor.h"
#include "gpio.h"
#include "i2c.h"
#include "keyboard_scan.h"
#include "system.h"
#include "task.h"
#include "temp_sensor.h"
#ifdef CONFIG_ZEPHYR
#include "usbc_config.h"
#endif

#define CPRINTS(format, args...) cprints(CC_CHIPSET, format, ## args)
#define CPRINTF(format, args...) cprintf(CC_CHIPSET, format, ## args)

/******************************************************************************/
/* ADC configuration */
const struct adc_t adc_channels[] = {
	[ADC_TEMP_SENSOR_1_CHARGER] = {
		.name = "TEMP_CHARGER",
		.input_ch = NPCX_ADC_CH0,
		.factor_mul = ADC_MAX_VOLT,
		.factor_div = ADC_READ_MAX + 1,
		.shift = 0,
	},
	[ADC_TEMP_SENSOR_2_PP3300_REGULATOR] = {
		.name = "TEMP_PP3300_REGULATOR",
		.input_ch = NPCX_ADC_CH1,
		.factor_mul = ADC_MAX_VOLT,
		.factor_div = ADC_READ_MAX + 1,
		.shift = 0,
	},
	[ADC_TEMP_SENSOR_3_DDR_SOC] = {
		.name = "TEMP_DDR_SOC",
		.input_ch = NPCX_ADC_CH8,
		.factor_mul = ADC_MAX_VOLT,
		.factor_div = ADC_READ_MAX + 1,
		.shift = 0,
	},
	[ADC_TEMP_SENSOR_4_FAN] = {
		.name = "TEMP_FAN",
		.input_ch = NPCX_ADC_CH3,
		.factor_mul = ADC_MAX_VOLT,
		.factor_div = ADC_READ_MAX + 1,
		.shift = 0,
	},
};
BUILD_ASSERT(ARRAY_SIZE(adc_channels) == ADC_CH_COUNT);

/******************************************************************************/
/* Wake up pins */
const enum gpio_signal hibernate_wake_pins[] = {
	GPIO_LID_OPEN,
	GPIO_ACOK_OD,
	GPIO_POWER_BUTTON_L,
	GPIO_EC_RST_ODL,
};
const int hibernate_wake_pins_used = ARRAY_SIZE(hibernate_wake_pins);

/******************************************************************************/
/* Temperature sensor configuration */
const struct temp_sensor_t temp_sensors[] = {
	[TEMP_SENSOR_1_CHARGER] = {.name = "Charger",
				 .type = TEMP_SENSOR_TYPE_BOARD,
				 .read = get_temp_3v3_30k9_47k_4050b,
				 .idx = ADC_TEMP_SENSOR_1_CHARGER},
	[TEMP_SENSOR_2_PP3300_REGULATOR] = {.name = "PP3300 Regulator",
				 .type = TEMP_SENSOR_TYPE_BOARD,
				 .read = get_temp_3v3_30k9_47k_4050b,
				 .idx = ADC_TEMP_SENSOR_2_PP3300_REGULATOR},
	[TEMP_SENSOR_3_DDR_SOC] = {.name = "DDR and SOC",
				 .type = TEMP_SENSOR_TYPE_BOARD,
				 .read = get_temp_3v3_30k9_47k_4050b,
				 .idx = ADC_TEMP_SENSOR_3_DDR_SOC},
	[TEMP_SENSOR_4_FAN] = {.name = "Fan",
				 .type = TEMP_SENSOR_TYPE_BOARD,
				 .read = get_temp_3v3_30k9_47k_4050b,
				 .idx = ADC_TEMP_SENSOR_4_FAN},
};
BUILD_ASSERT(ARRAY_SIZE(temp_sensors) == TEMP_SENSOR_COUNT);
