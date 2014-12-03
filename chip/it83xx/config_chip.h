/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_CONFIG_CHIP_H
#define __CROS_EC_CONFIG_CHIP_H

/* CPU core BFD configuration */
#include "core/nds32/config_core.h"

/* Number of IRQ vectors on the IVIC */
#define CONFIG_IRQ_COUNT 16

/* Interval between HOOK_TICK notifications */
#define HOOK_TICK_INTERVAL_MS 500
#define HOOK_TICK_INTERVAL    (HOOK_TICK_INTERVAL_MS * MSEC)

/* Maximum number of deferrable functions */
#define DEFERRABLE_MAX_COUNT 8

/* Default PLL frequency. */
#define PLL_CLOCK 48000000

/****************************************************************************/
/* Memory mapping */

#define CONFIG_RAM_BASE             0x00080000
#define CONFIG_RAM_SIZE             0x00002000

/* System stack size */
#define CONFIG_STACK_SIZE           1024

/* non-standard task stack sizes */
#define IDLE_TASK_STACK_SIZE        512
#define LARGER_TASK_STACK_SIZE      640

/* Default task stack size */
#define TASK_STACK_SIZE             512

#define CONFIG_FLASH_BASE           0x00000000
#define CONFIG_FLASH_BANK_SIZE      0x00000800  /* protect bank size */
#define CONFIG_FLASH_ERASE_SIZE     0x00000400  /* erase bank size */
#define CONFIG_FLASH_WRITE_SIZE     0x00000004  /* minimum write size */

/* Ideal flash write size fills the 32-entry flash write buffer */
#define CONFIG_FLASH_WRITE_IDEAL_SIZE (32 * 4)

/* This is the physical size of the flash on the chip. We'll reserve one bank
 * in order to emulate per-bank write-protection UNTIL REBOOT. The hardware
 * doesn't support a write-protect pin, and if we make the write-protection
 * permanent, it can't be undone easily enough to support RMA. */
#define CONFIG_FLASH_PHYSICAL_SIZE  0x00020000

/****************************************************************************/
/* Define our flash layout. */

/* Size of one firmware image in flash */
#ifndef CONFIG_FW_IMAGE_SIZE
#define CONFIG_FW_IMAGE_SIZE		(CONFIG_FLASH_PHYSICAL_SIZE / 2)
#endif

/* RO firmware must start at beginning of flash */
#define CONFIG_FW_RO_OFF		0

/*
 * The EC uses the one bank of flash to emulate a SPI-like write protect
 * register with persistent state.
 */
#define CONFIG_FW_PSTATE_SIZE		CONFIG_FLASH_BANK_SIZE

#ifdef CONFIG_PSTATE_AT_END
/* PSTATE is at end of flash */
#define CONFIG_FW_RO_SIZE		CONFIG_FW_IMAGE_SIZE
#define CONFIG_FW_PSTATE_OFF		(CONFIG_FLASH_PHYSICAL_SIZE	\
					 - CONFIG_FW_PSTATE_SIZE)
/* Don't claim PSTATE is part of flash */
#define CONFIG_FLASH_SIZE		CONFIG_FW_PSTATE_OFF

#else
/* PSTATE immediately follows RO, in the first half of flash */
#define CONFIG_FW_RO_SIZE		(CONFIG_FW_IMAGE_SIZE		\
					 - CONFIG_FW_PSTATE_SIZE)
#define CONFIG_FW_PSTATE_OFF		CONFIG_FW_RO_SIZE
#define CONFIG_FLASH_SIZE		CONFIG_FLASH_PHYSICAL_SIZE
#endif

/* Either way, RW firmware is one firmware image offset from the start */
#define CONFIG_FW_RW_OFF		CONFIG_FW_IMAGE_SIZE
#define CONFIG_FW_RW_SIZE		CONFIG_FW_IMAGE_SIZE

/* TODO: why 2 sets of configs with the same numbers? */
#define CONFIG_FW_WP_RO_OFF		CONFIG_FW_RO_OFF
#define CONFIG_FW_WP_RO_SIZE		CONFIG_FW_RO_SIZE

/****************************************************************************/
/* Customize the build */

/* Use hardware specific udelay() for this chip */
#define CONFIG_HW_SPECIFIC_UDELAY

/* Optional features present on this chip */
#undef CONFIG_I2C
#undef CONFIG_FLASH
#undef CONFIG_WATCHDOG
#define CONFIG_PWM
#define CONFIG_ADC

#endif  /* __CROS_EC_CONFIG_CHIP_H */
