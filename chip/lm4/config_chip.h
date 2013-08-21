/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_CONFIG_CHIP_H
#define __CROS_EC_CONFIG_CHIP_H

/* 16.000 Mhz internal oscillator frequency (PIOSC) */
#define INTERNAL_CLOCK 16000000

/* Number of IRQ vectors on the NVIC */
#define CONFIG_IRQ_COUNT 132

/* Use a bigger console output buffer */
#undef CONFIG_UART_TX_BUF_SIZE
#define CONFIG_UART_TX_BUF_SIZE 8192

/* Interval between HOOK_TICK notifications */
#define HOOK_TICK_INTERVAL (250 * MSEC)

/* Maximum number of deferrable functions */
#define DEFERRABLE_MAX_COUNT 8

/* Number of I2C ports */
#define I2C_PORT_COUNT 6

/****************************************************************************/
/* Memory mapping */

#define CONFIG_RAM_BASE             0x20000000
#define CONFIG_RAM_SIZE             0x00008000

/* System stack size */
#define CONFIG_STACK_SIZE           4096

/* non-standard task stack sizes */
#define IDLE_TASK_STACK_SIZE        384
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
#define CONFIG_FLASH_PHYSICAL_SIZE  0x00040000

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
#define CONFIG_ADC
#define CONFIG_FPU
#define CONFIG_I2C
#define CONFIG_LPC
#define CONFIG_PECI
#define CONFIG_SWITCH

/* Compile for running from RAM instead of flash */
/* #define COMPILE_FOR_RAM */

#endif  /* __CROS_EC_CONFIG_CHIP_H */
