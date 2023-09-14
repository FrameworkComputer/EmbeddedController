/* Copyright 2019 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Garg board configuration */

#ifndef __CROS_EC_BOARD_H
#define __CROS_EC_BOARD_H

/* Free up flash space */
#define CONFIG_LTO
#undef CONFIG_CMD_ACCELSPOOF
#undef CONFIG_CMD_BATTFAKE
#undef CONFIG_CMD_MEM

/* Select Baseboard features */
#define VARIANT_OCTOPUS_EC_NPCX796FB
#define VARIANT_OCTOPUS_CHARGER_ISL9238
#include "baseboard.h"

#define GPIO_PG_EC_RSMRST_ODL GPIO_RSMRST_L_PGOOD

/* I2C bus configuraiton */
#define I2C_PORT_ACCEL I2C_PORT_SENSOR

#define CONFIG_LED_COMMON

/* Sensors */
#define CONFIG_ACCEL_KX022 /* Lid accel */
#define CONFIG_ACCELGYRO_BMI160 /* Base accel */
#define CONFIG_ACCELGYRO_BMI260 /* 3rd Base accel */
#define CONFIG_ACCELGYRO_ICM426XX /* 2nd Base accel */
#define CONFIG_SYNC /* Camera VSYNC */

#define CONFIG_DYNAMIC_MOTION_SENSOR_COUNT
/* Sensors without hardware FIFO are in forced mode */
#define CONFIG_ACCEL_FORCE_MODE_MASK BIT(LID_ACCEL)

/* Motion Sense Task Events */
#define CONFIG_ACCELGYRO_BMI160_INT_EVENT \
	TASK_EVENT_MOTION_SENSOR_INTERRUPT(BASE_ACCEL)
#define CONFIG_ACCELGYRO_BMI260_INT_EVENT \
	TASK_EVENT_MOTION_SENSOR_INTERRUPT(BASE_ACCEL)
#define CONFIG_ACCELGYRO_ICM426XX_INT_EVENT \
	TASK_EVENT_MOTION_SENSOR_INTERRUPT(BASE_ACCEL)

#define CONFIG_SYNC_INT_EVENT TASK_EVENT_MOTION_SENSOR_INTERRUPT(VSYNC)

#define CONFIG_LID_ANGLE
#define CONFIG_LID_ANGLE_UPDATE
#define CONFIG_LID_ANGLE_SENSOR_BASE BASE_ACCEL
#define CONFIG_LID_ANGLE_SENSOR_LID LID_ACCEL

#define CONFIG_VOLUME_BUTTONS
#define GPIO_VOLUME_UP_L GPIO_EC_VOLUP_BTN_ODL
#define GPIO_VOLUME_DOWN_L GPIO_EC_VOLDN_BTN_ODL

#define CONFIG_TEMP_SENSOR
#define CONFIG_THERMISTOR
#define CONFIG_STEINHART_HART_3V3_13K7_47K_4050B
#define CONFIG_STEINHART_HART_3V3_51K1_47K_4050B

/* Free up more flash. */
#undef CONFIG_CMD_ACCELSPOOF
#undef CONFIG_CMD_ADC
#undef CONFIG_CMD_GETTIME
#undef CONFIG_CMD_HCDEBUG
#undef CONFIG_CMD_I2C_SCAN
#undef CONFIG_CMD_I2C_XFER
#undef CONFIG_CONSOLE_VERBOSE
#undef CONFIG_CONSOLE_CMDHELP
#undef CONFIG_CONSOLE_HISTORY
#define CONFIG_USB_PD_DEBUG_LEVEL 0

#ifndef __ASSEMBLER__

/* support factory keyboard test */
#define CONFIG_KEYBOARD_FACTORY_TEST

#include "gpio_signal.h"
#include "registers.h"

enum adc_channel {
	ADC_TEMP_SENSOR_AMB, /* ADC0 */
	ADC_TEMP_SENSOR_CHARGER, /* ADC1 */
	ADC_VBUS_C0, /* ADC9 */
	ADC_VBUS_C1, /* ADC4 */
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
	BATTERY_SIMPLO_SDI,
	BATTERY_SIMPLO_BYD,
	BATTERY_SIMPLO_CA475778G,
	BATTERY_SIMPLO_CA475778G_R,
	BATTERY_TYPE_COUNT,
};

void sensor_interrupt(enum gpio_signal signal);

#endif /* !__ASSEMBLER__ */

#endif /* __CROS_EC_BOARD_H */
