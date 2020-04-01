/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * Memory mapping for STM32G431xb. The STM32G431xb is a category 2 device within
 * the STM32G4 chip family. Category 2 devices have either 32, 64, or 128 kB of
 * internal flash. The 'xB' indicates 128 kB of internal flash.
 */
#define CONFIG_FLASH_SIZE       (128 * 1024)
#define CONFIG_FLASH_WRITE_SIZE 0x0004
#define CONFIG_FLASH_ERASE_SIZE 0x0800
#define CONFIG_FLASH_BANK_SIZE CONFIG_FLASH_SIZE

/* Erasing 128K can take up to 2s, need to defer erase. */
#define CONFIG_FLASH_DEFERRED_ERASE

/* No page mode on STM32G4, so no benefit to larger write sizes */
#define CONFIG_FLASH_WRITE_IDEAL_SIZE CONFIG_FLASH_WRITE_SIZE

/*
 * STM32G431x6/x8/xB devices feature 32 Kbytes of embedded SRAM. This SRAM
 * is split into three blocks:
 * • 16 Kbytes mapped at address 0x2000 0000 (SRAM1).
 * •  6 Kbytes mapped at address 0x2000 4000 (SRAM2).
 * • 10 Kbytes mapped at address 0x1000 0000 (CCM SRAM). It is also aliased
 *   at 0x2000 5800 address to be accessed by all bus controllers.
 */
#define CONFIG_RAM_BASE		0x20000000
#define CONFIG_RAM_SIZE		0x00008000

#undef I2C_PORT_COUNT
#define I2C_PORT_COUNT	3

/* Number of DMA channels supported (6 channels each for DMA1 and DMA2) */
#define DMAC_COUNT 12

/* Use PSTATE embedded in the RO image, not in its own erase block */
#define CONFIG_FLASH_PSTATE
#undef CONFIG_FLASH_PSTATE_BANK

/* Number of IRQ vectors on the NVIC */
#define CONFIG_IRQ_COUNT	101
