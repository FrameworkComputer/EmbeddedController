/* Copyright 2018 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Arcada ISH board configuration */

#ifndef __CROS_EC_BOARD_H
#define __CROS_EC_BOARD_H

/*
 * By default, enable all console messages except HC, ACPI and event
 * The sensor stack is generating a lot of activity.
 */
#undef CONFIG_HOSTCMD_DEBUG_MODE
#define CONFIG_HOSTCMD_DEBUG_MODE HCDEBUG_OFF

/* ISH specific*/
#undef CONFIG_DEBUG_ASSERT
#define CONFIG_CLOCK_CRYSTAL
/* EC */
#define CONFIG_FLASH_SIZE_BYTES 0x80000
#define CONFIG_FPU
#define CONFIG_I2C
#define CONFIG_I2C_CONTROLLER

#define CONFIG_ACCEL_LNG2DM /* Base sensor: LNG2DM  \
			     * (uses LIS2DH driver) \
			     */
#define CONFIG_ACCELGYRO_LSM6DSM /* Lid sensor: LSM6DS3   \
				  * (uses LSM6DSM driver) \
				  */
#define CONFIG_MAG_LIS2MDL /* Lid sensor: LIS2DML */
#define CONFIG_MAG_CALIBRATE

/* Enable sensor fifo, must also define the _SIZE and _THRES */
#define CONFIG_ACCEL_FIFO
/* FIFO size is a power of 2. */
#define CONFIG_ACCEL_FIFO_SIZE 256
/* Depends on how fast the AP boots and typical ODRs. */
#define CONFIG_ACCEL_FIFO_THRES (CONFIG_ACCEL_FIFO_SIZE / 3)
/* Sensors without hardware FIFO are in forced mode */
#define CONFIG_ACCEL_FORCE_MODE_MASK (BIT(BASE_ACCEL) | BIT(LID_MAG))

#define CONFIG_ACCEL_LSM6DSM_INT_EVENT \
	TASK_EVENT_MOTION_SENSOR_INTERRUPT(LID_ACCEL)

#define CONFIG_MKBP_EVENT
#define CONFIG_MKBP_USE_HECI

#define CONFIG_LID_ANGLE
#define CONFIG_LID_ANGLE_SENSOR_BASE BASE_ACCEL
#define CONFIG_LID_ANGLE_SENSOR_LID LID_ACCEL

#define CONFIG_TABLET_MODE
#define CONFIG_GMR_TABLET_MODE
#define CONFIG_GMR_TABLET_MODE_CUSTOM

/* DMA paging between SRAM and DRAM */
#define CONFIG_DMA_PAGING

/* Host command over HECI */
#define CONFIG_HOST_INTERFACE_HECI

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
#undef CONFIG_FLASH_CROS
#undef CONFIG_FMAP
#undef CONFIG_SWITCH
#undef CONFIG_CONSOLE_VERBOSE

/* Modules we want to exclude */
#undef CONFIG_CMD_HASH
#undef CONFIG_CMD_TEMP_SENSOR
#undef CONFIG_ADC
#undef CONFIG_SHA256_SW

/* power management definitions */
#define CONFIG_LOW_POWER_IDLE

#define CONFIG_ISH_PM_D0I1
#define CONFIG_ISH_PM_D0I2
#define CONFIG_ISH_PM_D0I3
#define CONFIG_ISH_PM_D3
#define CONFIG_ISH_PM_RESET_PREP

#define CONFIG_ISH_D0I2_MIN_USEC (15 * MSEC) /* need final tune */
#define CONFIG_ISH_D0I3_MIN_USEC (100 * MSEC) /* need final tune */

#ifndef __ASSEMBLER__

#include "gpio_signal.h"
#include "registers.h"

/* Motion sensors
 *
 * Note: Since we aren't using LPC memory map to transmit sensor data, the
 * order of this enum does not need to be accel, accel, gyro
 */
enum sensor_id { LID_ACCEL, LID_GYRO, BASE_ACCEL, LID_MAG, SENSOR_COUNT };

#endif /* !__ASSEMBLER__ */

#endif /* __CROS_EC_BOARD_H */
