/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
/* Asurada board configuration */

#ifndef __CROS_EC_BOARD_H
#define __CROS_EC_BOARD_H

#include "baseboard.h"

/* Chipset config */
#define CONFIG_BRINGUP

/* Optional features */
#define CONFIG_SYSTEM_UNLOCKED
#define CONFIG_LTO

/*
 * TODO: Remove this option once the VBAT no longer keeps high when
 * system's power isn't presented.
 */
#define CONFIG_IT83XX_RESET_PD_CONTRACT_IN_BRAM

/* BC12 */
/* TODO(b/159583342): remove after rev0 deprecated */
#define CONFIG_MT6360_BC12_GPIO

/* LED */
#ifdef BOARD_HAYATO
#define CONFIG_LED_POWER_LED
#define CONFIG_LED_ONOFF_STATES
#define CONFIG_LED_ONOFF_STATES_BAT_LOW 10
#endif

/* PD / USB-C / PPC */
#define CONFIG_USB_PD_DEBUG_LEVEL 3

/* Optional console commands */
#define CONFIG_CMD_FLASH
#define CONFIG_CMD_SCRATCHPAD
#define CONFIG_CMD_STACKOVERFLOW

/* Sensor */
#define CONFIG_GMR_TABLET_MODE
#define CONFIG_TABLET_MODE
#define CONFIG_TABLET_MODE_SWITCH
#define GMR_TABLET_MODE_GPIO_L GPIO_TABLET_MODE_L

#define CONFIG_ACCELGYRO_BMI160 /* Base accel */
#define CONFIG_ACCELGYRO_BMI160_INT_EVENT \
	TASK_EVENT_MOTION_SENSOR_INTERRUPT(BASE_ACCEL)

#define CONFIG_ACCEL_LIS2DWL
#define CONFIG_ACCEL_LIS2DW_AS_BASE
#define CONFIG_ACCEL_LIS2DW12_INT_EVENT \
	TASK_EVENT_MOTION_SENSOR_INTERRUPT(LID_ACCEL)

#define CONFIG_LID_ANGLE
#define CONFIG_LID_ANGLE_SENSOR_BASE BASE_ACCEL
#define CONFIG_LID_ANGLE_SENSOR_LID LID_ACCEL
#define CONFIG_LID_ANGLE_UPDATE

#ifdef BOARD_ASURADA_REV0
#define CONFIG_ALS
#define ALS_COUNT 1
#define CONFIG_ALS_TCS3400
#define CONFIG_ALS_TCS3400_INT_EVENT \
	TASK_EVENT_MOTION_SENSOR_INTERRUPT(CLEAR_ALS)
#define CONFIG_ALS_TCS3400_EMULATED_IRQ_EVENT

#define CONFIG_ACCEL_FORCE_MODE_MASK (BIT(LID_ACCEL) | BIT(CLEAR_ALS))
#else
/* TODO(b/171931139): remove this after rev1 board deprecated */
#define CONFIG_ACCEL_FORCE_MODE_MASK (board_accel_force_mode_mask())
#endif

/* SPI / Host Command */
#undef CONFIG_HOSTCMD_DEBUG_MODE
#define CONFIG_HOSTCMD_DEBUG_MODE HCDEBUG_OFF

/* USB-A */
#define USBA_PORT_COUNT 1

#ifndef __ASSEMBLER__

#include "gpio_signal.h"
#include "registers.h"

enum battery_type {
	BATTERY_C235,
	BATTERY_TYPE_COUNT,
};

enum sensor_id {
	BASE_ACCEL = 0,
	BASE_GYRO,
	LID_ACCEL,
#ifdef BOARD_ASURADA_REV0
	CLEAR_ALS,
	RGB_ALS,
#endif
	SENSOR_COUNT,
};

enum pwm_channel {
	PWM_CH_LED1,
	PWM_CH_LED2,
	PWM_CH_LED3,
	PWM_CH_COUNT,
};

int board_accel_force_mode_mask(void);

#endif /* !__ASSEMBLER__ */
#endif /* __CROS_EC_BOARD_H */
