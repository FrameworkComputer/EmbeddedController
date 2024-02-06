/* Copyright 2019 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Volteer ISH board configuration */

#ifndef __CROS_EC_BOARD_H
#define __CROS_EC_BOARD_H

/*
 * Allow dangerous commands.
 * TODO: Don't use this on production systems.
 */
#define CONFIG_SYSTEM_UNLOCKED

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

/* BMA253 Lid accel */
#define CONFIG_ACCEL_BMA255

/* Host command over HECI */
#define CONFIG_HOST_INTERFACE_HECI

#define CONFIG_MKBP_EVENT
#define CONFIG_MKBP_USE_HECI

/* Enable sensor fifo, must also define the _SIZE and _THRES */
#define CONFIG_ACCEL_FIFO
/* FIFO size is in power of 2. */
#define CONFIG_ACCEL_FIFO_SIZE 512
/* Depends on how fast the AP boots and typical ODRs */
#define CONFIG_ACCEL_FIFO_THRES (CONFIG_ACCEL_FIFO_SIZE / 3)

/* Sensors without hardware FIFO are in forced mode */
#define CONFIG_ACCEL_FORCE_MODE_MASK (BIT(LID_ACCEL))

/* I2C ports */
#define I2C_PORT_SENSOR ISH_I2C1
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
#undef CONFIG_LID_SWITCH
#undef CONFIG_SWITCH
#undef CONFIG_WATCHDOG

/* Modules we want to exclude */
#undef CONFIG_CMD_HASH
#undef CONFIG_CMD_TEMP_SENSOR
#undef CONFIG_ADC
#undef CONFIG_SHA256_SW

#ifndef __ASSEMBLER__

#include "gpio_signal.h"
#include "registers.h"

/* Motion sensors */
enum sensor_id { LID_ACCEL, SENSOR_COUNT };

#endif /* !__ASSEMBLER__ */

#endif /* __CROS_EC_BOARD_H */
