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

/* Chip ID for all variants */
#define NPCX787G_CHIP_ID		0x1F
#define NPCX796F_A_B_CHIP_ID		0x21
#define NPCX796F_C_CHIP_ID		0x29
#define NPCX797F_C_CHIP_ID		0x20
#define NPCX797W_B_CHIP_ID		0x24
#define NPCX797W_C_CHIP_ID		0x2C

/*****************************************************************************/
/* Hardware features */

/* The optional hardware features depend on chip variant */
#if defined(CHIP_VARIANT_NPCX7M6F) || defined(CHIP_VARIANT_NPCX7M6FB) || \
	defined(CHIP_VARIANT_NPCX7M6FC) || defined(CHIP_VARIANT_NPCX7M7FC) || \
	defined(CHIP_VARIANT_NPCX7M7WB) || defined(CHIP_VARIANT_NPCX7M7WC)
#define NPCX_INT_FLASH_SUPPORT /* Internal flash support */
#define NPCX_PSL_MODE_SUPPORT /* Power switch logic mode for ultra-low power */
#define NPCX_EXT32K_OSC_SUPPORT /* External 32KHz crytal osc. input support */
#endif

#if defined(CHIP_VARIANT_NPCX7M6FB) || defined(CHIP_VARIANT_NPCX7M6FC) || \
	defined(CHIP_VARIANT_NPCX7M7FC) || defined(CHIP_VARIANT_NPCX7M7WB) || \
	defined(CHIP_VARIANT_NPCX7M7WC)
#define NPCX_UART_FIFO_SUPPORT
/* Number of UART modules. */
#define NPCX_SECOND_UART
#define UART_MODULE_COUNT 2

/* 64-bit timer support */
#define NPCX_ITIM64_SUPPORT
#else
#define UART_MODULE_COUNT 1
#endif

#if defined(CHIP_VARIANT_NPCX7M7WB) || defined(CHIP_VARIANT_NPCX7M7WC)
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

#define NPCX_I2C_FIFO_SUPPORT

/* Use SHI module version 2 supported by npcx7 family */
#define NPCX_SHI_V2

/*****************************************************************************/
/* Memory mapping */
#define NPCX_BTRAM_SIZE    0x800 /* 2KB data ram used by booter. */

#define NPCX_RAM_SIZE (CONFIG_DATA_RAM_SIZE + NPCX_PROGRAM_MEMORY_SIZE)

#if defined(CHIP_VARIANT_NPCX7M6F) || defined(CHIP_VARIANT_NPCX7M6FB) || \
	defined(CHIP_VARIANT_NPCX7M6FC) || defined(CHIP_VARIANT_NPCX7M6G)
	/* 192KB RAM for FW code */
#	define NPCX_PROGRAM_MEMORY_SIZE (192 * 1024)
	/* program memory base address for Code RAM (0x100C0000 - 192KB) */
#	define CONFIG_PROGRAM_MEMORY_BASE 0x10090000
#	define CONFIG_RAM_BASE    0x200C0000 /* memory address of data ram */
	/* 62 KB data RAM + 2 KB BT RAM size */
#	define CONFIG_DATA_RAM_SIZE    0x00010000
#elif defined(CHIP_VARIANT_NPCX7M7WB)
	/* 256KB RAM for FW code */
#	define NPCX_PROGRAM_MEMORY_SIZE (256 * 1024)
	/* program memory base address for Code RAM (0x100B0000 - 256KB) */
#	define CONFIG_PROGRAM_MEMORY_BASE 0x10070000
#	define CONFIG_RAM_BASE    0x200B0000 /* memory address of data ram */
	/* 126 KB data RAM + 2 KB BT RAM size */
#	define CONFIG_DATA_RAM_SIZE    0x00020000
#elif defined(CHIP_VARIANT_NPCX7M7FC) || defined(CHIP_VARIANT_NPCX7M7WC)
	/*
	 * Code RAM is normally assumed to be same as image size, but since
	 * we exclude 4k from the image (see NPCX_PROGRAM_MEMORY_SIZE) we
	 * need to explicitly configure it. This is the actual size of code
	 * RAM on-chip.
	 */
#	define CONFIG_CODE_RAM_SIZE (256 * 1024)
	/*
	 * In npcx797wc and npcx797fc, the code RAM size is limited by the
	 * internal flash size (i.e. 512 KB/2=256 KB.) The driver has to
	 * re-organize the memory to:
	 * 1. the overall memory (RAM) layout is re-organized against the
	 * datasheet:
	 *     In datasheet: 320 KB code RAM + 64 KB data RAM
	 *     After re-organization: 256 KB code RAM + 128 KB data RAM.
	 * 2. 256KB program RAM, but only 512K of Flash (vs 1M for the
	 * -WB). After the boot header is added, a 256K image would be
	 * too large to fit in either RO or RW sections of Flash (each
	 * of which is half of it). Because other code assumes that
	 * image size is a multiple of Flash erase granularity, we
	 * sacrifice a whole sector.
	 */
#	define NPCX_PROGRAM_MEMORY_SIZE (CONFIG_CODE_RAM_SIZE - 0x1000)
	/* program memory base address for Code RAM (0x100B0000 - 256KB) */
#	define CONFIG_PROGRAM_MEMORY_BASE 0x10070000
#	define CONFIG_RAM_BASE    0x200B0000 /* memory address of data ram */
	/* 126 KB data RAM + 2 KB BT RAM size */
#	define CONFIG_DATA_RAM_SIZE    0x00020000

	/*
	 * Override default NPCX_RAM_SIZE because NPCX_PROGRAM_MEMORY_SIZE
	 * is not the actual size of code RAM.
	 */
#	undef NPCX_RAM_SIZE
#	define NPCX_RAM_SIZE (CONFIG_DATA_RAM_SIZE + CONFIG_CODE_RAM_SIZE)
#else
#	error "Unsupported chip variant"
#endif

#define CONFIG_RAM_SIZE         (CONFIG_DATA_RAM_SIZE - NPCX_BTRAM_SIZE)
/* no low power ram in npcx7 series */

#endif /* __CROS_EC_CONFIG_CHIP_NPCX7_H */
