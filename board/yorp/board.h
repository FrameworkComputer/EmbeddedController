/* Copyright 2018 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Yorp board configuration */

#ifndef __CROS_EC_BOARD_H
#define __CROS_EC_BOARD_H

/* Select Baseboard features */
#define VARIANT_OCTOPUS_EC_NPCX796FB
#define VARIANT_OCTOPUS_CHARGER_ISL9238
#include "baseboard.h"

/* Optional features */
#define CONFIG_SYSTEM_UNLOCKED /* Allow dangerous commands while in dev. */

/* EC console commands  */
#define CONFIG_CMD_ACCELS
#define CONFIG_CMD_ACCEL_INFO

#define CONFIG_LED_COMMON

/* Sensors */
#define CONFIG_ACCEL_KX022		/* Lid accel */
#define CONFIG_ACCELGYRO_LSM6DSM	/* Base accel */
/* Sensors without hardware FIFO are in forced mode */
#define CONFIG_ACCEL_FORCE_MODE_MASK (1 << LID_ACCEL)

#define CONFIG_LID_ANGLE
#define CONFIG_LID_ANGLE_UPDATE
#define CONFIG_LID_ANGLE_SENSOR_BASE BASE_ACCEL
#define CONFIG_LID_ANGLE_SENSOR_LID LID_ACCEL

#define CONFIG_TABLET_MODE
#define CONFIG_TABLET_SWITCH
#define TABLET_MODE_GPIO_L GPIO_TABLET_MODE_L

#define CONFIG_TEMP_SENSOR
#define CONFIG_THERMISTOR_NCP15WB

#define CONFIG_ACCEL_INTERRUPTS
/* FIFO size is in power of 2. */
#define CONFIG_ACCEL_FIFO 1024

/* Depends on how fast the AP boots and typical ODRs */
#define CONFIG_ACCEL_FIFO_THRES (CONFIG_ACCEL_FIFO / 3)
#define CONFIG_MKBP_EVENT
#define CONFIG_MKBP_USE_HOST_EVENT

#define CONFIG_ACCEL_LSM6DSM_INT_EVENT TASK_EVENT_CUSTOM(4)
#ifndef __ASSEMBLER__

#include "gpio_signal.h"
#include "registers.h"

/* GPIO signals updated base on board version. */
#define GPIO_SYS_RESET_L gpio_sys_reset_l_runtime
extern enum gpio_signal gpio_sys_reset_l_runtime;

#define GPIO_ENTERING_RW gpio_entering_rw_runtime
extern enum gpio_signal gpio_entering_rw_runtime;

#define GPIO_USB2_OTG_ID gpio_usb2_otg_id_runtime
extern enum gpio_signal gpio_usb2_otg_id_runtime;

enum adc_channel {
	ADC_TEMP_SENSOR_AMB,		/* ADC0 */
	ADC_TEMP_SENSOR_CHARGER,	/* ADC1 */
	ADC_CH_COUNT
};

enum temp_sensor_id {
	TEMP_SENSOR_BATTERY,
	TEMP_SENSOR_AMBIENT,
	TEMP_SENSOR_CHARGER,
	TEMP_SENSOR_COUNT
};

enum pwm_channel {
	PWM_CH_KBLIGHT,
	PWM_CH_COUNT
};

/* Motion sensors */
enum sensor_id {
	LID_ACCEL,
	BASE_ACCEL,
	BASE_GYRO,
	SENSOR_COUNT
};

/* List of possible batteries */
enum battery_type {
	BATTERY_LGC15,
	BATTERY_PANASONIC,
	BATTERY_SANYO,
	BATTERY_SONY,
	BATTERY_TYPE_COUNT,
};

#endif /* !__ASSEMBLER__ */

#endif /* __CROS_EC_BOARD_H */
