/* Copyright 2019 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* TGL RVP ISH board configuration */

#ifndef __CROS_EC_BOARD_H
#define __CROS_EC_BOARD_H

/*
 * By default, enable all console messages except HC, ACPI and event
 * The sensor stack is generating a lot of activity.
 */
#undef CONFIG_HOSTCMD_DEBUG_MODE
#define CONFIG_HOSTCMD_DEBUG_MODE HCDEBUG_OFF

/* ISH specific */
#undef CONFIG_DEBUG_ASSERT
#define CONFIG_CLOCK_CRYSTAL
#define CONFIG_ISH_UART_0

/* EC */
#define CONFIG_FLASH_SIZE_BYTES 0x80000
#define CONFIG_FPU
#define CONFIG_I2C
#define CONFIG_I2C_CONTROLLER

/* Host command over HECI */
#define CONFIG_HOST_INTERFACE_HECI

#ifdef BOARD_TGLRVP_ISH
#define CONFIG_ACCELGYRO_LSM6DSM /* For LSM6DS3 */
#define CONFIG_ACCEL_FORCE_MODE_MASK BIT(BASE_ACCEL)

/* I2C ports */
#define I2C_PORT_SENSOR ISH_I2C1

/* EC Console Commands */
#define CONFIG_CMD_ACCELS
#define CONFIG_CMD_ACCEL_INFO
#define CONFIG_CMD_TIMERINFO
#define CONFIG_CMD_I2C_XFER
#endif /* BOARD_TGLRVP_ISH */

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
#undef CONFIG_LID_SWITCH
#undef CONFIG_SWITCH
#undef CONFIG_WATCHDOG

/* Modules we want to exclude */
#undef CONFIG_CMD_HASH
#undef CONFIG_CMD_TEMP_SENSOR
#undef CONFIG_ADC
#undef CONFIG_SHA256_SW

/* DMA paging between SRAM and DRAM */
#define CONFIG_DMA_PAGING

/* power management definitions */
#define CONFIG_LOW_POWER_IDLE

#define CONFIG_ISH_PM_D0I1
#define CONFIG_ISH_PM_D0I2
#define CONFIG_ISH_PM_D0I3
#define CONFIG_ISH_PM_D3
#define CONFIG_ISH_PM_RESET_PREP

#define CONFIG_ISH_IPAPG

#define CONFIG_ISH_D0I2_MIN_USEC (15 * MSEC)
#ifdef BOARD_ADL_ISH_LITE
#define CONFIG_ISH_D0I3_MIN_USEC (3000 * MSEC)
#else
#define CONFIG_ISH_D0I3_MIN_USEC (50 * MSEC)
#endif
#define CONFIG_ISH_NEW_PM

#ifndef __ASSEMBLER__

#include "gpio_signal.h"
#include "registers.h"

/* Motion sensors */
enum sensor_id { BASE_ACCEL, SENSOR_COUNT };

#endif /* !__ASSEMBLER__ */

#endif /* __CROS_EC_BOARD_H */
