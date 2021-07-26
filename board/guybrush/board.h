/* Copyright 2020 The Chromium OS Authors. All rights reserved.
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

/* Sensors */
#define CONFIG_ACCELGYRO_BMI160
#define CONFIG_ACCELGYRO_BMI160_INT_EVENT \
	TASK_EVENT_MOTION_SENSOR_INTERRUPT(BASE_ACCEL)
#define CONFIG_ACCELGYRO_BMI3XX
#define CONFIG_ACCELGYRO_BMI3XX_INT_EVENT \
	TASK_EVENT_MOTION_SENSOR_INTERRUPT(BASE_ACCEL)
#define CONFIG_ACCEL_INTERRUPTS
#define CONFIG_ACCEL_BMA4XX

#define I2C_PORT_ACCEL      I2C_PORT_SENSOR

/* EC console commands */
#define CONFIG_CMD_ACCELS
#define CONFIG_CMD_ACCEL_INFO
#define CONFIG_CMD_BUTTON

/* USB Type C and USB PD defines */

/* USB Type A Features */

/* BC 1.2 */

/* Volume Button feature */

/* Fan features */

/* LED features */
#define CONFIG_LED_COMMON
#define CONFIG_LED_ONOFF_STATES

#ifndef __ASSEMBLER__

#include "gpio_signal.h"
#include "registers.h"

/* Motion sensor interrupt */
void motion_interrupt(enum gpio_signal signal);

/* Battery Types */
enum battery_type {
	BATTERY_AEC,
	BATTERY_AP18F4M,
	BATTERY_POWER_TECH,
	BATTERY_TYPE_COUNT,
};

enum base_accelgyro_type {
	BASE_GYRO_NONE = 0,
	BASE_GYRO_BMI160 = 1,
	BASE_GYRO_BMI323 = 2,
};

#endif /* !__ASSEMBLER__ */

#endif /* __CROS_EC_BOARD_H */
