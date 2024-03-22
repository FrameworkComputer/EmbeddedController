/* Copyright 2020 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_CONFIG_CHIP_NPCX9_H
#define __CROS_EC_CONFIG_CHIP_NPCX9_H

/*
 * NPCX9 Series Device-Specific Information
 * Ex. NPCX9(M)(N)(G/K/F)(B/C)
 * @param M: 9: 144-pins package
 * @param N: 3: 320KB RAM Size, 6: 256KB RAM Size.
 * @param F: Google EC.
 * @param B/C: (Optional) Chip generation in the same series.
 */

/* Chip ID for all variants */
#define NPCX996F_CHIP_ID 0x21
#define NPCX993F_CHIP_ID 0x25
#define NPCX99FP_CHIP_ID 0x2b

/*****************************************************************************/
/* Hardware features */

#define NPCX_EXT32K_OSC_SUPPORT /* External 32KHz crytal osc. input support */
#define NPCX_INT_FLASH_SUPPORT /* Internal flash support */
#define NPCX_LCT_SUPPORT /* Long Countdown Timer support */
#define NPCX_PSL_MODE_SUPPORT /* Power switch logic mode for ultra-low power \
			       */

#define NPCX_UART_FIFO_SUPPORT
/* Number of UART modules. */
#define NPCX_SECOND_UART
#define UART_MODULE_COUNT 4

/*
 * For NPCX9, PS2_2 & PS2_3 pins also support other alternate functions
 * (e.g., ADC5, ADC6, TA2). PS2_2 & PS2_3 should be Explicit defined.
 */
#undef NPCX_PS2_MODULE_2
#undef NPCX_PS2_MODULE_3

/*
 * Number of I2C controllers. Controller 5/6 has 2 ports, so the chip has
 * two additional ports.
 */
#define CONFIG_I2C_MULTI_PORT_CONTROLLER
/* Number of I2C controllers */
#define I2C_CONTROLLER_COUNT 8
#define I2C_PORT_COUNT 10

#define NPCX_I2C_FIFO_SUPPORT

/* Use SHI module version 2 supported by npcx7 and latter family */
#define NPCX_SHI_V2

/* PSL_OUT optional configuration */
/* Set PSL_OUT mode to pulse mode */
#define NPCX_PSL_CFG_PSL_OUT_PULSE BIT(0)
/* set PSL_OUT to open-drain */
#define NPCX_PSL_CFG_PSL_OUT_OD BIT(1)
#define CONFIG_HIBERNATE_PSL_OUT_FLAGS 0

/*
 * Workaound the issue 3.10 in the NPCX99nF errata rev1.2
 * Enabling an eSPI channel (e.g. Peripheral Channel, Virtual Wire Channel, etc.
 * ) during an eSPI transaction might (with low probability) cause the eSPI_SIF
 * module to transition to a wrong state and therefore response with FATAL_ERROR
 * on an incoming transaction.
 */
#define NPCX_ESPI_BYPASS_CH_ENABLE_FATAL_ERROR

/*****************************************************************************/
/* Memory mapping */

#define NPCX_RAM_SIZE (CONFIG_DATA_RAM_SIZE + NPCX_PROGRAM_MEMORY_SIZE)

#if defined(CHIP_VARIANT_NPCX9M3F)
/*
 * 256KB program RAM, but only 512K of Flash. After the boot header is
 * added, a 256K image would be too large to fit in either RO or RW
 * sections of Flash (each of which is half of it). Because other code
 * assumes that image size is a multiple of Flash erase granularity, we
 * sacrifice a whole sector.
 */
#define NPCX_PROGRAM_MEMORY_SIZE (256 * 1024 - 0x1000)
/* program memory base address for Code RAM (0x100C0000 - 256KB) */
#define CONFIG_PROGRAM_MEMORY_BASE 0x10080000
#define CONFIG_RAM_BASE 0x200C0000 /* memory address of data ram */
/* Two blocks of data RAM - total size is 64KB */
#define CONFIG_DATA_RAM_SIZE 0x00010000
#define CONFIG_RAM_SIZE CONFIG_DATA_RAM_SIZE

/* Override default NPCX_RAM_SIZE because we're excluding a block. */
#undef NPCX_RAM_SIZE
#define NPCX_RAM_SIZE (CONFIG_DATA_RAM_SIZE + NPCX_PROGRAM_MEMORY_SIZE + 0x1000)
#elif defined(CHIP_VARIANT_NPCX9M6F)
/* 192KB RAM for FW code */
#define NPCX_PROGRAM_MEMORY_SIZE (192 * 1024)
/* program memory base address for Code RAM (0x100C0000 - 192KB) */
#define CONFIG_PROGRAM_MEMORY_BASE 0x10090000
#define CONFIG_RAM_BASE 0x200C0000 /* memory address of data ram */
/* Two blocks of data RAM - total size is 64KB */
#define CONFIG_DATA_RAM_SIZE 0x00010000
#define CONFIG_RAM_SIZE CONFIG_DATA_RAM_SIZE
#elif defined(CHIP_VARIANT_NPCX9MFP)
/*
 * 416KB program RAM, 1MB of Flash.
 */
#define NPCX_PROGRAM_MEMORY_SIZE (416 * 1024)
/* program memory base address for Code RAM (0x100C0000 - 416KB) */
#define CONFIG_PROGRAM_MEMORY_BASE 0x10058000
#define CONFIG_RAM_BASE 0x200C0000 /* memory address of data ram */
/*
 * Three blocks of data RAM - reserve 4KB for ROM utilities and
 *data ram size is 92KB
 */
#define CONFIG_DATA_RAM_SIZE (96 * 1024 - 0x1000)
#define CONFIG_RAM_SIZE CONFIG_DATA_RAM_SIZE

/* Override default NPCX_RAM_SIZE because we're excluding a block. */
#undef NPCX_RAM_SIZE
#define NPCX_RAM_SIZE (CONFIG_DATA_RAM_SIZE + NPCX_PROGRAM_MEMORY_SIZE + 0x1000)
#else
#error "Unsupported chip variant"
#endif

/* Internal spi-flash setting */
#define CONFIG_SPI_FLASH_REGS
#if defined(CHIP_VARIANT_NPCX9MFP)
#define CONFIG_SPI_FLASH_W25Q80 /* Internal spi flash type */
#define CONFIG_FLASH_SIZE_BYTES 0x00100000 /* 1 MB internal spi flash */
#else
#define CONFIG_SPI_FLASH_W25Q40 /* Internal spi flash type */
#define CONFIG_FLASH_SIZE_BYTES 0x00080000 /* 512 KB internal spi flash */
#endif

/* All NPCX9 variants support SHA256 accelerator. */
#define CONFIG_SHA256_HW_ACCELERATE

#endif /* __CROS_EC_CONFIG_CHIP_NPCX9_H */
