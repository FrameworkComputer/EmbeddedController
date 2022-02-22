/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
/* Dojo board configuration */

#ifndef __CROS_EC_BOARD_H
#define __CROS_EC_BOARD_H

#include "baseboard.h"

/* Chipset config */
#define CONFIG_BRINGUP

/* Optional features */
#define CONFIG_SYSTEM_UNLOCKED
#define CONFIG_LTO
#define CONFIG_PRESERVE_LOGS

/*
 * TODO: Remove this option once the VBAT no longer keeps high when
 * system's power isn't presented.
 */
#define CONFIG_IT83XX_RESET_PD_CONTRACT_IN_BRAM

/* Battery */
#define CONFIG_BATTERY_V2
#define CONFIG_BATTERY_COUNT 1
#define CONFIG_HOSTCMD_BATTERY_V2
#define CONFIG_BATTERY_VENDOR_PARAM

/* BC12 */

/* PD / USB-C / PPC */
#undef CONFIG_USB_PD_DEBUG_LEVEL /* default to 1, configurable in ec console */

/* Optional console commands */
#define CONFIG_CMD_FLASH
#define CONFIG_CMD_SCRATCHPAD
#define CONFIG_CMD_STACKOVERFLOW

#define CONFIG_BATT_FULL_CHIPSET_OFF_INPUT_LIMIT_MV	9000

/* Keyboard */
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
	BATTERY_DYNAPACK_COS,
	BATTERY_DYNAPACK_ATL,
	BATTERY_SIMPLO_COS,
	BATTERY_SIMPLO_HIGHPOWER,
	BATTERY_COS,
	BATTERY_TYPE_COUNT,
};

enum sensor_id {
	BASE_ACCEL = 0,
	BASE_GYRO,
	LID_ACCEL,
	SENSOR_COUNT,
};

enum pwm_channel {
	PWM_CH_LED_C1_WHITE,
	PWM_CH_LED_C1_AMBER,
	PWM_CH_LED_PWR,
	PWM_CH_KBLIGHT,
	PWM_CH_LED_C0_WHITE,
	PWM_CH_LED_C0_AMBER,
	PWM_CH_COUNT,
};

int board_accel_force_mode_mask(void);

#endif /* !__ASSEMBLER__ */
#endif /* __CROS_EC_BOARD_H */
