/* Copyright 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Chip config header file */

#ifndef __CROS_EC_CONFIG_CHIP_H
#define __CROS_EC_CONFIG_CHIP_H

/* Memory mapping */
#if !defined(TEST_NVMEM) && !defined(TEST_CR50_FUZZ)
#define CONFIG_FLASH_SIZE 0x00020000
#define CONFIG_FLASH_BANK_SIZE 0x1000
#else
#define CONFIG_FLASH_SIZE (512 * 1024)
#define CONFIG_FLASH_BANK_SIZE 0x800
#endif

extern char __host_flash[CONFIG_FLASH_SIZE];

#define CONFIG_PROGRAM_MEMORY_BASE ((uintptr_t)__host_flash)
#define CONFIG_FLASH_ERASE_SIZE 0x0010	     /* erase bank size */
#define CONFIG_FLASH_WRITE_SIZE 0x0002	     /* minimum write size */
#define CONFIG_FLASH_WRITE_IDEAL_SIZE 0x0080 /* ideal write size */
#define CONFIG_RAM_BASE 0x0		     /* Not supported */
#define CONFIG_RAM_SIZE                0x0 /* Not supported */

#define CONFIG_FPU

/* Memory-mapped internal flash */
#define CONFIG_INTERNAL_STORAGE
#define CONFIG_MAPPED_STORAGE

/* Program is run directly from storage */
#define CONFIG_MAPPED_STORAGE_BASE CONFIG_PROGRAM_MEMORY_BASE

/* Compute the rest of the flash params from these */
#include "config_std_internal_flash.h"

/* Default task stack size */
#define TASK_STACK_SIZE 512

/* Interval between HOOK_TICK notifications */
#define HOOK_TICK_INTERVAL_MS 250
#define HOOK_TICK_INTERVAL    (HOOK_TICK_INTERVAL_MS * MSEC)

/* Do NOT use common panic code (designed to output information on the UART) */
#undef CONFIG_COMMON_PANIC_OUTPUT
/* Do NOT use common timer code which is designed for hardware counters. */
#undef CONFIG_COMMON_TIMER

#define GPIO_PIN(port, index) GPIO_##port, BIT(index)
#define GPIO_PIN_MASK(p, m) .port = GPIO_##p, .mask = (m)

#define I2C_PORT_COUNT 1

#endif /* __CROS_EC_CONFIG_CHIP_H */
