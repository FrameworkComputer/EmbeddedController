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
#else
#error "Unsupported chip variant"
#endif

/* Header support which is used by booter to copy FW from flash to code ram */
#define NPCX_RO_HEADER
#define CONFIG_RO_HDR_MEM_OFF	0x0
#define CONFIG_RO_HDR_SIZE	0x40

#define CONFIG_WP_STORAGE_OFF	CONFIG_EC_PROTECTED_STORAGE_OFF
#define CONFIG_WP_STORAGE_SIZE	CONFIG_EC_PROTECTED_STORAGE_SIZE

/* RO firmware offset in flash */
#define CONFIG_RO_MEM_OFF	0
#define CONFIG_RO_SIZE		NPCX_PROGRAM_MEMORY_SIZE

/* RW firmware offset in flash */
#define CONFIG_RW_MEM_OFF	CONFIG_RW_STORAGE_OFF
#define CONFIG_RW_SIZE		CONFIG_RO_SIZE

/* The storage offset of ec.R*.flat which is used for firmware_image.lds */
#define CONFIG_RO_STORAGE_OFF	CONFIG_RO_HDR_SIZE
#define CONFIG_RW_STORAGE_OFF	0

#endif /* __CROS_EC_CONFIG_FLASH_LAYOUT_H */
