/* Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Memory mapping */
#define CONFIG_FLASH_SIZE 0x00100000	/* 1 MB */
#define CONFIG_FLASH_BANK_SIZE  0x800	/* 2 kB */
#define CONFIG_FLASH_ERASE_SIZE 0x800	/* 2 KB */
#define CONFIG_FLASH_WRITE_SIZE 0x8	/* 64 bits (without 8 bits ECC) */

/* Ideal write size in page-mode */
#define CONFIG_FLASH_WRITE_IDEAL_SIZE 0x100	/* 256 (32 double words) */

#define CONFIG_RAM_BASE         0x20000000
/* Only using SRAM1. SRAM2 (32 KB) is ignored. */
#define CONFIG_RAM_SIZE         0x00018000	/* 96 kB */

/* Number of IRQ vectors on the NVIC */
#define CONFIG_IRQ_COUNT 82
