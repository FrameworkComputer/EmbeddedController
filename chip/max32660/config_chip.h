/* Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_CONFIG_CHIP_H
#define __CROS_EC_CONFIG_CHIP_H

/* CPU core BFD configuration */
#include "core/cortex-m/config_core.h"

/* 96.000 MHz internal oscillator frequency */
#define INTERNAL_CLOCK 96000000

/* Number of IRQ vectors on the NVIC */
#define CONFIG_IRQ_COUNT 132

/* Use a bigger console output buffer */
#undef CONFIG_UART_TX_BUF_SIZE
#define CONFIG_UART_TX_BUF_SIZE 8192

/* Interval between HOOK_TICK notifications */
#define HOOK_TICK_INTERVAL_MS 250
#define HOOK_TICK_INTERVAL (HOOK_TICK_INTERVAL_MS * MSEC)

/* Number of I2C ports */
#define I2C_PORT_COUNT 2

/*
 * Time it takes to set the RTC match register. This value is conservatively
 * set based on measurements around 200us.
 */
#define HIB_SET_RTC_MATCH_DELAY_USEC 300

/****************************************************************************/
/* Memory mapping */

#define CONFIG_RAM_BASE 0x20000000
#define CONFIG_RAM_SIZE 0x00018000 /* 96k MAX32660 SRAM Size*/

/* System stack size */
#define CONFIG_STACK_SIZE 4096

/* non-standard task stack sizes */
#define IDLE_TASK_STACK_SIZE 512
#define LARGER_TASK_STACK_SIZE 768
#define SMALLER_TASK_STACK_SIZE 384

/* Default task stack size */
#define TASK_STACK_SIZE 512

#define CONFIG_PROGRAM_MEMORY_BASE 0x00000000
#define CONFIG_FLASH_BANK_SIZE 0x00002000  /* protect bank size */
#define CONFIG_FLASH_ERASE_SIZE 0x00002000 /* erase bank size */
#define CONFIG_FLASH_WRITE_SIZE 0x00000004 /* minimum write size */

/* Ideal flash write size fills the 32-entry flash write buffer */
#define CONFIG_FLASH_WRITE_IDEAL_SIZE (32 * 4)

/* This is the physical size of the flash on the chip. We'll reserve one bank
 * in order to emulate per-bank write-protection UNTIL REBOOT. The hardware
 * doesn't support a write-protect pin, and if we make the write-protection
 * permanent, it can't be undone easily enough to support RMA. */
#define CONFIG_FLASH_SIZE 0x00040000 /* 256K MAX32660 FLASH Size */

/****************************************************************************/
/* Define our flash layout. */

/* Memory-mapped internal flash */
#define CONFIG_INTERNAL_STORAGE
#define CONFIG_MAPPED_STORAGE

/* Program is run directly from storage */
#define CONFIG_MAPPED_STORAGE_BASE CONFIG_PROGRAM_MEMORY_BASE

/* Compute the rest of the flash params from these */
#include "config_std_internal_flash.h"

/****************************************************************************/
/* Lock the boot configuration to prevent brickage. */

/*
 * No GPIO trigger for ROM bootloader.
 * Keep JTAG debugging enabled.
 * Use 0xA442 flash write key.
 * Lock it this way.
 */
#define CONFIG_BOOTCFG_VALUE 0x7ffffffe

/****************************************************************************/
/* Customize the build */

/* Optional features present on this chip */
#define CONFIG_HOSTCMD_ALIGNED
#define CONFIG_RTC
#define CONFIG_SWITCH

/* Chip needs to do custom pre-init */
#define CONFIG_CHIP_PRE_INIT

#define GPIO_PIN(port, index) GPIO_##port, (1 << index)
#define GPIO_PIN_MASK(p, m) .port = GPIO_##p, .mask = (m)

#endif /* __CROS_EC_CONFIG_CHIP_H */
