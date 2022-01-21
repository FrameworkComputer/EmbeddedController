/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
/* Cherry board configuration */

#ifndef __CROS_EC_BOARD_H
#define __CROS_EC_BOARD_H

#include "baseboard.h"

/* Optional features */
#define CONFIG_LTO
#define CONFIG_PRESERVE_LOGS

/*
 * TODO: Remove this option once the VBAT no longer keeps high when
 * system's power isn't presented.
 */
#define CONFIG_IT83XX_RESET_PD_CONTRACT_IN_BRAM

/* BC12 */

/* LED */
#define CONFIG_LED_ONOFF_STATES
#define CONFIG_LED_ONOFF_STATES_BAT_LOW 10

/* PD / USB-C / PPC */
#undef CONFIG_USB_PD_DEBUG_LEVEL /* default to 1, configurable in ec console */

/* Optional console commands */
#define CONFIG_CMD_FLASH
#define CONFIG_CMD_SCRATCHPAD
#define CONFIG_CMD_STACKOVERFLOW

#define CONFIG_BATT_FULL_CHIPSET_OFF_INPUT_LIMIT_MV	9000

/* Keyboard */
#define GPIO_EN_KEYBOARD_BACKLIGHT	GPIO_EN_KB_BL
#define CONFIG_KEYBOARD_REFRESH_ROW3

/* Sensor */
#define CONFIG_GMR_TABLET_MODE
#define CONFIG_TABLET_MODE
#define CONFIG_TABLET_MODE_SWITCH

/* ICM426XX Base accel/gyro */
#define CONFIG_ACCELGYRO_ICM42607
#define CONFIG_ACCELGYRO_ICM42607_INT_EVENT \
	TASK_EVENT_MOTION_SENSOR_INTERRUPT(BASE_ACCEL)

/* KX022 Lid accel */
#define CONFIG_ACCEL_KX022

/* BMA422 Lid accel */
#define CONFIG_ACCEL_BMA4XX

#define CONFIG_ACCEL_FORCE_MODE_MASK BIT(LID_ACCEL)

#define CONFIG_LID_ANGLE
#define CONFIG_LID_ANGLE_SENSOR_BASE BASE_ACCEL
#define CONFIG_LID_ANGLE_SENSOR_LID LID_ACCEL
#define CONFIG_LID_ANGLE_UPDATE

/* SPI / Host Command */
#undef CONFIG_HOSTCMD_DEBUG_MODE
#define CONFIG_HOSTCMD_DEBUG_MODE HCDEBUG_OFF

/* USB-A */
#define USBA_PORT_COUNT 1

/* Temperature */
#define CONFIG_TEMP_SENSOR
#define CONFIG_THERMISTOR
#define CONFIG_STEINHART_HART_3V3_30K9_47K_4050B

#ifndef __ASSEMBLER__

#include "gpio_signal.h"
#include "registers.h"

enum battery_type {
	BATTERY_PANASONIC_AC16L5J,
	BATTERY_PANASONIC_AC16L5J_KT00205009,
	BATTERY_AP16L8J,
	BATTERY_LGC_AP18C8K,
	BATTERY_TYPE_COUNT,
};

enum sensor_id {
	BASE_ACCEL = 0,
	BASE_GYRO,
	LID_ACCEL,
	SENSOR_COUNT,
};

int board_accel_force_mode_mask(void);

#endif /* !__ASSEMBLER__ */
#endif /* __CROS_EC_BOARD_H */
