/* Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "config.h"
#include "mpu.h"

#include <zephyr/arch/cpu.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <cmsis_core.h>
#include <kernel_arch_data.h>

LOG_MODULE_REGISTER(shim_mpu, LOG_LEVEL_ERR);

void mpu_enable(void)
{
	for (int index = 0; index < mpu_config.num_regions; index++) {
		MPU->RNR = index;
		MPU->RASR |= MPU_RASR_ENABLE_Msk;
		LOG_DBG("[%d] %08x %08x", index, MPU->RBAR, MPU->RASR);
	}
}

static int mpu_disable_fixed_regions(void)
{
	/* MPU is configured and enabled by the Zephyr init code, disable the
	 * fixed sections by default.
	 */
	for (int index = 0; index < mpu_config.num_regions; index++) {
		MPU->RNR = index;
		MPU->RASR &= ~MPU_RASR_ENABLE_Msk;
		LOG_DBG("[%d] %08x %08x", index, MPU->RBAR, MPU->RASR);
	}

	return 0;
}

SYS_INIT(mpu_disable_fixed_regions, PRE_KERNEL_1, 50);

#ifdef CONFIG_PLATFORM_EC_ROLLBACK_MPU_PROTECT
/*
 * Defined in arch/arm/core/aarch32/mpu/arm_mpu.c.
 * This function is responsible for adding static regions and updating internal
 * structures (static region counter). This is important, because we want to use
 * MPU-based stack protection. On ARMv7-M background_area_start and
 * background_area_end are ignored.
 */
void arm_core_mpu_configure_static_mpu_regions(
	const struct z_arm_mpu_partition *static_regions,
	const uint8_t regions_num, const uint32_t background_area_start,
	const uint32_t background_area_end);

static int mpu_static_rollback_region_id = -1;

/*
 * When PRE_KERNEL_1 callbacks are executed (see z_cstart() at kernel/init.c)
 * we know that static MPU regions are installed - z_arm_mpu_init() and
 * z_arm_configure_static_mpu_regions() was called (see arch_kernel_init()),
 * but dynamic regions are not configured yet. This is good moment to install
 * user mpu regions.
 */
static int mpu_add_static_rollback_regions(void)
{
	/*
	 * We need to create two MPU regions for rollback because address of the
	 * region must be aligned with size. For example, if both rollback
	 * regions has 128kiB size and rollback0 starts at 128kiB we can't
	 * create one entry with 256kiB because start address is not aligned to
	 * it.
	 */
	const struct z_arm_mpu_partition rollback_regions[] = {
		{
			.start = CONFIG_FLASH_BASE_ADDRESS +
				 DT_REG_ADDR(DT_NODELABEL(rollback0)),
			.size = DT_REG_SIZE(DT_NODELABEL(rollback0)),
			.attr = K_MEM_PARTITION_P_NA_U_NA,
		},
		{
			.start = CONFIG_FLASH_BASE_ADDRESS +
				 DT_REG_ADDR(DT_NODELABEL(rollback1)),
			.size = DT_REG_SIZE(DT_NODELABEL(rollback1)),
			.attr = K_MEM_PARTITION_P_NA_U_NA,
		},
	};

	/*
	 * Background_area_start and background_area_end arguments are unused
	 * on ARMv7-M
	 */
	arm_core_mpu_configure_static_mpu_regions(
		rollback_regions, ARRAY_SIZE(rollback_regions), 0, 0);

	/*
	 * Find newly added regions by iterating over regions in MPU. Zephyr
	 * doesn't provide any convenient way to determine number of regions.
	 *
	 * We only need to check 7 regions instead of 8, because there are 2
	 * rollback regions. If the last MPU entry is used for the first
	 * rollback region the second rollback region is not protected.
	 */
	for (int index = 0; index < 7; index++) {
		MPU->RNR = index;
		if ((MPU->RBAR & MPU_RBAR_ADDR_Msk) ==
		    CONFIG_FLASH_BASE_ADDRESS +
			    DT_REG_ADDR(DT_NODELABEL(rollback0))) {
			mpu_static_rollback_region_id = index;
			LOG_DBG("Rollback MPU regions start at %d", index);
			return 0;
		}
	}

	/* It's an error if we can't find rollback MPU regions. */
	return -EINVAL;
}
SYS_INIT(mpu_add_static_rollback_regions, PRE_KERNEL_1, 50);

int mpu_lock_rollback(int lock)
{
	if (mpu_static_rollback_region_id < 0)
		return -EINVAL;

	for (int region = 0; region < 2; region++) {
		MPU->RNR = mpu_static_rollback_region_id + region;
		if (lock) {
			MPU->RASR |= MPU_RASR_ENABLE_Msk;
		} else {
			MPU->RASR &= ~MPU_RASR_ENABLE_Msk;
		}
	}

	return 0;
}
#endif
