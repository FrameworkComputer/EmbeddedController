// Copyright 2025 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "hooks.h"
#include "system.h"

#include <errno.h>
#include <stdbool.h>

#include <zephyr/arch/riscv/pmp.h>
#include <zephyr/devicetree.h>
#include <zephyr/logging/log.h>
#include <zephyr/mem_mgmt/mem_attr.h>

LOG_MODULE_REGISTER(riscv_mpu, LOG_LEVEL_ERR);

static void prepare_for_sysjump_to_ec(void)
{
	/*
	 * Clear all Physical Memory Protection (PMP) entries before jumping to
	 * the new image. When CONFIG_HW_STACK_PROTECTION is enabled, the
	 * current image's PMP configuration for stack boundaries can interfere
	 * with the stack setup of the new image, leading to crashes. Clearing
	 * ensures the new image starts with a clean PMP state.
	 */
	z_riscv_pmp_clear_all();
}
DECLARE_HOOK(HOOK_SYSJUMP, prepare_for_sysjump_to_ec, HOOK_PRIO_LAST);

#if defined(CONFIG_PLATFORM_EC_ROLLBACK_MPU_PROTECT)

#define ROLLBACK_NODE DT_NODELABEL(rollback)

BUILD_ASSERT(DT_NODE_EXISTS(ROLLBACK_NODE),
	     "The 'rollback' node label is not defined in the Devicetree.");

test_mockable int mpu_lock_rollback(bool lock)
{
	const char *rollback_node_name = DT_NODE_FULL_NAME(ROLLBACK_NODE);
	int rollback_region_idx =
		mem_attr_get_region_index_by_name(rollback_node_name);

	if (rollback_region_idx < 0) {
		LOG_ERR("Rollback region index for '%s' not found.",
			rollback_node_name);
		return -ENOENT;
	}

	if (lock) {
		return z_riscv_pmp_change_permissions(rollback_region_idx, 0);
	}
	return z_riscv_pmp_change_permissions(rollback_region_idx,
					      PMP_R | PMP_W);
}
#endif
