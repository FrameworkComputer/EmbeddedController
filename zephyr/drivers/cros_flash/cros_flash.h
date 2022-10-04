/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef ZEPHYR_DRIVERS_CROS_FLASH_CROS_FLASH_H_
#define ZEPHYR_DRIVERS_CROS_FLASH_CROS_FLASH_H_

#include <zephyr/device.h>

#define FLASH_SYSJUMP_TAG 0x5750 /* "WP" - Write Protect */

struct cros_flash_protection {
	bool control_access_blocked;
	bool protection_changes_blocked;
};

int flash_change_wp(const struct device *dev, uint32_t disable_mask,
		    uint32_t enable_mask);
int flash_get_wp(const struct device *dev, uint32_t *protected_mask);
int flash_change_rdp(const struct device *dev, bool enable, bool permanent);
int flash_get_rdp(const struct device *dev, bool *enable, bool *permanent);
int flash_block_protection_changes(const struct device *dev);
int flash_block_control_access(const struct device *dev);

int decode_wp_from_sysjump(struct cros_flash_protection *protection,
			   uint32_t prot_flags, const void *jump_data,
			   size_t size, int version);
void prepare_wp_jump(struct cros_flash_protection *protection);
#endif /* ZEPHYR_DRIVERS_CROS_FLASH_CROS_FLASH_H_ */
