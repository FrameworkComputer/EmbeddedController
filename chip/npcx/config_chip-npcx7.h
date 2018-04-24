/* Copyright 2017 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_CONFIG_CHIP_NPCX7_H
#define __CROS_EC_CONFIG_CHIP_NPCX7_H

/*
 * NPCX7 Series Device-Specific Information
 * Ex. NPCX7(M)(N)(G/K/F)(B/C)
 * @param M: 8: 128-pins package, 9: 144-pins package
 * @param N: 5: 128KB RAM Size, 6: 256KB RAM Size, 7: 384KB RAM Size
 * @param G/K/F/W: Google EC depends on specific features.
 * @param B/C: (Optional) Chip generation in the same series.
 */

/*****************************************************************************/
/* Hardware features */

/* The optional hardware features depend on chip variant */
#if defined(CHIP_VARIANT_NPCX7M6F) || defined(CHIP_VARIANT_NPCX7M6FB) || \
	defined(CHIP_VARIANT_NPCX7M7WB)
#define NPCX_INT_FLASH_SUPPORT /* Internal flash support */
#define NPCX_PSL_MODE_SUPPORT /* Power switch logic mode for ultra-low power */
#define NPCX_EXT32K_OSC_SUPPORT /* External 32KHz crytal osc. input support */
#endif

#if defined(CHIP_VARIANT_NPCX7M6FB) || defined(CHIP_VARIANT_NPCX7M7WB)
#define NPCX_UART_FIFO_SUPPORT
#endif

#ifdef CHIP_VARIANT_NPCX7M7WB
#define NPCX_WOV_SUPPORT /* Audio front-end for Wake-on-Voice support */
#endif

/*
 * Number of I2C controllers. Controller 4/5/6 has 2 ports, so the chip has
 * three additional ports.
 */
#define CONFIG_I2C_MULTI_PORT_CONTROLLER
/* Number of I2C controllers */
#define I2C_CONTROLLER_COUNT 8
/* Number of I2C ports */
#ifdef NPCX_PSL_MODE_SUPPORT
#define I2C_PORT_COUNT 10
#else
#define I2C_PORT_COUNT 11
#endif

/* Use SHI module version 2 supported by npcx7 family */
#define NPCX_SHI_V2

/*****************************************************************************/
/* Memory mapping */
#define NPCX_BTRAM_SIZE    0x400 /* 1KB data ram used by booter. */
#define CONFIG_RAM_BASE    0x200C0000 /* memory address of data ram */
/* 63KB data RAM */
#define CONFIG_DATA_RAM_SIZE    0x00010000
#define CONFIG_RAM_SIZE         (CONFIG_DATA_RAM_SIZE - NPCX_BTRAM_SIZE)
/* no low power ram in npcx7 series */

/* Use chip variant to specify the size and start address of program memory */
#if defined(CHIP_VARIANT_NPCX7M6F) || defined(CHIP_VARIANT_NPCX7M6FB) || \
	defined(CHIP_VARIANT_NPCX7M6G)
/* 192KB RAM for FW code */
#define NPCX_PROGRAM_MEMORY_SIZE (192 * 1024)
/* program memory base address for 192KB Code RAM (ie. 0x100C0000 - 192KB) */
#define CONFIG_PROGRAM_MEMORY_BASE 0x10090000
#elif defined(CHIP_VARIANT_NPCX7M7WB)
/* 320 RAM for FW code */
#define NPCX_PROGRAM_MEMORY_SIZE (320 * 1024)
/* program memory base address for 320KB Code RAM (ie. 0x100C0000 - 320KB) */
#define CONFIG_PROGRAM_MEMORY_BASE 0x10070000
#else
#error "Unsupported chip variant"
#endif

/* Total RAM size checking for npcx ec */
#define NPCX_RAM_SIZE (CONFIG_DATA_RAM_SIZE + NPCX_PROGRAM_MEMORY_SIZE)
#if defined(CHIP_VARIANT_NPCX7M6F) || defined(CHIP_VARIANT_NPCX7M6FB) || \
	defined(CHIP_VARIANT_NPCX7M6G)
/* 256KB RAM in NPCX7M6F/NPCX7M6G/NPCX7M6FB */
#if (NPCX_RAM_SIZE != 0x40000)
#error "Wrong memory mapping layout for NPCX7M6F"
#endif
#elif defined(CHIP_VARIANT_NPCX7M7WB)
/* 384KB RAM in NPCX7M7WB */
#if (NPCX_RAM_SIZE != 0x60000)
#error "Wrong memory mapping layout for NPCX7M6F"
#endif
#endif

#endif /* __CROS_EC_CONFIG_CHIP_NPCX7_H */
