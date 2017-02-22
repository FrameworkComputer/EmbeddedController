/* Copyright 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Memory mapping */
/*
 * Flash physical size: 256KB
 * Write protect sectors: 31 4KB sectors, one 132KB sector
 */
#define CONFIG_FLASH_SIZE 0x00040000
#define CONFIG_FLASH_BANK_SIZE  0x1000
#define CONFIG_FLASH_ERASE_SIZE 0x0800  /* erase bank size */
#define CONFIG_FLASH_WRITE_SIZE 0x0002  /* minimum write size */

/* No page mode on STM32F, so no benefit to larger write sizes */
#define CONFIG_FLASH_WRITE_IDEAL_SIZE 0x0002

#define CONFIG_RAM_BASE         0x20000000
#define CONFIG_RAM_SIZE         0x00008000

/* Number of IRQ vectors on the NVIC */
#define CONFIG_IRQ_COUNT 32

/*
 * STM32F09x flash layout:
 * - RO image starts at the beginning of flash: sector 0 ~ 29
 * - PSTATE immediately follows the RO image: sector 30
 * - RW image starts at 0x1f00: sector 31
 * - Protected region consists of the RO image + PSTATE: sector 0 ~ 30
 * - Unprotected region consists of second half of RW image
 *
 *                            PSTATE(4KB)
 *                              |
 *          (124KB)             v            (132KB)
 * |<-----Protected Region------>|<------Unprotected Region----->|
 * |<--------RO image--------->| |<----------RW image----------->|
 * 0        (120KB)            ^ ^
 *                             | |
 *                             | 31(132KB sector)
 *                             |
 *                             30
 *
 */

#define _SECTOR_4KB             (4 * 1024)
#define _SECTOR_132KB           (132 * 1024)

/* The EC uses one sector to emulate persistent state */
#define CONFIG_FLASH_PSTATE
#define CONFIG_FW_PSTATE_SIZE   _SECTOR_4KB
#define CONFIG_FW_PSTATE_OFF    (30 * _SECTOR_4KB)

#define CONFIG_RO_MEM_OFF       0
#define CONFIG_RO_STORAGE_OFF   0
#define CONFIG_RO_SIZE          (30 * _SECTOR_4KB)
#define CONFIG_RW_MEM_OFF       (CONFIG_RO_MEM_OFF + CONFIG_RO_SIZE + \
				 CONFIG_FW_PSTATE_SIZE)
#define CONFIG_RW_STORAGE_OFF	0
#define CONFIG_RW_SIZE          _SECTOR_132KB

#define CONFIG_EC_PROTECTED_STORAGE_OFF  0
#define CONFIG_EC_PROTECTED_STORAGE_SIZE CONFIG_RW_MEM_OFF
#define CONFIG_EC_WRITABLE_STORAGE_OFF   CONFIG_RW_MEM_OFF
#define CONFIG_EC_WRITABLE_STORAGE_SIZE  (CONFIG_FLASH_SIZE - \
					  CONFIG_EC_WRITABLE_STORAGE_OFF)

#define CONFIG_WP_STORAGE_OFF   CONFIG_EC_PROTECTED_STORAGE_OFF
#define CONFIG_WP_STORAGE_SIZE  CONFIG_EC_PROTECTED_STORAGE_SIZE

/* We map each write protect sector to a bank */
#define PHYSICAL_BANKS          32
#define WP_BANK_COUNT           31
#define PSTATE_BANK             30
#define PSTATE_BANK_COUNT       1

