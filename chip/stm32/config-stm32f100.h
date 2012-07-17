/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Memory mapping */
#define CONFIG_FLASH_BASE       0x08000000
#define CONFIG_FLASH_SIZE       0x00020000
#define CONFIG_FLASH_BANK_SIZE  0x1000
#define CONFIG_RAM_BASE         0x20000000
#define CONFIG_RAM_SIZE         0x00002000

/* Size of one firmware image in flash */
#define CONFIG_FW_IMAGE_SIZE    (64 * 1024)

#define CONFIG_FW_RO_OFF        0
#define CONFIG_FW_RO_SIZE       CONFIG_FW_IMAGE_SIZE
#define CONFIG_FW_RW_OFF        CONFIG_FW_IMAGE_SIZE
#define CONFIG_FW_RW_SIZE       CONFIG_FW_IMAGE_SIZE

#define CONFIG_SECTION_RO_OFF   CONFIG_FW_RO_OFF
#define CONFIG_SECTION_RO_SIZE  CONFIG_FW_RO_SIZE
#define CONFIG_SECTION_RW_OFF   CONFIG_FW_RW_OFF
#define CONFIG_SECTION_RW_SIZE  CONFIG_FW_RW_SIZE

/* no keys for now */
#define CONFIG_VBOOT_ROOTKEY_OFF    (CONFIG_FW_RO_OFF + CONFIG_FW_RO_SIZE)
#define CONFIG_VBLOCK_RW_OFF        (CONFIG_FW_RW_OFF + CONFIG_FW_RW_SIZE)
#define CONFIG_VBOOT_ROOTKEY_SIZE   0
#define CONFIG_VBLOCK_SIZE          0

/* Number of IRQ vectors on the NVIC */
#define CONFIG_IRQ_COUNT 61
