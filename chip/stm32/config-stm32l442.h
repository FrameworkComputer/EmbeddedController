/* Copyright 2017 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Memory mapping */
#define CONFIG_FLASH_SIZE  0x00040000   /* 256 kB */
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
#define CONFIG_IRQ_COUNT 82
