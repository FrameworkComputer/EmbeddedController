/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_CONFIG_CHIP_H
#define __CROS_EC_CONFIG_CHIP_H

/* CPU core BFD configuration */
#include "core/cortex-m/config_core.h"

/* Number of IRQ vectors on the NVIC */
#define CONFIG_IRQ_COUNT	93

/* Use a bigger console output buffer */
#undef CONFIG_UART_TX_BUF_SIZE
#define CONFIG_UART_TX_BUF_SIZE	2048

/* Interval between HOOK_TICK notifications */
#define HOOK_TICK_INTERVAL_MS	250
#define HOOK_TICK_INTERVAL	(HOOK_TICK_INTERVAL_MS * MSEC)

/* Maximum number of deferrable functions */
#define DEFERRABLE_MAX_COUNT	8

/*
 * Number of I2C controllers. Controller 0 has 2 ports, so the chip has one
 * additional port.
 */
#define CONFIG_I2C_MULTI_PORT_CONTROLLER

#define I2C_CONTROLLER_COUNT	4
#define I2C_PORT_COUNT		5

/****************************************************************************/
/* Memory mapping */

/*
 * The memory region for RAM is actually 0x00100000-0x00120000.
 * RAM for Loader = 2k
 * RAM for RO/RW = 24k
 * CODE size of the Loader is 4k
 * As per the above configuartion  the upper 26k
 * is used to store data.The rest is for code.
 * the lower 100K is flash[ 4k Loader and 96k RO/RW],
 * and the higher 26K is RAM shared by loader and RO/RW.
 */

/****************************************************************************/
/* Define our RAM layout. */

#define CONFIG_MEC_SRAM_BASE_START	0x00100000
#define CONFIG_MEC_SRAM_BASE_END	0x00120000
#define CONFIG_MEC_SRAM_SIZE		(CONFIG_MEC_SRAM_BASE_END - \
					CONFIG_MEC_SRAM_BASE_START)

/* 2k RAM for Loader */
#define CONFIG_RAM_SIZE_LOADER		0x00000800
/* 24k RAM for RO /RW */
#define CONFIG_RAM_SIZE_RORW		0x00006000

#define CONFIG_RAM_SIZE_TOTAL		(CONFIG_RAM_SIZE_LOADER + \
					CONFIG_RAM_SIZE_RORW)
#define CONFIG_RAM_BASE_RORW		(CONFIG_MEC_SRAM_BASE_END - \
					CONFIG_RAM_SIZE_TOTAL)
#define CONFIG_RAM_BASE			CONFIG_RAM_BASE_RORW
#define CONFIG_RAM_SIZE			CONFIG_RAM_SIZE_TOTAL

/* System stack size */
#define CONFIG_STACK_SIZE		4096

/* non-standard task stack sizes */
#define IDLE_TASK_STACK_SIZE		512
#define LARGER_TASK_STACK_SIZE		640

#define CHARGER_TASK_STACK_SIZE		640
#define HOOKS_TASK_STACK_SIZE		640
#define CONSOLE_TASK_STACK_SIZE		640

/* Default task stack size */
#define TASK_STACK_SIZE			512

/****************************************************************************/
/* Define our flash layout. */

/* Protect bank size 4K bytes */
#define CONFIG_FLASH_BANK_SIZE		0x00001000
/* Sector erase size 4K bytes */
#define CONFIG_FLASH_ERASE_SIZE		0x00001000
/* Minimum write size */
#define CONFIG_FLASH_WRITE_SIZE		0x00000004

/* One page size for write */
#define CONFIG_FLASH_WRITE_IDEAL_SIZE	256

/* Independent of the Flash Physical size of the board
256KB Max size used. Located at the top most segment */
#define CONFIG_FLASH_PHYSICAL_SIZE	0x00040000

/* Program memory base address */
#define CONFIG_FLASH_BASE		0x00100000

#define CONFIG_CDRAM_BASE		0x00100000
#define CONFIG_CDRAM_SIZE		0x00020000

