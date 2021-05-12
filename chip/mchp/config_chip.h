/* Copyright 2017 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_CONFIG_CHIP_H
#define __CROS_EC_CONFIG_CHIP_H

/* CPU core BFD configuration */
#include "core/cortex-m/config_core.h"

/* Number of IRQ vectors on the NVIC */
#ifdef CHIP_FAMILY_MEC17XX
#define CONFIG_IRQ_COUNT	157
#elif defined(CHIP_FAMILY_MEC152X)
#define CONFIG_IRQ_COUNT	172
#endif

/* Use a bigger console output buffer */
#undef CONFIG_UART_TX_BUF_SIZE
#define CONFIG_UART_TX_BUF_SIZE	1024

/* Interval between HOOK_TICK notifications */
#define HOOK_TICK_INTERVAL_MS	250
#define HOOK_TICK_INTERVAL	(HOOK_TICK_INTERVAL_MS * MSEC)

/*
 * Enable chip_pre_init called from main
 * Used for configuring peripheral block
 * sleep enables.
 */
#define CONFIG_CHIP_PRE_INIT

/*
 * MCHP EC's have I2C master/slave
 * controllers and multiple I2C ports. Any
 * port may be mapped to any controller.
 * Enable multi-port controller feature.
 * Board level configuration determines
 * how many controllers/ports are used and
 * the mapping of port(s) to controller(s).
 * NOTE: Some MCHP reduced pin packages
 * may not implement all 11 I2C ports.
 */
#define CONFIG_I2C_MULTI_PORT_CONTROLLER

/*
 * MCHP I2C controller is master-slave capable and requires
 * a slave address be programmed even if used as master only.
 * Each I2C controller can respond to two slave address.
 * Define fake slave addresses that aren't used on the I2C port(s)
 * connected to each controller.
 */
#define CONFIG_MCHP_I2C0_SLAVE_ADDRS	0xE3E1
#define CONFIG_MCHP_I2C1_SLAVE_ADDRS	0xE3E1
#define CONFIG_MCHP_I2C2_SLAVE_ADDRS	0xE3E1
#define CONFIG_MCHP_I2C3_SLAVE_ADDRS	0xE3E1


/************************************************************************/
/* Memory mapping */

/*
 * MEC1701H has a total of 256KB SRAM.
 *   CODE at 0xE0000 - 0x117FFF, DATA at 0x118000 - 0x11FFFF
 *   MCHP MEC can fetch code from data or data from code.
 */

/************************************************************************/
/* Define our RAM layout. */

#define CONFIG_MEC_SRAM_BASE_START	0x000E0000
#define CONFIG_MEC_SRAM_BASE_END	0x00120000
#define CONFIG_MEC_SRAM_SIZE		(CONFIG_MEC_SRAM_BASE_END - \
					CONFIG_MEC_SRAM_BASE_START)

/* 64k Data RAM for RO / RW / loader */
#define CONFIG_RAM_SIZE			0x00010000
#define CONFIG_RAM_BASE			(CONFIG_MEC_SRAM_BASE_END - \
					CONFIG_RAM_SIZE)

/* System stack size */
/* was 1024, temporarily expanded to 2048 for debug */
#define CONFIG_STACK_SIZE		2048

/* non-standard task stack sizes */
/* temporarily expanded for debug */
#define IDLE_TASK_STACK_SIZE		1024	/* 512 */
#define LARGER_TASK_STACK_SIZE		1024	/* 640 */
#define VENTI_TASK_STACK_SIZE		1024	/* 768 */

#define CHARGER_TASK_STACK_SIZE		1024	/* 640 */
#define HOOKS_TASK_STACK_SIZE		1024	/* 640 */
#define CONSOLE_TASK_STACK_SIZE		1024	/* 640 */
#define HOST_CMD_TASK_STACK_SIZE	1024	/* 640 */

/*
 * TODO: Large stack consumption
 * https://code.google.com/p/chrome-os-partner/issues/detail?id=49245
 */
/* dsw original = 800, if stack exceptions expand to 1024 for debug */
#define PD_TASK_STACK_SIZE		2048

/* Default task stack size */
#define TASK_STACK_SIZE			1024	/* 512 */

/************************************************************************/
/* Define our flash layout. */

/* Protect bank size 4K bytes */
#define CONFIG_FLASH_BANK_SIZE		0x00001000
/* Sector erase size 4K bytes */
#define CONFIG_FLASH_ERASE_SIZE		0x00001000
/* Minimum write size */
#define CONFIG_FLASH_WRITE_SIZE		0x00000004

/* One page size for write */
#define CONFIG_FLASH_WRITE_IDEAL_SIZE	256

/* Program memory base address */
#define CONFIG_PROGRAM_MEMORY_BASE	0x000E0000

#include "config_flash_layout.h"

/************************************************************************/
/* Customize the build */
/* Optional features present on this chip */
#define CONFIG_ADC
#define CONFIG_DMA
#define CONFIG_HOSTCMD_X86
#define CONFIG_SPI
#define CONFIG_SWITCH

/*
 * Enable configuration after ESPI_RESET# de-asserts
 */
#undef CONFIG_MCHP_ESPI_RESET_DEASSERT_INIT

/*
 * Enable CPRINT in chip eSPI module
 * Define at board level.
 */
#undef CONFIG_MCHP_ESPI_DEBUG

/*
 * Enable EC UART commands in eSPI module useful for debugging.
 */
#undef CONFIG_MCHP_ESPI_EC_CMD

/*
 * Enable CPRINT debug messages in LPC module
 */
#undef CONFIG_MCHP_DEBUG_LPC

/*
 * Define this to use MEC1701 ROM SPI read API
 * in little firmware module instead of SPI code
 * from this module
 */
#undef CONFIG_CHIP_LFW_USE_ROM_SPI

/*
 * Use DMA when transmitting commands & data
 * with GPSPI controllers.
 */
#define CONFIG_MCHP_GPSPI_TX_DMA

/*
 * Use DMA when transmitting command & data of length
 * greater than QMSPI TX FIFO size.
 */
#define CONFIG_MCHP_QMSPI_TX_DMA

/*
 * Board level gpio.inc is using MCHP data sheet GPIO pin
 * numbers which are octal.
 * MCHP has 6 banks/ports each containing 32 GPIO's.
 * Each bank/port is connected to a GIRQ.
 * Port numbering:
 * GPIO_015 = 13 decimal. Port = 13/32 = 0, bit = 13 % 32 = 13
 * GPIO_0123 = 83 decimal. Port 83/32 = 2, bit = 83 % 32 = 19
 * OR port = 0123 >> 5, bit = 0123 & 037(0x1F) = 023 = 19 decimal.
 * You must use octal GPIO numbers in PIN(gpio_num) macro in
 * gpio.inc files.
 * Example: GPIO 211 in documentation 0211 = 137 = 0x89
 * GPIO(PCH_SLP_S0_L, PIN(0211), GPIO_INPUT | GPIO_PULL_DOWN)
 * OR
 * GPIO(PCH_SLP_S0_L, PIN(0x89), GPIO_INPUT | GPIO_PULL_DOWN)
 */
#define GPIO_BANK(index) ((index) >> 5)
#define GPIO_BANK_MASK(index) (1ul << ((index) & 0x1F))

#define GPIO_PIN(index) GPIO_BANK(index), GPIO_BANK_MASK(index)
#define GPIO_PIN_MASK(p, m) .port = (p), .mask = (m)

#ifndef __ASSEMBLER__


#endif /* #ifndef __ASSEMBLER__ */

#endif  /* __CROS_EC_CONFIG_CHIP_H */
