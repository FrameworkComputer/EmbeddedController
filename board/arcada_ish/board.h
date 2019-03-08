/* Copyright 2018 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Arcada ISH board configuration */

#ifndef __CROS_EC_BOARD_H
#define __CROS_EC_BOARD_H

/*
 * Allow dangerous commands.
 * TODO: Remove this config before production.
 */
#define CONFIG_SYSTEM_UNLOCKED

/*
 * By default, enable all console messages except HC, ACPI and event
 * The sensor stack is generating a lot of activity.
 */
#undef CONFIG_HOSTCMD_DEBUG_MODE
#define CONFIG_HOSTCMD_DEBUG_MODE HCDEBUG_OFF

/* ISH specific*/
#undef  CONFIG_DEBUG_ASSERT
#define CONFIG_CLOCK_CRYSTAL
#define CONFIG_ISH_UART_0
/* EC */
#define CONFIG_FLASH_SIZE 0x80000
#define CONFIG_FPU
#define CONFIG_I2C
#define CONFIG_I2C_MASTER

#define CONFIG_ACCEL_LNG2DM	 /* Base sensor: LNG2DM (uses LIS2DH driver) */
#define CONFIG_ACCELGYRO_LSM6DSM /* Lid sensor: LSM6DS3 (uses LSM6DSM driver) */

#define CONFIG_ACCEL_INTERRUPTS
#define CONFIG_ACCEL_FIFO 256
#define CONFIG_ACCEL_FIFO_THRES (CONFIG_ACCEL_FIFO / 3)
/* Sensors without hardware FIFO are in forced mode */
#define CONFIG_ACCEL_FORCE_MODE_MASK  (1 << BASE_ACCEL)

#define CONFIG_ACCEL_LSM6DSM_INT_EVENT TASK_EVENT_CUSTOM(1 << 2)

#define CONFIG_MKBP_EVENT
#define CONFIG_MKBP_USE_HECI

#define CONFIG_LID_ANGLE
#define CONFIG_LID_ANGLE_SENSOR_BASE BASE_ACCEL
#define CONFIG_LID_ANGLE_SENSOR_LID LID_ACCEL

#define CONFIG_TABLET_MODE

/* Host command over HECI */
#define CONFIG_HOSTCMD_HECI

/* I2C ports */
#define I2C_PORT_SENSOR ISH_I2C0
#define CONFIG_CMD_I2C_XFER

/* EC Console Commands */
#define CONFIG_CMD_ACCELS
#define CONFIG_CMD_ACCEL_INFO
#define CONFIG_CMD_TIMERINFO

/* Undefined features */
#undef CONFIG_CMD_HASH
#undef CONFIG_CMD_I2C_SCAN
#undef CONFIG_CMD_KEYBOARD
#undef CONFIG_CMD_POWER_AP
#undef CONFIG_CMD_POWERINDEBUG
#undef CONFIG_CMD_SHMEM
#undef CONFIG_EXTPOWER
#undef CONFIG_KEYBOARD_KSO_BASE
#undef CONFIG_FLASH
#undef CONFIG_FMAP
#undef CONFIG_SWITCH
/* TODO: Watch Dog is supported but temporarily removed. Currently under 
 * development on KBL and will be carried over to WHL.
 */
#undef CONFIG_WATCHDOG

/* Modules we want to exclude */
#undef CONFIG_CMD_HASH
#undef CONFIG_CMD_TEMP_SENSOR
#undef CONFIG_ADC
#undef CONFIG_SHA256

#ifndef __ASSEMBLER__

#include "gpio_signal.h"
#include "registers.h"

/* Motion sensors
 *
 * Note: Since we aren't using LPC memory map to transmit sensor data, the
 * order of this enum does not need to be accel, accel, gyro
 */
enum sensor_id {
	LID_ACCEL,
	LID_GYRO,
	BASE_ACCEL,
	/* TODO(b/122281217): Add remain sensors */
	SENSOR_COUNT
};

#endif /* !__ASSEMBLER__ */

#endif /* __CROS_EC_BOARD_H */
