/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_CONFIG_CHIP_H
#define __CROS_EC_CONFIG_CHIP_H

/* CPU core BFD configuration */
#include "core/cortex-m/config_core.h"

/* 32k hz internal oscillator frequency (FRCLK) */
#define INT_32K_CLOCK 32768

/* Number of IRQ vectors on the NVIC */
#define CONFIG_IRQ_COUNT 64

/* Use a bigger console output buffer */
#undef CONFIG_UART_TX_BUF_SIZE
#define CONFIG_UART_TX_BUF_SIZE 8192

/*
 * Interval between HOOK_TICK notifications
 * Notice instant wake-up from deep-idle cannot exceed 200 ms
 */
#define HOOK_TICK_INTERVAL_MS 200
#define HOOK_TICK_INTERVAL    (HOOK_TICK_INTERVAL_MS * MSEC)

/* Maximum number of deferrable functions */
#define DEFERRABLE_MAX_COUNT 8

/*
 * Number of I2C controllers. Controller 0 has 2 ports, so the chip has one
 * additional port.
 */
#define CONFIG_I2C_MULTI_PORT_CONTROLLER
/* Number of I2C controllers */
#define I2C_CONTROLLER_COUNT	4
/* Number of I2C ports */
#define I2C_PORT_COUNT		5


/* Number of PWM ports */
#define PWM_COUNT 8

/*****************************************************************************/
/* Memory mapping */
#define CONFIG_RAM_BASE         0x200C0000 /* memory map address of data ram */
#define CONFIG_RAM_SIZE         (0x00008000 - 0x800) /* 30KB data ram */
#define CONFIG_CDRAM_BASE       0x100A8000 /* memory map address of code ram */
#define CONFIG_CDRAM_SIZE       0x00018000 /* 96KB code ram */
#define CONFIG_FLASH_BASE	0x64000000 /* memory address of spi-flash */
#define CONFIG_LPRAM_BASE       0x40001600 /* memory address of low power ram */
#define CONFIG_LPRAM_SIZE	0x00000620 /* 1568B low power ram */

/* System stack size */
#define CONFIG_STACK_SIZE       4096

/* non-standard task stack sizes */
#define IDLE_TASK_STACK_SIZE		512
#define LARGER_TASK_STACK_SIZE		640

#define CHARGER_TASK_STACK_SIZE		640
#define HOOKS_TASK_STACK_SIZE		640
#define CONSOLE_TASK_STACK_SIZE		640

/* Default task stack size */
#define TASK_STACK_SIZE			512

/* Address of RAM log used by Booter */
#define ADDR_BOOT_RAMLOG        0x100C7FC0

/* SPI Flash Spec of W25Q20CV */
#define CONFIG_FLASH_BANK_SIZE	0x00001000  /* protect bank size 4K bytes */
#define CONFIG_FLASH_ERASE_SIZE	0x00001000  /* sector erase size 4K bytes */
#define CONFIG_FLASH_WRITE_SIZE	0x00000001  /* minimum write size */

#define CONFIG_FLASH_WRITE_IDEAL_SIZE 256   /* one page size for write */
/* 128 KB alignment for SPI status registers protection */
#define CONFIG_FLASH_PHYSICAL_SIZE  0x40000 /* 256 KB Flash used for EC */

/* No PSTATE; uses a real SPI flash */
#undef CONFIG_FLASH_PSTATE

/* Header support which is used by booter to copy FW from flash to code ram */
#define NPCX_RO_HEADER

/****************************************************************************/
/* Define npcx flash layout. */
/* Size of one firmware image in flash */
#ifndef CONFIG_FW_IMAGE_SIZE
#define CONFIG_FW_IMAGE_SIZE    (CONFIG_FLASH_PHYSICAL_SIZE / 2)
#endif

/* The storage offset of ec.RO.flat which is used for CONFIG_CDRAM_ARCH */
#define CONFIG_RO_STORAGE_OFF   0
#ifdef NPCX_RO_HEADER
#define CONFIG_RO_HDR_MEM_OFF   0x0
#define CONFIG_RO_HDR_SIZE      0x40
/* RO firmware offset in flash */
#define CONFIG_RO_MEM_OFF       CONFIG_RO_HDR_SIZE
#else
#define CONFIG_RO_MEM_OFF       0x0
#endif
#define CONFIG_RO_SIZE          CONFIG_CDRAM_SIZE    /* 96KB for RO FW */
#define CONFIG_FLASH_SIZE       CONFIG_FLASH_PHYSICAL_SIZE

/* The storage offset of ec.RW.flat which is used for CONFIG_CDRAM_ARCH */
#define CONFIG_RW_STORAGE_OFF   CONFIG_FW_IMAGE_SIZE /* 128 KB alignemnt */
/* RW firmware offset in flash */
#define CONFIG_RW_MEM_OFF       CONFIG_RW_STORAGE_OFF
#define CONFIG_RW_SIZE          CONFIG_CDRAM_SIZE    /* 96KB for RW FW */

#define CONFIG_WP_OFF           CONFIG_RO_STORAGE_OFF
#define CONFIG_WP_SIZE          CONFIG_FW_IMAGE_SIZE

/****************************************************************************/
/* Customize the build */

/* Optional features present on this chip */
#define CONFIG_ADC
#define CONFIG_FPU
#define CONFIG_I2C
#define CONFIG_PECI
#define CONFIG_SWITCH
#define CONFIG_MPU

/* Compile for running from RAM instead of flash */
/* #define COMPILE_FOR_RAM */

#define GPIO_PIN(port, index) GPIO_##port, (1 << index)
#define GPIO_PIN_MASK(port, mask) GPIO_##port, (mask)

#endif  /* __CROS_EC_CONFIG_CHIP_H */
