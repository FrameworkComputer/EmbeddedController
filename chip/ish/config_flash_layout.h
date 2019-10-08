/* Copyright 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_CONFIG_FLASH_LAYOUT_H
#define __CROS_EC_CONFIG_FLASH_LAYOUT_H

/* Mem-mapped, No external SPI for ISH */
#undef CONFIG_EXTERNAL_STORAGE
#define  CONFIG_MAPPED_STORAGE
#undef  CONFIG_FLASH_PSTATE
#undef CONFIG_SPI_FLASH

#ifdef CHIP_VARIANT_ISH5P4
#define CONFIG_ISH_BOOT_START		0xFF200000
#else
#define CONFIG_ISH_BOOT_START		0xFF000000
#endif

/*****************************************************************************/
/* The following macros are not applicable for ISH, however the build fails if
 * they are not defined. Ideally, there should be an option in EC build to
 * turn off SPI and flash, making these unnecessary.
 */

#define CONFIG_MAPPED_STORAGE_BASE	0x0

#define CONFIG_EC_PROTECTED_STORAGE_OFF  (CONFIG_FLASH_SIZE - 0x20000)
#define CONFIG_EC_PROTECTED_STORAGE_SIZE 0x20000
#define CONFIG_EC_WRITABLE_STORAGE_OFF   (CONFIG_FLASH_SIZE - 0x40000)
#define CONFIG_EC_WRITABLE_STORAGE_SIZE  0x20000

/* Unused for ISH - loader is external to ISH FW */
#define CONFIG_LOADER_MEM_OFF		0
#define CONFIG_LOADER_SIZE		0xC00


/* RO/RW images - not relevant for ISH
 */
#define CONFIG_RO_MEM_OFF		(CONFIG_LOADER_MEM_OFF + \
					CONFIG_LOADER_SIZE)
#define CONFIG_RO_SIZE			(97 * 1024)
#define CONFIG_RW_MEM_OFF		CONFIG_RO_MEM_OFF
#define CONFIG_RW_SIZE			CONFIG_RO_SIZE

/*****************************************************************************/

/* Not relevant for ISH */
#define CONFIG_BOOT_HEADER_STORAGE_OFF	0
#define CONFIG_BOOT_HEADER_STORAGE_SIZE	0x240

#define CONFIG_LOADER_STORAGE_OFF	(CONFIG_BOOT_HEADER_STORAGE_OFF + \
					CONFIG_BOOT_HEADER_STORAGE_SIZE)

/* RO image immediately follows the loader image */
#define CONFIG_RO_STORAGE_OFF		(CONFIG_LOADER_STORAGE_OFF + \
					CONFIG_LOADER_SIZE)

/* RW image starts at the beginning of SPI */
#define CONFIG_RW_STORAGE_OFF		0

#endif /* __CROS_EC_CONFIG_FLASH_LAYOUT_H */
