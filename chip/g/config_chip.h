/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_CONFIG_CHIP_H
#define __CROS_EC_CONFIG_CHIP_H

#if defined(BOARD)
#include "core/cortex-m/config_core.h"
#include "hw_regdefs.h"
#endif

/* Describe the RAM layout */
#define CONFIG_RAM_BASE         0x10000
#define CONFIG_RAM_SIZE         0x10000

/* Flash chip specifics */
#define CONFIG_FLASH_BANK_SIZE         0x800	/* protect bank size */
#define CONFIG_FLASH_ERASE_SIZE        0x800	/* erase bank size */
/* This flash can only be written as 4-byte words (aligned properly, too). */
#define CONFIG_FLASH_WRITE_SIZE        4	/* min write size (bytes) */
/* But we have a 32-word buffer for writing multiple adjacent cells */
#define CONFIG_FLASH_WRITE_IDEAL_SIZE  128	/* best write size (bytes) */

/* Describe the flash layout */
#define CONFIG_PROGRAM_MEMORY_BASE     0x40000
#define CONFIG_FLASH_SIZE              (512 * 1024)
#define CONFIG_RO_HEAD_ROOM	       1024	/* Room for ROM signature. */
#define CONFIG_RW_HEAD_ROOM	       CONFIG_RO_HEAD_ROOM  /* same for RW */

/* Memory-mapped internal flash */
#define CONFIG_INTERNAL_STORAGE
#define CONFIG_MAPPED_STORAGE

/* Program is run directly from storage */
#define CONFIG_MAPPED_STORAGE_BASE CONFIG_PROGRAM_MEMORY_BASE

#if defined(BOARD)
/* Compute the rest of the flash params from these */
#include "config_std_internal_flash.h"
#endif

/* Interval between HOOK_TICK notifications */
#define HOOK_TICK_INTERVAL_MS 500
#define HOOK_TICK_INTERVAL    (HOOK_TICK_INTERVAL_MS * MSEC)

/* System stack size */
#define CONFIG_STACK_SIZE 1024

/* Idle task stack size */
#define IDLE_TASK_STACK_SIZE 256

/* Default task stack size */
#define TASK_STACK_SIZE 488

/* Larger task stack size, for hook task */
#define LARGER_TASK_STACK_SIZE 640

/* Magic for gpio.inc */
#define GPIO_PIN(port, index) (port), (1 << (index))
#define GPIO_PIN_MASK(port, mask) (port), (mask)
#define DUMMY_GPIO_BANK 0

#define PCLK_FREQ  (24 * 1000 * 1000)

/* Number of IRQ vectors on the NVIC */
#define CONFIG_IRQ_COUNT (GC_INTERRUPTS_COUNT - 16)

#undef CONFIG_RW_MEM_OFF
#undef CONFIG_RW_SIZE

/* Leaving 16K for the RO aka loader. */
#define CONFIG_RW_MEM_OFF 0x4000
#define CONFIG_RW_B_MEM_OFF (CONFIG_RW_MEM_OFF + (CONFIG_FLASH_SIZE>>1))
#define CONFIG_RW_SIZE ((CONFIG_FLASH_SIZE>>1) - CONFIG_RW_MEM_OFF)
#define CONFIG_CUSTOMIZED_RO
#define CONFIG_EXTENSION_COMMAND 0xbaccd00a

#undef CONFIG_RO_SIZE
#define CONFIG_RO_SIZE CONFIG_RW_MEM_OFF

#endif /* __CROS_EC_CONFIG_CHIP_H */
