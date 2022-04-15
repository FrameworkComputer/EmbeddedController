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

/* Tablet Mode Switch */
#define CONFIG_TABLET_MODE_SWITCH

/* Sensors */
#define CONFIG_ACCELGYRO_BMI260
#define CONFIG_ACCELGYRO_BMI260_INT_EVENT \
	TASK_EVENT_MOTION_SENSOR_INTERRUPT(BASE_ACCEL)
#define CONFIG_ACCEL_INTERRUPTS
#define CONFIG_ACCEL_BMA4XX

#define I2C_PORT_ACCEL      I2C_PORT_SENSOR

/* EC console commands */
#define CONFIG_CMD_ACCELS
#define CONFIG_CMD_ACCEL_INFO
#define CONFIG_CMD_BUTTON

/* USB Type C and USB PD defines */
#define CONFIG_USB_MUX_ANX7451
#define CONFIG_USBC_RETIMER_ANX7451

#define PD_OPERATING_POWER_MW   15000
#define PD_MAX_CURRENT_MA       3250
#define PD_MAX_VOLTAGE_MV       20000
/* Max Power = 65 W */
#define PD_MAX_POWER_MW         ((PD_MAX_VOLTAGE_MV * PD_MAX_CURRENT_MA) / 1000)

/* USB Type A Features */

/* BC 1.2 */

/* Volume Button feature */

/* Fan features */
#define CONFIG_FAN_RPM_CUSTOM

/* LED features */
#define CONFIG_LED_COMMON
#define CONFIG_LED_ONOFF_STATES

/* Thermal Config */
#define CONFIG_TEMP_SENSOR_PCT2075

#ifndef __ASSEMBLER__

#include "gpio_signal.h"
#include "registers.h"

/* Motion sensor interrupt */
void motion_interrupt(enum gpio_signal signal);

/* Battery Types */
enum battery_type {
	BATTERY_AP19B8M,
	BATTERY_TYPE_COUNT,
};

enum base_accelgyro_type {
	BASE_GYRO_NONE = 0,
	BASE_GYRO_BMI260 = 1,
};

/* ADC Channels */
enum adc_channel {
	ADC_TEMP_SENSOR_SOC = 0,
	ADC_TEMP_SENSOR_CHARGER,
	ADC_TEMP_SENSOR_MEMORY,
	ADC_CORE_IMON1,
	ADC_SOC_IMON2,
	ADC_CH_COUNT
};

/* Temp Sensors */
enum temp_sensor_id {
	TEMP_SENSOR_SOC = 0,
	TEMP_SENSOR_CHARGER,
	TEMP_SENSOR_MEMORY,
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
