/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_CBI_FLASH_H
#define __CROS_EC_CBI_FLASH_H

#include "cros_board_info.h"

#include <zephyr/devicetree.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DT_DRV_COMPAT cros_ec_flash_layout

#define CBI_FLASH_NODE DT_NODELABEL(cbi_flash)
#define CBI_FLASH_OFFSET DT_PROP(CBI_FLASH_NODE, offset)
#define CBI_FLASH_PRESERVE DT_PROP(CBI_FLASH_NODE, preserve)

#ifdef __cplusplus
}
#endif

#endif /* __CROS_EC_CBI_FLASH_H */
