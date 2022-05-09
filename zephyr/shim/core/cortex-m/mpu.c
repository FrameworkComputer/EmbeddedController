/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "config.h"
#include "mpu.h"
#include "logging/log.h"
#include <zephyr/arch/arm/aarch32/cortex_m/cmsis.h>
#include <zephyr/arch/cpu.h>
#include <zephyr/init.h>

LOG_MODULE_REGISTER(shim_mpu, LOG_LEVEL_ERR);

void mpu_enable(void)
{
	for (int index = 0; index < mpu_config.num_regions; index++) {
		MPU->RNR = index;
		MPU->RASR |= MPU_RASR_ENABLE_Msk;
		LOG_DBG("[%d] %08x %08x", index, MPU->RBAR, MPU->RASR);
	}
}

static int mpu_disable_fixed_regions(const struct device *dev)
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
