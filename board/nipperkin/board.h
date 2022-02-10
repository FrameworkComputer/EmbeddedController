/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Guybrush board configuration */

#ifndef __CROS_EC_BOARD_H
#define __CROS_EC_BOARD_H

/* Baseboard features */
#include "baseboard.h"

/* Motion sensing drivers */

/* Keyboard features */
#define CONFIG_KEYBOARD_FACTORY_TEST
#define CONFIG_KEYBOARD_REFRESH_ROW3

/* EC console commands */
#define CONFIG_CMD_BUTTON

/* USB Type C and USB PD defines */
#define PD_OPERATING_POWER_MW   15000
#define PD_MAX_CURRENT_MA       5000
#define PD_MAX_VOLTAGE_MV       20000
/* Max Power = 100 W */
#define PD_MAX_POWER_MW         ((PD_MAX_VOLTAGE_MV * PD_MAX_CURRENT_MA) / 1000)

#define CONFIG_CHARGER_PROFILE_OVERRIDE

/* USB Type A Features */

/* BC 1.2 */

/* Volume Button feature */

/* Fan features */
#define CONFIG_CUSTOM_FAN_CONTROL
#define RPM_DEVIATION 1

/* LED features */
#define CONFIG_LED_COMMON
#define CONFIG_LED_ONOFF_STATES

/* Thermal Config */
#define CONFIG_TEMP_SENSOR_PCT2075

#ifndef __ASSEMBLER__

#include "gpio_signal.h"
#include "registers.h"

/* Battery Types */
enum battery_type {
	BATTERY_SIMPLO_HIGHPOWER,
	BATTERY_COSMX,
	BATTERY_TYPE_COUNT,
};

/* ADC Channels */
enum adc_channel {
	ADC_TEMP_SENSOR_MEMORY = 0,
	ADC_TEMP_SENSOR_CHARGER,
	ADC_TEMP_SENSOR_5V_REGULATOR,
	ADC_CORE_IMON1,
	ADC_SOC_IMON2,
	ADC_CH_COUNT
};

/* Temp Sensors */
enum temp_sensor_id {
	TEMP_SENSOR_SOC = 0,
	TEMP_SENSOR_CHARGER,
	TEMP_SENSOR_MEMORY,
	TEMP_SENSOR_5V_REGULATOR,
	TEMP_SENSOR_CPU,
	TEMP_SENSOR_AMBIENT,
	TEMP_SENSOR_COUNT
};

/* PCT2075 sensors */
enum pct2075_sensor {
	PCT2075_SOC,
	PCT2075_AMB,
	PCT2075_COUNT,
};

#endif /* !__ASSEMBLER__ */

#endif /* __CROS_EC_BOARD_H */