/* Size of one firmware image in flash */
#ifndef CONFIG_FW_IMAGE_SIZE
#define CONFIG_FW_IMAGE_SIZE		(96 * 1024)
#endif

/* Loader resides at the beginning of program memory */
#define CONFIG_LOADER_MEM_OFF		0
#define CONFIG_LOADER_SIZE		0x1000

/*
 * RO / RW images follow the loader in program memory. Either RO or RW
 * image will be loaded -- both cannot be loaded at the same time.
 */
#define CONFIG_RO_MEM_OFF		(CONFIG_LOADER_MEM_OFF + \
					CONFIG_LOADER_SIZE)
#define CONFIG_RO_SIZE			CONFIG_FW_IMAGE_SIZE
#define CONFIG_RW_MEM_OFF		CONFIG_RO_MEM_OFF
#define CONFIG_RW_SIZE			CONFIG_RO_SIZE

#define CONFIG_FLASH_SIZE		CONFIG_FLASH_PHYSICAL_SIZE
#define CONFIG_FW_INCLUDE_RO

/* Write protect Loader and RO Image */
#define CONFIG_WP_OFF			(CONFIG_FLASH_PHYSICAL_SIZE >> 1)
/* Write protect 128k section of 256k physical flash
which contains Loader and RO Images */
#define CONFIG_WP_SIZE			(CONFIG_FLASH_PHYSICAL_SIZE >> 1)
/****************************************************************************/
/* SPI Flash Memory Mapping */

/* Size of SPI memory used (lfw + RSA Keys + RO + RW + boot header) */
#define CONFIG_FLASH_BASE_SPI		(CONFIG_SPI_FLASH_SIZE - (0x40000))

/* RW image starts at the beginning of SPI */
#define CONFIG_RW_STORAGE_OFF		0

/* WP region consists of second half of SPI, and begins with the boot header */
#define CONFIG_BOOT_HEADER_STORAGE_OFF	CONFIG_WP_OFF
#define CONFIG_BOOT_HEADER_STORAGE_SIZE	0x240

/* Loader / lfw image immediately follows the boot header on SPI */
#define CONFIG_LOADER_STORAGE_OFF	(CONFIG_BOOT_HEADER_STORAGE_OFF + \
					CONFIG_BOOT_HEADER_STORAGE_SIZE)

/* RO image immediately follows the loader image */
#define CONFIG_RO_STORAGE_OFF		(CONFIG_LOADER_STORAGE_OFF + \
					CONFIG_LOADER_SIZE)

#define CONFIG_RO_IMAGE_FLASHADDR	(CONFIG_FLASH_BASE_SPI + \
					CONFIG_RO_STORAGE_OFF)
#define CONFIG_RW_IMAGE_FLASHADDR	(CONFIG_FLASH_BASE_SPI + \
					CONFIG_RW_STORAGE_OFF)

/* Memory Lcation shared between lfw and RO /RW image */
#define SHARED_RAM_LFW_RORW		(CONFIG_MEC_SRAM_BASE_START + \
					(CONFIG_LOADER_SIZE - 4))

/* Non-memmapped, external SPI */
/* #define CONFIG_CODERAM_ARCH */
#undef  CONFIG_FLASH_MAPPED
#undef  CONFIG_FLASH_PSTATE
#define CONFIG_SPI_FLASH

/****************************************************************************/
/* Customize the build */
/* Optional features present on this chip */
#if 0
#define CONFIG_ADC
#define CONFIG_PECI
#define CONFIG_MPU
#endif
#define CONFIG_DMA
#define CONFIG_FPU
#define CONFIG_I2C
#define CONFIG_LPC
#define CONFIG_SPI
#define CONFIG_SWITCH

#define GPIO_PIN(index) (index / 10), (1 << (index % 10))
#define GPIO_PIN_MASK(pin, mask) (pin), (mask)

#endif  /* __CROS_EC_CONFIG_CHIP_H */
