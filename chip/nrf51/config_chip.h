/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_CONFIG_CHIP_H
#define __CROS_EC_CONFIG_CHIP_H

#include "core/cortex-m0/config_core.h"

/* System stack size */
#define CONFIG_STACK_SIZE 1024

/* Idle task stack size */
#define IDLE_TASK_STACK_SIZE 256

/* Default task stack size */
#define TASK_STACK_SIZE 488

/* Larger task stack size, for hook task */
#define LARGER_TASK_STACK_SIZE 640

/* Interval between HOOK_TICK notifications */
#define HOOK_TICK_INTERVAL_MS 500
#define HOOK_TICK_INTERVAL    (HOOK_TICK_INTERVAL_MS * MSEC)

/* Maximum number of deferrable functions */
#define DEFERRABLE_MAX_COUNT 8

/* Number of I2C ports */
#define I2C_PORT_COUNT 2

/*
 * --- chip variant settings ---
 */

/* RAM mapping */
#define CONFIG_RAM_BASE         0x20000000
#define CONFIG_RAM_SIZE         0x00004000

/* Flash mapping */
#define CONFIG_FLASH_BASE       0x00000000
#define CONFIG_FLASH_PHYSICAL_SIZE 0x00040000
#define CONFIG_FLASH_SIZE       CONFIG_FLASH_PHYSICAL_SIZE
#define CONFIG_FLASH_BANK_SIZE  0x1000

/* Size of one firmware image in flash */
#define CONFIG_FW_IMAGE_SIZE    (128 * 1024)

/* Define the RO/RW offset */
#define CONFIG_FW_RO_OFF        0
#define CONFIG_FW_RO_SIZE       (CONFIG_FW_IMAGE_SIZE - CONFIG_FW_PSTATE_SIZE)
#define CONFIG_FW_RW_OFF        CONFIG_FW_IMAGE_SIZE
#define CONFIG_FW_RW_SIZE       CONFIG_FW_IMAGE_SIZE

/*
 * Put pstate after RO to give RW more space and make RO write protect
 * region contiguous.
 */
#define CONFIG_FW_PSTATE_SIZE  CONFIG_FLASH_BANK_SIZE
#define CONFIG_FW_PSTATE_OFF   (CONFIG_FW_RO_OFF + CONFIG_FW_RO_SIZE)


/* Number of IRQ vectors on the NVIC */
#define CONFIG_IRQ_COUNT 32

/* Not that much RAM, set to smaller */
#undef CONFIG_UART_TX_BUF_SIZE
#define CONFIG_UART_TX_BUF_SIZE 1024


#endif /* __CROS_EC_CONFIG_CHIP_H */

