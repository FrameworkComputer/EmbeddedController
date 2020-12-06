/* Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_STM32_FLASH_F_H
#define __CROS_EC_STM32_FLASH_F_H

#include <stdbool.h>

enum flash_rdp_level {
	FLASH_RDP_LEVEL_INVALID = -1,	/**< Error occurred. */
	FLASH_RDP_LEVEL_0,		/**< No read protection. */
	FLASH_RDP_LEVEL_1,		/**< Reading flash is disabled while in
					 *   bootloader mode or JTAG attached.
					 *   Changing to Level 0 from this level
					 *   triggers mass erase.
					 */
	FLASH_RDP_LEVEL_2,		/**< Same as Level 1, but is permanent
					 *   and can never be disabled.
					 */
};

bool is_flash_rdp_enabled(void);

#endif /* __CROS_EC_STM32_FLASH_F_H */
