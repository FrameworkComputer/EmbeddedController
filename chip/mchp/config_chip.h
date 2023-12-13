/* Copyright 2017 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_CONFIG_CHIP_H
#define __CROS_EC_CONFIG_CHIP_H

/* CPU core BFD configuration */
#include "core/cortex-m/config_core.h"

/* Number of IRQ vectors on the NVIC */
#ifdef CHIP_FAMILY_MEC152X
#define CONFIG_IRQ_COUNT 174
#elif defined(CHIP_FAMILY_MEC170X)
#define CONFIG_IRQ_COUNT 157
#elif defined(CHIP_FAMILY_MEC172X)
#define CONFIG_IRQ_COUNT 181
#endif

/* Use a bigger console output buffer */
#undef CONFIG_UART_TX_BUF_SIZE
#define CONFIG_UART_TX_BUF_SIZE 1024

/* Interval between HOOK_TICK notifications */
#define HOOK_TICK_INTERVAL_MS 250
#define HOOK_TICK_INTERVAL (HOOK_TICK_INTERVAL_MS * MSEC)

/*
 * Enable chip_pre_init called from main
 * Used for configuring peripheral block
 * sleep enables.
 */
#define CONFIG_CHIP_PRE_INIT

/*
 * MCHP EC's have I2C controllers and multiple I2C ports. Any port may be
 * mapped to any controller at run time. Enable multi-port controller feature.
 * Board level configuration determines how many controllers/ports are used
 * and the mapping of port(s) to controller(s). NOTE: Some MCHP packages
 * may not implement all I2C ports.
 */
#define CONFIG_I2C_MULTI_PORT_CONTROLLER

/*
 * MCHP I2C controllers also act as I2C peripherals listening for their
 * peripheral address. Each controller has two programmable peripheral
 * addresses. Define fake peripheral addresses that aren't used by
 * peripherals on the board.
 */
#define CONFIG_MCHP_I2C0_SLAVE_ADDRS 0xE3E1
#define CONFIG_MCHP_I2C1_SLAVE_ADDRS 0xE3E1
#define CONFIG_MCHP_I2C2_SLAVE_ADDRS 0xE3E1
#define CONFIG_MCHP_I2C3_SLAVE_ADDRS 0xE3E1
#define CONFIG_MCHP_I2C4_SLAVE_ADDRS 0xE3E1
#define CONFIG_MCHP_I2C5_SLAVE_ADDRS 0xE3E1
#define CONFIG_MCHP_I2C6_SLAVE_ADDRS 0xE3E1
#define CONFIG_MCHP_I2C7_SLAVE_ADDRS 0xE3E1

/************************************************************************/
/* Memory mapping */

/*
 * MEC170x-H and MEC152x-H have a total of 256KB SRAM.
 *   CODE at 0xE0000 - 0x117FFF, DATA at 0x118000 - 0x11FFFF
 * MEC172x-N has a total of 416KB SRAM: 352KB CODE 64KB DATA
 *   CODE at 0xC0000 - 0x117FFF, DATA at 0x118000 - 0x127FFF
 *   Customer data preserved across reset is 1KB at 0x12_7400.
 *   Set top of SRAM to 0x12_7800.  We lose the top 2KB.
 * MCHP MEC can fetch code from data or data from code.
 */

/************************************************************************/
/* Define our RAM layout. */

#if defined(CHIP_FAMILY_MEC172X)
#define CONFIG_MEC_SRAM_BASE_START 0x000C0000
#define CONFIG_MEC_SRAM_BASE_END (0x00128000 - (2 * 1024))
#else
#define CONFIG_MEC_SRAM_BASE_START 0x000E0000
#define CONFIG_MEC_SRAM_BASE_END 0x00120000
#endif

#define CONFIG_MEC_SRAM_SIZE \
	(CONFIG_MEC_SRAM_BASE_END - CONFIG_MEC_SRAM_BASE_START)
/* 64k Data RAM for RO / RW / loader */
#define CONFIG_RAM_SIZE 0x00010000
#define CONFIG_RAM_BASE (CONFIG_MEC_SRAM_BASE_END - CONFIG_RAM_SIZE)

/* System stack size */
/* was 1024, temporarily expanded to 2048 for debug */
#define CONFIG_STACK_SIZE 2048

/* non-standard task stack sizes */
#define IDLE_TASK_STACK_SIZE 672
#define LARGER_TASK_STACK_SIZE 800
#define VENTI_TASK_STACK_SIZE 928
#define ULTRA_TASK_STACK_SIZE 1056
#define TRENTA_TASK_STACK_SIZE 1184

#define CHARGER_TASK_STACK_SIZE 1024 /* 640 */
#define HOOKS_TASK_STACK_SIZE 1024 /* 640 */
#define CONSOLE_TASK_STACK_SIZE 1024 /* 640 */
#define HOST_CMD_TASK_STACK_SIZE 1024 /* 640 */

/*
 * TODO: Large stack consumption
 * https://code.google.com/p/chrome-os-partner/issues/detail?id=49245
 */
/* original = 800, if stack exceptions expand to 1024 for debug */
#define PD_TASK_STACK_SIZE 2048

/* Default task stack size */
#define TASK_STACK_SIZE 672

/************************************************************************/
/* Define our flash layout. */

/*
 * MEC1521H loads firmware using QMSPI controller
 * CONFIG_SPI_FLASH_PORT is the index into
 * spi_devices[] in board.c
 */
#define CONFIG_SPI_FLASH_PORT 0
#define CONFIG_SPI_FLASH

/*
 * MEC1727 chip has integrated SPI flash with 512KB size
 */
#if (defined(CHIP_VARIANT_MEC1727SZ) || defined(CHIP_VARIANT_MEC1727LJ))
/* Total size of writable flash */
#define CONFIG_FLASH_SIZE_BYTES 524288
#endif

/* Protect bank size 4K bytes */
#define CONFIG_FLASH_BANK_SIZE 0x00001000
/* Sector erase size 4K bytes */
#define CONFIG_FLASH_ERASE_SIZE 0x00001000
/* Minimum write size */
#define CONFIG_FLASH_WRITE_SIZE 0x00000004

/* One page size for write */
#define CONFIG_FLASH_WRITE_IDEAL_SIZE 256

/* Program memory base address */
#if defined(CHIP_FAMILY_MEC172X)
#define CONFIG_PROGRAM_MEMORY_BASE 0x000C0000
#else
#define CONFIG_PROGRAM_MEMORY_BASE 0x000E0000
#endif

/*
 * Optimize SPI flash read timing, MEC172x QMSPI controller controls CS#
 * by hardware, it will add several system clock cycles delay between CS
 * deassertion to CS assertion at the start of the next transaction, this
 * guarantees SPI back to back transactions, so 1ms delay can be removed
 * to optimze timing. MEC172x chip supports this hardware feature.
 */
#if defined(CHIP_FAMILY_MEC172X)
#undef CONFIG_SPI_FLASH_READ_WAIT_MS
#define CONFIG_SPI_FLASH_READ_WAIT_MS 0
#endif

#include "config_flash_layout.h"

/************************************************************************/
/* Customize the build */
/* Optional features present on this chip */
#define CONFIG_ADC
#define CONFIG_DMA_CROS
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
#if defined(CHIP_FAMILY_MEC170X) || defined(CHIP_FAMILY_MEC172X)
#define CONFIG_MCHP_GPSPI_TX_DMA
#endif

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

#endif /* __CROS_EC_CONFIG_CHIP_H */
