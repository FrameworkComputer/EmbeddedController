/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * Memory mapping for STM32G473xc. The STM32G473xc is a category 1 device within
 * the STM32G4 chip family. Category 1 devices have either 128, 256, or 512 kB
 * of internal flash. 'xc' indicates 256 kB of internal flash.
 *
 * STM32G473xc can be configured via option bytes as either a single bank or
 * dual bank device. Dual bank is the default selection.
 * CONFIG_FLASH_BANK_SIZE is consistent with page size as defined in RM0440 TRM
 * for the STM32G4 chip family. In dual bank mode, the flash is organized in 2
 * kB pages, with 64 pages per bank for this variant.
 *
 * The minimum write size for STM32G4 is 8 bytes. Cros-EC does not support
 * PSTATE in single bank memories with a write size > 4 bytes.
 *
 * TODO(b/181874494): Verify that dual bank mode should be used, or add support
 * for enabling single bank mode on STM32G473xc.
 */
#define CONFIG_FLASH_SIZE_BYTES (256 * 1024)
#define CONFIG_FLASH_WRITE_SIZE 0x0004
#define CONFIG_FLASH_BANK_SIZE (2 * 1024)
#define CONFIG_FLASH_ERASE_SIZE CONFIG_FLASH_BANK_SIZE

/* Dual-bank (DBANK) mode is enabled by default for this chip */
#define STM32_FLASH_DBANK_MODE

/* Erasing 128K can take up to 2s, need to defer erase. */
#define CONFIG_FLASH_DEFERRED_ERASE

/* No page mode on STM32G4, so no benefit to larger write sizes */
#define CONFIG_FLASH_WRITE_IDEAL_SIZE CONFIG_FLASH_WRITE_SIZE

/*
 * STM32G473xc is a category 3 SRAM device featuring 128 Kbytes of embedded
 * SRAM. This SRAM is split into three blocks:
 * • 80 Kbytes mapped at address 0x2000 0000 (SRAM1).
 * • 16 Kbytes mapped at address 0x2001 4000 (SRAM2).
 * • 32 Kbytes mapped at address 0x1000 0000 (CCM SRAM). It is also aliased
 *   at 0x2001 8000 address to be accessed by all bus controllers.
 */
#define CONFIG_RAM_BASE		0x20000000
#define CONFIG_RAM_SIZE		0x00020000

#undef I2C_PORT_COUNT
#define I2C_PORT_COUNT	4

/* Number of DMA channels supported (6 channels each for DMA1 and DMA2) */
#define DMAC_COUNT 12

/* Use PSTATE embedded in the RO image, not in its own erase block */
#define CONFIG_FLASH_PSTATE
#undef CONFIG_FLASH_PSTATE_BANK

/* Number of IRQ vectors on the NVIC */
#define CONFIG_IRQ_COUNT	101

/* USB packet ram config */
#define CONFIG_USB_RAM_BASE        0x40006000
#define CONFIG_USB_RAM_SIZE        1024
#define CONFIG_USB_RAM_ACCESS_TYPE uint16_t
#define CONFIG_USB_RAM_ACCESS_SIZE 2

/* DFU Address */
#define STM32_DFU_BASE              0x1fff0000
