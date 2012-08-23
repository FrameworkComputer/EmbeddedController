/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Memory mapping */
#define CONFIG_FLASH_BASE       0x08000000
#define CONFIG_FLASH_PHYSICAL_SIZE 0x00020000
#define CONFIG_FLASH_SIZE       CONFIG_FLASH_PHYSICAL_SIZE
#define CONFIG_FLASH_BANK_SIZE  0x1000
#define CONFIG_FLASH_ERASE_SIZE 0x0400  /* erase bank size */
#define CONFIG_FLASH_WRITE_SIZE 0x0002  /* minimum write size */
#define CONFIG_RAM_BASE         0x20000000
#define CONFIG_RAM_SIZE         0x00002000

/* Size of one firmware image in flash */
#define CONFIG_FW_IMAGE_SIZE    (64 * 1024)

#define CONFIG_FW_RO_OFF        0
#define CONFIG_FW_RO_SIZE       (CONFIG_FW_IMAGE_SIZE \
				 - CONFIG_SECTION_FLASH_PSTATE_SIZE)
#define CONFIG_FW_RW_OFF        CONFIG_FW_IMAGE_SIZE
#define CONFIG_FW_RW_SIZE       CONFIG_FW_IMAGE_SIZE

#define CONFIG_SECTION_RO_OFF   CONFIG_FW_RO_OFF
#define CONFIG_SECTION_RO_SIZE  CONFIG_FW_RO_SIZE
#define CONFIG_SECTION_RW_OFF   CONFIG_FW_RW_OFF
#define CONFIG_SECTION_RW_SIZE  CONFIG_FW_RW_SIZE
#define CONFIG_SECTION_WP_RO_OFF   CONFIG_FW_RO_OFF
#define CONFIG_SECTION_WP_RO_SIZE  CONFIG_FW_IMAGE_SIZE

/*
 * Put this after RO to give RW more space. This also makes RO write protect
 * region contiguous.
 */
#define CONFIG_SECTION_FLASH_PSTATE_SIZE  (1 * CONFIG_FLASH_BANK_SIZE)
#define CONFIG_SECTION_FLASH_PSTATE_OFF   CONFIG_FW_RO_OFF + CONFIG_FW_RO_SIZE

/* Number of IRQ vectors on the NVIC */
#define CONFIG_IRQ_COUNT 61
