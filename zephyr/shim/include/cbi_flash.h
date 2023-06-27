/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_CBI_FLASH_H
#define __CROS_EC_CBI_FLASH_H

#include "cros_board_info.h"

#include <zephyr/devicetree.h>

#define DT_DRV_COMPAT cros_ec_flash_layout

#define CBI_FLASH_NODE DT_NODELABEL(cbi_flash)
#define CBI_FLASH_OFFSET DT_PROP(CBI_FLASH_NODE, offset)
#define CBI_FLASH_PRESERVE DT_PROP(CBI_FLASH_NODE, preserve)

#if CONFIG_PLATFORM_EC_CBI_FLASH
BUILD_ASSERT(DT_NODE_EXISTS(CBI_FLASH_NODE) == 1,
	     "CBI flash DT node label not found");
BUILD_ASSERT((CBI_FLASH_OFFSET % CONFIG_FLASH_ERASE_SIZE) == 0,
	     "CBI flash offset is not erase-size aligned");
BUILD_ASSERT((CBI_IMAGE_SIZE % CONFIG_FLASH_ERASE_SIZE) == 0,
	     "CBI flash size is not erase-size aligned");
BUILD_ASSERT(CBI_IMAGE_SIZE > 0, "CBI flash size must be greater than zero");
#endif /* CONFIG_PLATFORM_EC_CBI_FLASH */
#endif /* __CROS_EC_CBI_FLASH_H */
