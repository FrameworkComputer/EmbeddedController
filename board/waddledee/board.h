/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Waddledee board configuration */

#ifndef __CROS_EC_BOARD_H
#define __CROS_EC_BOARD_H

/* Select Baseboard features */
#define VARIANT_DEDEDE_EC_IT8320
#include "baseboard.h"

/* System unlocked in early development */
#define CONFIG_SYSTEM_UNLOCKED

/* Battery */
#define CONFIG_BATTERY_FUEL_GAUGE

/* LED */
#define CONFIG_LED_PWM_COUNT 1

/* Sensors */
#define CONFIG_ACCEL_LIS2DE		/* Lid accel */
#define CONFIG_ACCELGYRO_LSM6DSM	/* Base accel */
#define CONFIG_SYNC			/* Camera VSYNC */
/* Sensors without hardware FIFO are in forced mode */
#define CONFIG_ACCEL_FORCE_MODE_MASK BIT(LID_ACCEL)

#define CONFIG_ACCEL_INTERRUPTS
/* Enable sensor fifo, must also define the _SIZE and _THRES */
#define CONFIG_ACCEL_FIFO
/* Power of 2 - Too large of a fifo causes too much timestamp jitter */
#define CONFIG_ACCEL_FIFO_SIZE 256
#define CONFIG_ACCEL_FIFO_THRES (CONFIG_ACCEL_FIFO_SIZE / 3)

#define CONFIG_LID_ANGLE
#define CONFIG_LID_ANGLE_UPDATE
#define CONFIG_LID_ANGLE_SENSOR_BASE BASE_ACCEL
#define CONFIG_LID_ANGLE_SENSOR_LID LID_ACCEL

#define CONFIG_SYNC_INT_EVENT \
	TASK_EVENT_MOTION_SENSOR_INTERRUPT(VSYNC)

#define CONFIG_TABLET_MODE
#define CONFIG_TABLET_MODE_SWITCH
#define CONFIG_GMR_TABLET_MODE

/* USB Mux and Retimer */
#define CONFIG_USB_MUX_IT5205			/* C1: ITE Mux */
#define I2C_PORT_USB_MUX I2C_PORT_USB_C0	/* Required for ITE Mux */

#define CONFIG_USBC_RETIMER_TUSB544		/* C1 Redriver: TUSB544 */


/* Thermistors */
#define CONFIG_TEMP_SENSOR
#define CONFIG_THERMISTOR
#define CONFIG_STEINHART_HART_3V3_51K1_47K_4050B
#define CONFIG_TEMP_SENSOR_POWER_GPIO GPIO_EN_PP3300_A

#ifndef __ASSEMBLER__

#include "gpio_signal.h"
#include "registers.h"

enum pwm_channel {
	PWM_CH_KBLIGHT,
	PWM_CH_LED_RED,
	PWM_CH_LED_GREEN,
	PWM_CH_LED_BLUE,
	PWM_CH_COUNT,
};

/* Motion sensors */
enum sensor_id {
	LID_ACCEL,
	BASE_ACCEL,
	BASE_GYRO,
	VSYNC,
	SENSOR_COUNT
};

enum temp_sensor_id {
	TEMP_SENSOR_1,
	TEMP_SENSOR_2,
	TEMP_SENSOR_COUNT
};

/* List of possible batteries */
enum battery_type {
	/* TODO(b/146557556): get battery spec(s) */
	BATTERY_TYPE_COUNT,
};

int board_is_sourcing_vbus(int port);

#endif /* !__ASSEMBLER__ */

#endif /* __CROS_EC_BOARD_H */
