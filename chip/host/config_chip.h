/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Chip config header file */

#ifndef __CROS_EC_CONFIG_CHIP_H
#define __CROS_EC_CONFIG_CHIP_H

/* Memory mapping */
#define CONFIG_FLASH_PHYSICAL_SIZE 0x00020000
extern char __host_flash[CONFIG_FLASH_PHYSICAL_SIZE];

#define CONFIG_FLASH_BASE       ((uintptr_t)__host_flash)
#define CONFIG_FLASH_BANK_SIZE  0x1000
#define CONFIG_FLASH_ERASE_SIZE 0x0010  /* erase bank size */
#define CONFIG_FLASH_WRITE_SIZE 0x0002  /* minimum write size */
#define CONFIG_FLASH_WRITE_IDEAL_SIZE 0x0080  /* ideal write size */
#define CONFIG_RAM_BASE         0x0 /* Not supported */
#define CONFIG_RAM_SIZE         0x0 /* Not supported */

#define CONFIG_FPU

#include "config_std_internal_flash.h"

/* Maximum number of deferrable functions */
#define DEFERRABLE_MAX_COUNT 8

/* Interval between HOOK_TICK notifications */
#define HOOK_TICK_INTERVAL_MS 250
#define HOOK_TICK_INTERVAL    (HOOK_TICK_INTERVAL_MS * MSEC)

/* Do NOT use common panic code (designed to output information on the UART) */
#undef CONFIG_COMMON_PANIC_OUTPUT
/* Do NOT use common timer code which is designed for hardware counters. */
#undef CONFIG_COMMON_TIMER

#endif /* __CROS_EC_CONFIG_CHIP_H */
