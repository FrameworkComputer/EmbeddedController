/* Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_CONFIG_CHIP_H
#define __CROS_EC_CONFIG_CHIP_H

/* CPU core BFD configuration */
#include "core/cortex-m/config_core.h"

/* Interval between HOOK_TICK notifications */
#define HOOK_TICK_INTERVAL_MS	250
#define HOOK_TICK_INTERVAL	(HOOK_TICK_INTERVAL_MS * MSEC)

/****************************************************************************/
/* Memory mapping */

/*
 * The memory region for RAM is 0x20000000-0x20060000. (384 KB)
 * - Only 1 image is loaded directly into SRAM.
 * - The lower 128KB is reserved for the image.
 * - The next 128KB is reserved for RAM.
 * - The last 128KB is reserved.
 */

/****************************************************************************/
#define ROTOR_MCU_SRAM_BASE		0x20000000
#define ROTOR_MCU_SRAM_END		(ROTOR_MCU_SRAM_BASE + (3 * 0x20000))
#define CONFIG_RAM_BASE			(ROTOR_MCU_SRAM_BASE + CONFIG_RW_SIZE)
#define CONFIG_RAM_SIZE			(1 * 0x20000)

/* Default task stack size. */
#define TASK_STACK_SIZE			512

/* System stack size */
#define CONFIG_STACK_SIZE		1024

/* non-standard task stack sizes */
#define IDLE_TASK_STACK_SIZE		512
#define LARGER_TASK_STACK_SIZE		768

#define CONFIG_PROGRAM_MEMORY_BASE	ROTOR_MCU_SRAM_BASE
#define CONFIG_MAPPED_STORAGE_BASE	CONFIG_PROGRAM_MEMORY_BASE

/* There's only 1 image. */
#undef CONFIG_FW_INCLUDE_RO
#define CONFIG_RO_MEM_OFF		0
#define CONFIG_RO_SIZE			0

#define CONFIG_RW_MEM_OFF		0
#define CONFIG_RW_SIZE			(1 * 0x20000)

/* There's no concept of protected storage for the MCU. */
#define CONFIG_EC_PROTECTED_STORAGE_OFF		0
#define CONFIG_EC_PROTECTED_STORAGE_SIZE	0
#define CONFIG_EC_WRITABLE_STORAGE_OFF		0
#define CONFIG_EC_WRITABLE_STORAGE_SIZE		0
#define CONFIG_RO_STORAGE_OFF			0
#define CONFIG_RW_STORAGE_OFF			0

#define CONFIG_FLASH_SIZE CONFIG_RW_SIZE
#define CONFIG_FLASH_BANK_SIZE 0

/* Number of IRQ vectors on the NVIC */
#define CONFIG_IRQ_COUNT 96

#define GPIO_PIN(port, index) GPIO_##port, (1 << index)
#define GPIO_PIN_MASK(port, mask) GPIO_##port, (mask)

#define I2C_PORT_COUNT 6

#endif  /* __CROS_EC_CONFIG_CHIP_H */
