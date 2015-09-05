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

/* Size of one firmware image in flash */
#ifndef CONFIG_FW_IMAGE_SIZE
#define CONFIG_FW_IMAGE_SIZE	(CONFIG_FLASH_PHYSICAL_SIZE / 2)
#endif

/* Header support which is used by booter to copy FW from flash to code ram */
#define NPCX_RO_HEADER
#define CONFIG_RO_HDR_MEM_OFF	0x0
#define CONFIG_RO_HDR_SIZE	0x40

#define CONFIG_WP_OFF		CONFIG_RO_STORAGE_OFF
#define CONFIG_WP_SIZE		CONFIG_FW_IMAGE_SIZE

/* RO firmware offset in flash */
#define CONFIG_RO_MEM_OFF	CONFIG_RO_HDR_SIZE
#define CONFIG_RO_SIZE		CONFIG_CDRAM_SIZE    /* 96KB for RO FW */
#define CONFIG_FLASH_SIZE	CONFIG_FLASH_PHYSICAL_SIZE

/* RW firmware offset in flash */
#define CONFIG_RW_MEM_OFF	CONFIG_RW_STORAGE_OFF
#define CONFIG_RW_SIZE		CONFIG_CDRAM_SIZE    /* 96KB for RW FW */

/* The storage offset of ec.R*.flat which is used for CONFIG_CDRAM_ARCH */
#define CONFIG_RO_STORAGE_OFF	0
#define CONFIG_RW_STORAGE_OFF	CONFIG_FW_IMAGE_SIZE /* 128 KB alignemnt */

#endif /* __CROS_EC_CONFIG_FLASH_LAYOUT_H */
