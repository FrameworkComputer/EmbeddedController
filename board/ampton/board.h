/* Copyright 2018 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Ampton/Apel board configuration */

#ifndef __CROS_EC_BOARD_H
#define __CROS_EC_BOARD_H

/* Select Baseboard features */
#define VARIANT_OCTOPUS_EC_ITE8320
#define VARIANT_OCTOPUS_CHARGER_ISL9238
#include "baseboard.h"

#define GPIO_PG_EC_RSMRST_ODL GPIO_RSMRST_L_PGOOD

/* I2C bus configuraiton */
#define I2C_PORT_ACCEL I2C_PORT_SENSOR

/* EC console commands  */
#define CONFIG_CMD_ACCELS
#define CONFIG_CMD_ACCEL_INFO

#define CONFIG_VOLUME_BUTTONS
#define GPIO_VOLUME_UP_L GPIO_EC_VOLUP_BTN_ODL
#define GPIO_VOLUME_DOWN_L GPIO_EC_VOLDN_BTN_ODL

#define CONFIG_LED_COMMON

#define CONFIG_BATT_FULL_CHIPSET_OFF_INPUT_LIMIT_MV 5000

/* Sensors */
#define CONFIG_TEMP_SENSOR
#define CONFIG_THERMISTOR
#define CONFIG_STEINHART_HART_3V3_51K1_47K_4050B
#define CONFIG_STEINHART_HART_3V3_13K7_47K_4050B
#define CONFIG_TEMP_SENSOR_POWER
#define GPIO_TEMP_SENSOR_POWER GPIO_EN_PP3300

#define CONFIG_ACCEL_BMA255 /* Lid accel */
#define CONFIG_ACCEL_KX022 /* Lid accel */
#define CONFIG_ACCELGYRO_BMI160 /* Base accel */
#define CONFIG_ACCELGYRO_ICM42607 /* Base accel */
#define CONFIG_SYNC /* Camera VSYNC */

#define CONFIG_DYNAMIC_MOTION_SENSOR_COUNT
/* Sensors without hardware FIFO are in forced mode */
#define CONFIG_ACCEL_FORCE_MODE_MASK BIT(LID_ACCEL)

#define CONFIG_LID_ANGLE
#define CONFIG_LID_ANGLE_UPDATE
#define CONFIG_LID_ANGLE_SENSOR_BASE BASE_ACCEL
#define CONFIG_LID_ANGLE_SENSOR_LID LID_ACCEL

#define CONFIG_ACCELGYRO_BMI160_INT_EVENT \
	TASK_EVENT_MOTION_SENSOR_INTERRUPT(BASE_ACCEL)
#define CONFIG_ACCELGYRO_ICM42607_INT_EVENT \
	TASK_EVENT_MOTION_SENSOR_INTERRUPT(BASE_ACCEL)
#define CONFIG_SYNC_INT_EVENT TASK_EVENT_MOTION_SENSOR_INTERRUPT(VSYNC)

/* Keyboard backlight is unimplemented in hardware */
#undef CONFIG_PWM
#undef CONFIG_PWM_KBLIGHT

#undef CONFIG_UART_TX_BUF_SIZE
#define CONFIG_UART_TX_BUF_SIZE 4096

#define CONFIG_USB_MUX_RUNTIME_CONFIG

#ifndef __ASSEMBLER__

#include "gpio_signal.h"
#include "registers.h"

enum adc_channel {
	ADC_VBUS_C0,
	ADC_VBUS_C1,
	ADC_TEMP_SENSOR_AMB,
	ADC_TEMP_SENSOR_CHARGER,
	ADC_CH_COUNT
};

enum temp_sensor_id {
	TEMP_SENSOR_BATTERY,
	TEMP_SENSOR_AMBIENT,
	TEMP_SENSOR_CHARGER,
	TEMP_SENSOR_COUNT
};

/* Motion sensors */
enum sensor_id { LID_ACCEL, BASE_ACCEL, BASE_GYRO, VSYNC, SENSOR_COUNT };

/* List of possible batteries */
enum battery_type {
	BATTERY_C214,
	BATTERY_C204EE,
	BATTERY_C424,
	BATTERY_C204_SECOND,
	BATTERY_TYPE_COUNT,
};

void motion_interrupt(enum gpio_signal signal);

#endif /* !__ASSEMBLER__ */

#endif /* __CROS_EC_BOARD_H */
