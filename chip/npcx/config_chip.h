/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_CONFIG_CHIP_H
#define __CROS_EC_CONFIG_CHIP_H

/* CPU core BFD configuration */
#include "core/cortex-m/config_core.h"
#define CONFIG_PSTATE_AT_END

/* 32k hz internal oscillator frequency (FRCLK) */
#define INT_32K_CLOCK 32768

/* Number of IRQ vectors on the NVIC */
#define CONFIG_IRQ_COUNT 64

/* Use a bigger console output buffer */
#undef CONFIG_UART_TX_BUF_SIZE
#define CONFIG_UART_TX_BUF_SIZE 8192

/* Interval between HOOK_TICK notifications */
#define HOOK_TICK_INTERVAL_MS 250
#define HOOK_TICK_INTERVAL    (HOOK_TICK_INTERVAL_MS * MSEC)

/* Maximum number of deferrable functions */
#define DEFERRABLE_MAX_COUNT 8

/* Number of I2C ports */
#define I2C_PORT_COUNT 4

/* Number of PWM ports */
#define PWM_COUNT 8

/*****************************************************************************/
/* Memory mapping */
#define CONFIG_RAM_BASE         0x200C0000 /* memory map address of data ram */
#define CONFIG_RAM_SIZE         0x00008000 /* 32KB data ram */
#define CONFIG_CDRAM_BASE       0x10088000 /* memory map address of code ram */
#define CONFIG_CDRAM_SIZE       0x00020000 /* 128KB code ram */
#define CONFIG_FLASH_BASE	0x64000000 /* memory address of spi-flash */
#define CONFIG_LPRAM_BASE       0x40001600 /* memory address of low power ram */
#define CONFIG_LPRAM_SIZE	0x00000620 /* 1568B low power ram */

/* System stack size */
#define CONFIG_STACK_SIZE       4096

/* non-standard task stack sizes */
#define IDLE_TASK_STACK_SIZE    512
#define LARGER_TASK_STACK_SIZE  768
#define SMALLER_TASK_STACK_SIZE 384

/* Default task stack size */
#define TASK_STACK_SIZE         512

/* SPI Flash Spec of W25Q20CV */

#define CONFIG_FLASH_BANK_SIZE	0x00001000  /* protect bank size 4K bytes */
#define CONFIG_FLASH_ERASE_SIZE	0x00001000  /* sector erase size 4K bytes */
#define CONFIG_FLASH_WRITE_SIZE	0x00000001  /* minimum write size */

#define CONFIG_FLASH_WRITE_IDEAL_SIZE 256	 /* one page size for write */
#define CONFIG_FLASH_PHYSICAL_SIZE    0x00040000 /* 256KB Flash used for EC */

/****************************************************************************/
/* Define our flash layout. */
/* Size of one firmware image in flash */
#ifndef CONFIG_FW_IMAGE_SIZE
#define CONFIG_FW_IMAGE_SIZE    (CONFIG_FLASH_PHYSICAL_SIZE / 2)
#endif

/* RO firmware offset of flash */
#define CONFIG_FW_RO_OFF	0

/*
 * The EC uses the one bank of flash to emulate a SPI-like write protect
 * register with persistent state.
 */
#define CONFIG_FW_PSTATE_SIZE   CONFIG_FLASH_BANK_SIZE

#ifdef CONFIG_PSTATE_AT_END
/* PSTATE is at end of flash */
#define CONFIG_FW_RO_SIZE       CONFIG_FW_IMAGE_SIZE
#define CONFIG_FW_PSTATE_OFF    (CONFIG_FLASH_PHYSICAL_SIZE \
				- CONFIG_FW_PSTATE_SIZE)
/* Don't claim PSTATE is part of flash */
#define CONFIG_FLASH_SIZE       CONFIG_FW_PSTATE_OFF

#else
/* PSTATE immediately follows RO, in the first half of flash */
#define CONFIG_FW_RO_SIZE       (CONFIG_FW_IMAGE_SIZE \
				- CONFIG_FW_PSTATE_SIZE)
#define CONFIG_FW_PSTATE_OFF    CONFIG_FW_RO_SIZE
#define CONFIG_FLASH_SIZE       CONFIG_FLASH_PHYSICAL_SIZE
#endif

/* Either way, RW firmware is one firmware image offset from the start */
#define CONFIG_FW_RW_OFF        CONFIG_FW_IMAGE_SIZE
#define CONFIG_FW_RW_SIZE       CONFIG_FW_IMAGE_SIZE

/* TODO(crosbug.com/p/23796): why 2 sets of configs with the same numbers? */
#define CONFIG_FW_WP_RO_OFF     CONFIG_FW_RO_OFF
#define CONFIG_FW_WP_RO_SIZE    CONFIG_FW_RO_SIZE

/*
 * The offset from top of flash wich used by booter
 * the main funcationality to copy iamge from spi-flash to code ram
 */
#define CONFIG_LFW_OFFSET       0x1000

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
#define CONFIG_MPU
#define CONFIG_SPI


/* Compile for running from RAM instead of flash */
/* #define COMPILE_FOR_RAM */

#endif  /* __CROS_EC_CONFIG_CHIP_H */
