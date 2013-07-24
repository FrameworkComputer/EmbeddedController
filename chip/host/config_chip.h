/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Chip config header file */

#ifndef __CROS_EC_CONFIG_CHIP_H
#define __CROS_EC_CONFIG_CHIP_H

/* Memory mapping */
#define CONFIG_FLASH_PHYSICAL_SIZE 0x00020000
#define CONFIG_FLASH_SIZE       CONFIG_FLASH_PHYSICAL_SIZE
extern char __host_flash[CONFIG_FLASH_PHYSICAL_SIZE];

#define CONFIG_FLASH_BASE       ((uintptr_t)__host_flash)
#define CONFIG_FLASH_BANK_SIZE  0x1000
#define CONFIG_FLASH_ERASE_SIZE 0x0400  /* erase bank size */
#define CONFIG_FLASH_WRITE_SIZE 0x0002  /* minimum write size */
#define CONFIG_FLASH_WRITE_IDEAL_SIZE 0x0080  /* ideal write size */
#define CONFIG_RAM_BASE         0x0 /* Not supported */
#define CONFIG_RAM_SIZE         0x0 /* Not supported */

/* Size of one firmware image in flash */
#define CONFIG_FW_IMAGE_SIZE    (64 * 1024)

#define CONFIG_FW_RO_OFF        0
#define CONFIG_FW_RO_SIZE       (CONFIG_FW_IMAGE_SIZE - CONFIG_FW_PSTATE_SIZE)
#define CONFIG_FW_RW_OFF        CONFIG_FW_IMAGE_SIZE
#define CONFIG_FW_RW_SIZE       CONFIG_FW_IMAGE_SIZE
#define CONFIG_FW_WP_RO_OFF     CONFIG_FW_RO_OFF
#define CONFIG_FW_WP_RO_SIZE    CONFIG_FW_IMAGE_SIZE

/*
 * Put this after RO to give RW more space and make RO write protect region
 * contiguous.
 */
#define CONFIG_FW_PSTATE_OFF    CONFIG_FW_RO_SIZE
#define CONFIG_FW_PSTATE_SIZE   CONFIG_FLASH_BANK_SIZE

/* Maximum number of deferrable functions */
#define DEFERRABLE_MAX_COUNT 8

/* Interval between HOOK_TICK notifications */
#define HOOK_TICK_INTERVAL (250 * MSEC)

#endif /* __CROS_EC_CONFIG_CHIP_H */

