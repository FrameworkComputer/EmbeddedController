/* Copyright 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_CONFIG_FLASH_LAYOUT_H
#define __CROS_EC_CONFIG_FLASH_LAYOUT_H

/*
 * npcx flash layout:
 * - Memory-mapped external SPI.
 * - Image header at the beginning of protected region, followed by RO image.
 * - RW image starts at the second half of flash.
 */

/* Memmapped, external SPI */
#define CONFIG_EXTERNAL_STORAGE
#define CONFIG_MAPPED_STORAGE
/* Storage is memory-mapped, but program runs from SRAM */
#define CONFIG_MAPPED_STORAGE_BASE 0x64000000
#undef  CONFIG_FLASH_PSTATE

#if defined(CHIP_VARIANT_NPCX5M5G)
#define CONFIG_EC_PROTECTED_STORAGE_OFF  0
#define CONFIG_EC_PROTECTED_STORAGE_SIZE 0x20000
#define CONFIG_EC_WRITABLE_STORAGE_OFF   0x20000
#define CONFIG_EC_WRITABLE_STORAGE_SIZE  0x20000
#elif defined(CHIP_VARIANT_NPCX5M6G)
#define CONFIG_EC_PROTECTED_STORAGE_OFF  0
#define CONFIG_EC_PROTECTED_STORAGE_SIZE 0x40000
#define CONFIG_EC_WRITABLE_STORAGE_OFF   0x40000
#define CONFIG_EC_WRITABLE_STORAGE_SIZE  0x40000
#elif defined(CHIP_VARIANT_NPCX7M6F) || defined(CHIP_VARIANT_NPCX7M6FB) || \
	defined(CHIP_VARIANT_NPCX7M6FC) || defined(CHIP_VARIANT_NPCX7M6G) || \
	defined(CHIP_VARIANT_NPCX7M7FC) || defined(CHIP_VARIANT_NPCX7M7WC)
#define CONFIG_EC_PROTECTED_STORAGE_OFF  0
#define CONFIG_EC_PROTECTED_STORAGE_SIZE 0x40000
#define CONFIG_EC_WRITABLE_STORAGE_OFF   0x40000
#define CONFIG_EC_WRITABLE_STORAGE_SIZE  0x40000
#elif defined(CHIP_VARIANT_NPCX7M7WB)
#define CONFIG_EC_PROTECTED_STORAGE_OFF  0
#define CONFIG_EC_PROTECTED_STORAGE_SIZE 0x80000
#define CONFIG_EC_WRITABLE_STORAGE_OFF   0x80000
#define CONFIG_EC_WRITABLE_STORAGE_SIZE  0x80000
#elif defined(CHIP_VARIANT_NPCX9M3F) || defined(CHIP_VARIANT_NPCX9M6F)
#define CONFIG_EC_PROTECTED_STORAGE_OFF  0
#define CONFIG_EC_PROTECTED_STORAGE_SIZE 0x40000
#define CONFIG_EC_WRITABLE_STORAGE_OFF   0x40000
#define CONFIG_EC_WRITABLE_STORAGE_SIZE  0x40000
#else
#error "Unsupported chip variant"
#endif

/* Header support which is used by booter to copy FW from flash to code ram */
#define NPCX_RO_HEADER
#define CONFIG_RO_HDR_MEM_OFF	0x0
#define CONFIG_RO_HDR_SIZE	0x40

#define CONFIG_WP_STORAGE_OFF	CONFIG_EC_PROTECTED_STORAGE_OFF
#define CONFIG_WP_STORAGE_SIZE	CONFIG_EC_PROTECTED_STORAGE_SIZE

/* RO firmware in program memory - use all of program memory */
#define CONFIG_RO_MEM_OFF	0
#define CONFIG_RO_SIZE		NPCX_PROGRAM_MEMORY_SIZE

/*
 * ROM resident area in flash used to store data objects that are not copied
 * into code RAM. Enable using the CONFIG_CHIP_INIT_ROM_REGION option.
 */
#define CONFIG_RO_ROM_RESIDENT_MEM_OFF	CONFIG_RO_SIZE
#define CONFIG_RO_ROM_RESIDENT_SIZE \
	(CONFIG_EC_PROTECTED_STORAGE_SIZE - CONFIG_RO_SIZE)

/*
 * RW firmware in program memory - Identical to RO, only one image loaded at
 * a time.
 */
#define CONFIG_RW_MEM_OFF	CONFIG_RO_MEM_OFF
#define CONFIG_RW_SIZE		CONFIG_RO_SIZE

#define CONFIG_RW_ROM_RESIDENT_MEM_OFF	CONFIG_RW_SIZE
#define CONFIG_RW_ROM_RESIDENT_SIZE \
	(CONFIG_EC_WRITABLE_STORAGE_SIZE - CONFIG_RW_SIZE)

#if (CONFIG_RO_SIZE != CONFIG_RW_SIZE)
#error "Unsupported.. FLASH_ERASE_SIZE assumes RO and RW size is same!"
#endif

#if (CONFIG_RO_MEM_OFF != 0)
#error "Unsupported.. CONFIG_RO_MEM_OFF is assumed to be 0!"
#endif

/*
 * CONFIG_FLASH_ERASE_SIZE is set to maximum possible out of 64k, 32k and 4k
 * depending upon alignment of CONFIG_RO_SIZE. There are two assumptions here:
 * 1. CONFIG_RO_MEM_OFF is always 0 i.e. RO starts at 0.
 * 2. CONFIG_RO_SIZE and CONFIG_RW_SIZE are the same.
 *
 * If above assumptions are not true, then additional checks would be required
 * to ensure that erase block size is selected based on the alignment of both
 * CONFIG_RO_SIZE and CONFIG_RW_SIZE and the offset of RO.
 */
#if ((CONFIG_RO_SIZE & (0x10000 - 1)) == 0)
#define CONFIG_FLASH_ERASE_SIZE	0x10000
#define NPCX_ERASE_COMMAND		CMD_BLOCK_64K_ERASE
#elif ((CONFIG_RO_SIZE & (0x8000 - 1)) == 0)
#define CONFIG_FLASH_ERASE_SIZE	0x8000
#define NPCX_ERASE_COMMAND		CMD_BLOCK_32K_ERASE
#else
#define CONFIG_FLASH_ERASE_SIZE	0x1000
#define NPCX_ERASE_COMMAND		CMD_SECTOR_ERASE
#endif

#define CONFIG_FLASH_BANK_SIZE		CONFIG_FLASH_ERASE_SIZE
#define CONFIG_FLASH_WRITE_SIZE		0x1  /* minimum write size */
#define CONFIG_FLASH_WRITE_IDEAL_SIZE	256   /* one page size for write */

/* Use 4k sector erase for NPCX monitor flash erase operations. */
#define NPCX_MONITOR_FLASH_ERASE_SIZE	0x1000

/* RO image resides at start of protected region, right after header */
#define CONFIG_RO_STORAGE_OFF	CONFIG_RO_HDR_SIZE
/* RW image resides at start of writable region */
#define CONFIG_RW_STORAGE_OFF	0

#endif /* __CROS_EC_CONFIG_FLASH_LAYOUT_H */
