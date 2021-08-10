/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Memory mapping */
#define CONFIG_FLASH_SIZE_BYTES  0x00080000   /* 512 kB */
#define CONFIG_FLASH_BANK_SIZE  0x800   /* 2 kB */
#define CONFIG_FLASH_ERASE_SIZE 0x800   /* 2 KB */
#define CONFIG_FLASH_WRITE_SIZE 0x8     /* 64 bits */

/* Ideal write size in page-mode */
#define CONFIG_FLASH_WRITE_IDEAL_SIZE 0x100     /* 256 (32 double words) */

/*
 * SRAM1 (48kB) at 0x20000000
 * SRAM2 (16kB) at 0x10000000 (and aliased at 0x2000C000)
 * so they are contiguous.
 */
#define CONFIG_RAM_BASE         0x20000000
#define CONFIG_RAM_SIZE         0x00010000      /* 64 kB */

/* Number of IRQ vectors on the NVIC */
#define CONFIG_IRQ_COUNT 109

/* USB packet ram config */
#define CONFIG_USB_RAM_BASE        0x4000D800
#define CONFIG_USB_RAM_SIZE        1024
#define CONFIG_USB_RAM_ACCESS_TYPE uint32_t
#define CONFIG_USB_RAM_ACCESS_SIZE 4

#undef I2C_PORT_COUNT
#define I2C_PORT_COUNT	4

/* Number of DMA channels supported (8 channels each for DMA1 and DMA2) */
#define DMAC_COUNT 16
