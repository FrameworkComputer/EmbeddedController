/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "cros_flash.h"
#include "system.h"

#include <zephyr/drivers/flash/stm32_flash_api_extensions.h>

int flash_change_wp(const struct device *dev, uint32_t disable_mask,
		    uint32_t enable_mask)
{
	int err;
	struct flash_stm32_ex_op_sector_wp_in wp_request = {
		.enable_mask = enable_mask,
		.disable_mask = disable_mask,
	};

	err = flash_ex_op(dev, FLASH_STM32_EX_OP_SECTOR_WP,
			  (uintptr_t)&wp_request, NULL);

	return err;
}

int flash_get_wp(const struct device *dev, uint32_t *protected_mask)
{
	int err;
	struct flash_stm32_ex_op_sector_wp_out wp_status;

	if (!protected_mask)
		return -EINVAL;

	err = flash_ex_op(dev, FLASH_STM32_EX_OP_SECTOR_WP, (uintptr_t)NULL,
			  &wp_status);
	*protected_mask = wp_status.protected_mask;

	return err;
}

int flash_change_rdp(const struct device *dev, bool enable, bool permanent)
{
	int err;
	struct flash_stm32_ex_op_rdp rdp_request = {
		.enable = enable,
		.permanent = permanent,
	};

	err = flash_ex_op(dev, FLASH_STM32_EX_OP_RDP, (uintptr_t)&rdp_request,
			  NULL);

	return err;
}

int flash_get_rdp(const struct device *dev, bool *enable, bool *permanent)
{
	int err;
	struct flash_stm32_ex_op_rdp rdp_status;

	err = flash_ex_op(dev, FLASH_STM32_EX_OP_RDP, (uintptr_t)NULL,
			  &rdp_status);

	if (enable)
		*enable = rdp_status.enable;

	if (permanent)
		*permanent = rdp_status.permanent;

	return err;
}

int flash_block_protection_changes(const struct device *dev)
{
	return flash_ex_op(dev, FLASH_STM32_EX_OP_BLOCK_OPTION_REG,
			   (uintptr_t)NULL, NULL);
}

int flash_block_control_access(const struct device *dev)
{
	return flash_ex_op(dev, FLASH_STM32_EX_OP_BLOCK_CONTROL_REG,
			   (uintptr_t)NULL, NULL);
}

#ifdef CONFIG_CROS_FLASH_STM32_EC_JUMP_STRUCTURE

#define CROS_FLASH_STM32_PROT_VERSION 1
#ifdef CONFIG_SOC_SERIES_STM32F4X
struct jump_wp_state {
	int entire_flash_locked;
};

int decode_wp_from_sysjump(struct cros_flash_protection *protection,
			   uint32_t prot_flags, const void *jump_data,
			   size_t size, int version)
{
	const struct jump_wp_state *wp =
		(const struct jump_wp_state *)jump_data;

	if (!protection || !wp || size != sizeof(*wp) ||
	    version != CROS_FLASH_STM32_PROT_VERSION)
		return -EINVAL;

	if (wp->entire_flash_locked) {
		protection->control_access_blocked = true;
		protection->protection_changes_blocked = true;
	} else if (prot_flags & EC_FLASH_PROTECT_RO_NOW) {
		/*
		 * If RO_NOW flag is set we know that RO image disabled option
		 * bytes.
		 */
		protection->protection_changes_blocked = true;
	}

	return EC_SUCCESS;
}

void prepare_wp_jump(struct cros_flash_protection *protection)
{
	struct jump_wp_state wp_state;

	if (!protection)
		return;

	wp_state.entire_flash_locked = 0;

	if (protection->control_access_blocked)
		wp_state.entire_flash_locked = 1;

	system_add_jump_tag(FLASH_SYSJUMP_TAG, CROS_FLASH_STM32_PROT_VERSION,
			    sizeof(wp_state), &wp_state);
}
#endif /* CONFIG_SOC_SERIES_STM32F4X */
#endif /* CONFIG_CROS_FLASH_STM32_EC_JUMP_STRUCTURE */
