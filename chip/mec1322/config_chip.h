/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_CONFIG_CHIP_H
#define __CROS_EC_CONFIG_CHIP_H

/* CPU core BFD configuration */
#include "core/cortex-m/config_core.h"

/* Number of IRQ vectors on the NVIC */
#define CONFIG_IRQ_COUNT 93

/* Use a bigger console output buffer */
#undef CONFIG_UART_TX_BUF_SIZE
#define CONFIG_UART_TX_BUF_SIZE 2048

/* Interval between HOOK_TICK notifications */
#define HOOK_TICK_INTERVAL_MS 250
#define HOOK_TICK_INTERVAL    (HOOK_TICK_INTERVAL_MS * MSEC)

/* Maximum number of deferrable functions */
#define DEFERRABLE_MAX_COUNT 8

/* Number of I2C ports */
#define I2C_PORT_COUNT 4

/****************************************************************************/
/* Memory mapping */

/*
 * The memory region for RAM is actually 0x00100000-0x00120000. The lower 96K
 * stores code and the higher 32K stores data. To reflect this, let's say
 * the lower 96K is flash, and the higher 32K is RAM.
 */
#define CONFIG_RAM_BASE             0x00118000
#define CONFIG_RAM_SIZE             0x00008000

/* System stack size */
#define CONFIG_STACK_SIZE           4096

/* non-standard task stack sizes */
#define IDLE_TASK_STACK_SIZE        512
#define LARGER_TASK_STACK_SIZE      640

/* Default task stack size */
#define TASK_STACK_SIZE             512

#define CONFIG_FLASH_BASE           0x00100000

#define CONFIG_FLASH_PHYSICAL_SIZE  0x00018000

/* Size of one firmware image in RAM */

/****************************************************************************/
/* Define our flash layout. */

/* Size of one firmware image in flash */
#ifndef CONFIG_FW_IMAGE_SIZE
#define CONFIG_FW_IMAGE_SIZE		(CONFIG_FLASH_PHYSICAL_SIZE / 2)
#endif

/* RO firmware must start at beginning of flash */
#define CONFIG_FW_RO_OFF		0

#define CONFIG_FW_RO_SIZE		CONFIG_FW_IMAGE_SIZE
#define CONFIG_FLASH_SIZE		CONFIG_FLASH_PHYSICAL_SIZE

/* Either way, RW firmware is one firmware image offset from the start */
#define CONFIG_FW_RW_OFF		CONFIG_FW_IMAGE_SIZE
#define CONFIG_FW_RW_SIZE		CONFIG_FW_IMAGE_SIZE

/* TODO(crosbug.com/p/23796): why 2 sets of configs with the same numbers? */
#define CONFIG_FW_WP_RO_OFF		CONFIG_FW_RO_OFF
#define CONFIG_FW_WP_RO_SIZE		CONFIG_FW_RO_SIZE

#define CONFIG_FLASH_BANK_SIZE          4

/****************************************************************************/
/* Customize the build */
/* Optional features present on this chip */
#if 0
#define CONFIG_ADC
#define CONFIG_PECI
#define CONFIG_SWITCH
#define CONFIG_MPU
#endif
#define CONFIG_I2C
#define CONFIG_LPC
#define CONFIG_FPU
#define CONFIG_SPI
#define CONFIG_DMA

#undef CONFIG_FLASH

#endif  /* __CROS_EC_CONFIG_CHIP_H */
