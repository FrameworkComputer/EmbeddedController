/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Memory mapping */
#define CONFIG_FLASH_SIZE_BYTES  0x00040000   /* 256 kB */
#define CONFIG_FLASH_BANK_SIZE \
	0x800 /* 2 kB. NOTE: BANK in chrome-ec means page */
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

/*
 * STM32L431 flash layout:
 * - RO image starts at the beginning of flash: sector 0 ~ 61
 * - PSTATE immediately follows the RO image: sector 62
 * - RW image starts at 0x1f800: sector 63
 * - Protected region consists of the RO image + PSTATE: sector 0 ~ 62
 * - Unprotected region consists of second half of RW image
 *
 *                            PSTATE(2KB)
 *                              |
 *          (126KB)             v            (130KB)
 * |<-----Protected Region------>|<------Unprotected Region----->|
 * |<--------RO image--------->| |<----------RW image----------->|
 * 0        (124KB)            ^ ^
 *                             | |
 *                             | 63(2KB sector)
 *                             |
 *                             62
 *
 */



/* The EC uses one sector to emulate persistent state */
#define CONFIG_FLASH_PSTATE
#define CONFIG_FW_PSTATE_SIZE   CONFIG_FLASH_BANK_SIZE
#define CONFIG_FW_PSTATE_OFF    (62 * CONFIG_FLASH_BANK_SIZE)

#define CONFIG_RO_MEM_OFF       0
#define CONFIG_RO_STORAGE_OFF   0
#define CONFIG_RO_SIZE          (62 * CONFIG_FLASH_BANK_SIZE)
#define CONFIG_RW_MEM_OFF       (CONFIG_RO_MEM_OFF + CONFIG_RO_SIZE + \
				 CONFIG_FW_PSTATE_SIZE)
#define CONFIG_RW_STORAGE_OFF	0
#define CONFIG_RW_SIZE          (CONFIG_FLASH_SIZE_BYTES - CONFIG_RW_MEM_OFF - \
                                 CONFIG_RW_STORAGE_OFF)

#define CONFIG_EC_PROTECTED_STORAGE_OFF  0
#define CONFIG_EC_PROTECTED_STORAGE_SIZE CONFIG_RW_MEM_OFF
#define CONFIG_EC_WRITABLE_STORAGE_OFF   CONFIG_RW_MEM_OFF
#define CONFIG_EC_WRITABLE_STORAGE_SIZE  (CONFIG_FLASH_SIZE_BYTES - \
					  CONFIG_EC_WRITABLE_STORAGE_OFF)

#define CONFIG_WP_STORAGE_OFF   CONFIG_EC_PROTECTED_STORAGE_OFF
#define CONFIG_WP_STORAGE_SIZE  CONFIG_EC_PROTECTED_STORAGE_SIZE

/* We map each write protect sector to a bank */
#define PHYSICAL_BANKS          128
#define WP_BANK_COUNT           63
#define PSTATE_BANK             62
#define PSTATE_BANK_COUNT       1
