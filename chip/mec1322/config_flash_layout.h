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
#define CONFIG_CODERAM_ARCH
#undef  CONFIG_FLASH_MAPPED
#undef  CONFIG_FLASH_PSTATE
#define CONFIG_SPI_FLASH

/* Size of SPI memory used by EC (lfw + RSA Keys + RO + RW + boot header) */
#define CONFIG_FLASH_BASE_SPI		(CONFIG_SPI_FLASH_SIZE - (0x40000))

/* Size of one firmware image in flash */
#ifndef CONFIG_FW_IMAGE_SIZE
#define CONFIG_FW_IMAGE_SIZE		(96 * 1024)
#endif
#define CONFIG_FLASH_SIZE		CONFIG_FLASH_PHYSICAL_SIZE

/* Loader resides at the beginning of program memory */
#define CONFIG_LOADER_MEM_OFF		0
#define CONFIG_LOADER_SIZE		0x1000

/* Write protect Loader and RO Image */
#define CONFIG_WP_OFF			(CONFIG_FLASH_PHYSICAL_SIZE >> 1)
/*
 * Write protect 128k section of 256k physical flash which contains loader
 * and RO Images.
 */
#define CONFIG_WP_SIZE			(CONFIG_FLASH_PHYSICAL_SIZE >> 1)

/*
 * RO / RW images follow the loader in program memory. Either RO or RW
 * image will be loaded -- both cannot be loaded at the same time.
 */
#define CONFIG_RO_MEM_OFF		(CONFIG_LOADER_MEM_OFF + \
					CONFIG_LOADER_SIZE)
#define CONFIG_RO_SIZE			CONFIG_FW_IMAGE_SIZE
#define CONFIG_RW_MEM_OFF		CONFIG_RO_MEM_OFF
#define CONFIG_RW_SIZE			CONFIG_RO_SIZE

/* WP region consists of second half of SPI, and begins with the boot header */
#define CONFIG_BOOT_HEADER_STORAGE_OFF	CONFIG_WP_OFF
#define CONFIG_BOOT_HEADER_STORAGE_SIZE	0x240

/* Loader / lfw image immediately follows the boot header on SPI */
#define CONFIG_LOADER_STORAGE_OFF	(CONFIG_BOOT_HEADER_STORAGE_OFF + \
					CONFIG_BOOT_HEADER_STORAGE_SIZE)

/* RO image immediately follows the loader image */
#define CONFIG_RO_STORAGE_OFF		(CONFIG_LOADER_STORAGE_OFF + \
					CONFIG_LOADER_SIZE)

/* RW image starts at the beginning of SPI */
#define CONFIG_RW_STORAGE_OFF		0

#define CONFIG_RO_IMAGE_FLASHADDR	(CONFIG_FLASH_BASE_SPI + \
					CONFIG_RO_STORAGE_OFF)
#define CONFIG_RW_IMAGE_FLASHADDR	(CONFIG_FLASH_BASE_SPI + \
					CONFIG_RW_STORAGE_OFF)

#endif /* __CROS_EC_CONFIG_FLASH_LAYOUT_H */
