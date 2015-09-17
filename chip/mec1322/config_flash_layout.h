/* Copyright 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_CONFIG_FLASH_LAYOUT_H
#define __CROS_EC_CONFIG_FLASH_LAYOUT_H

/*
 * mec1322 flash layout:
 * - Non memory-mapped, external SPI.
 * - RW image at the beginning of writable region.
 * - Bootloader at the beginning of protected region, followed by RO image.
 * - Loader + (RO | RW) loaded into program memory.
 */

/* Non-memmapped, external SPI */
#define CONFIG_EXTERNAL_STORAGE
#undef  CONFIG_MAPPED_STORAGE
#undef  CONFIG_FLASH_PSTATE
#define CONFIG_SPI_FLASH

/* EC region of SPI resides at end of ROM, protected region follows writable */
#define CONFIG_EC_PROTECTED_STORAGE_OFF  (CONFIG_FLASH_SIZE - 0x20000)
#define CONFIG_EC_PROTECTED_STORAGE_SIZE 0x20000
#define CONFIG_EC_WRITABLE_STORAGE_OFF   (CONFIG_FLASH_SIZE - 0x40000)
#define CONFIG_EC_WRITABLE_STORAGE_SIZE  0x20000

/* Loader resides at the beginning of program memory */
#define CONFIG_LOADER_MEM_OFF		0
#define CONFIG_LOADER_SIZE		0xC00

/* Write protect Loader and RO Image */
#define CONFIG_WP_STORAGE_OFF		CONFIG_EC_PROTECTED_STORAGE_OFF
/*
 * Write protect 128k section of 256k physical flash which contains loader
 * and RO Images.
 */
#define CONFIG_WP_STORAGE_SIZE		CONFIG_EC_PROTECTED_STORAGE_SIZE

/*
 * RO / RW images follow the loader in program memory. Either RO or RW
 * image will be loaded -- both cannot be loaded at the same time.
 */
#define CONFIG_RO_MEM_OFF		(CONFIG_LOADER_MEM_OFF + \
					CONFIG_LOADER_SIZE)
#define CONFIG_RO_SIZE			(97 * 1024)
#define CONFIG_RW_MEM_OFF		CONFIG_RO_MEM_OFF
#define CONFIG_RW_SIZE			CONFIG_RO_SIZE

/* WP region consists of second half of SPI, and begins with the boot header */
#define CONFIG_BOOT_HEADER_STORAGE_OFF	0
#define CONFIG_BOOT_HEADER_STORAGE_SIZE	0x240

/* Loader / lfw image immediately follows the boot header on SPI */
#define CONFIG_LOADER_STORAGE_OFF	(CONFIG_BOOT_HEADER_STORAGE_OFF + \
					CONFIG_BOOT_HEADER_STORAGE_SIZE)

/* RO image immediately follows the loader image */
#define CONFIG_RO_STORAGE_OFF		(CONFIG_LOADER_STORAGE_OFF + \
					CONFIG_LOADER_SIZE)

/* RW image starts at the beginning of SPI */
#define CONFIG_RW_STORAGE_OFF		0

#endif /* __CROS_EC_CONFIG_FLASH_LAYOUT_H */
