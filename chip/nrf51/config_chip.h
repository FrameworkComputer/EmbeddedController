/* Copyright 2014 The Chromium OS Authors. All rights reserved.
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
#define TASK_STACK_SIZE 512

/* Larger task stack size, for hook task */
#define LARGER_TASK_STACK_SIZE 640

/* Interval between HOOK_TICK notifications */
#define HOOK_TICK_INTERVAL_MS 500
#define HOOK_TICK_INTERVAL    (HOOK_TICK_INTERVAL_MS * MSEC)

/* Number of I2C ports */
#define I2C_PORT_COUNT 2

/*
 * --- chip variant settings ---
 */

/* RAM mapping */
#define CONFIG_RAM_BASE         0x20000000
#define CONFIG_RAM_SIZE         0x00004000

/* Flash mapping */
#define CONFIG_PROGRAM_MEMORY_BASE 0x00000000
#define CONFIG_FLASH_SIZE 0x00040000
#define CONFIG_FLASH_BANK_SIZE  0x1000

/* Memory-mapped internal flash */
#define CONFIG_INTERNAL_STORAGE
#define CONFIG_MAPPED_STORAGE

/* Program is run directly from storage */
#define CONFIG_MAPPED_STORAGE_BASE CONFIG_PROGRAM_MEMORY_BASE

/* Compute the rest of the flash params from these */
#include "config_std_internal_flash.h"

/* Number of IRQ vectors on the NVIC */
#define CONFIG_IRQ_COUNT 32

/* Not that much RAM, set to smaller */
#undef  CONFIG_UART_TX_BUF_SIZE
#define CONFIG_UART_TX_BUF_SIZE 1024

#define GPIO_PIN(port, index) GPIO_##port, BIT(index)
#define GPIO_PIN_MASK(p, m) .port = GPIO_##p, .mask = (m)

#endif /* __CROS_EC_CONFIG_CHIP_H */

