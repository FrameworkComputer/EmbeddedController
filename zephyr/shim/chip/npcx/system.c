/* Copyright 2020 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "system.h"
#include "system_chip.h"

#include <zephyr/drivers/bbram.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(shim_npcx_system, LOG_LEVEL_ERR);

static void chip_bbram_status_check(void)
{
	const struct device *bbram_dev;
	int res;

	bbram_dev = DEVICE_DT_GET(DT_NODELABEL(bbram));
	if (!device_is_ready(bbram_dev)) {
		LOG_ERR("device %s not ready", bbram_dev->name);
		return;
	}

	res = bbram_check_invalid(bbram_dev);
	if (res != 0 && res != -ENOTSUP)
		LOG_INF("VBAT power drop!");

	res = bbram_check_standby_power(bbram_dev);
	if (res != 0 && res != -ENOTSUP)
		LOG_INF("VSBY power drop!");

	res = bbram_check_power(bbram_dev);
	if (res != 0 && res != -ENOTSUP)
		LOG_INF("VCC1 power drop!");
}

/*
 * Configure address 0x40001600 (Low Power RAM) in the the MPU
 * (Memory Protection Unit) as a "regular" memory
 */
void system_mpu_config(void)
{
	if (!IS_ENABLED(CONFIG_PLATFORM_EC_WORKAROUND_FLASH_DOWNLOAD_API))
		return;

	/*
	 * npcx9 Rev.1 has the problem for download_from_flash API.
	 * Workaround it by implementing the system_download_from_flash function
	 * in the suspend RAM. The functions will do the same, but will provide
	 * a software solution similar to what's done in the npcx5.
	 */
	/* Enable MPU */
	CPU_MPU_CTRL = 0x7;

	/* Create a new MPU Region to allow execution from low-power ram */
	CPU_MPU_RNR = REGION_CHIP_RESERVED;
	CPU_MPU_RASR = CPU_MPU_RASR & 0xFFFFFFFE; /* Disable region */
	CPU_MPU_RBAR = CONFIG_LPRAM_BASE; /* Set region base address */
	/*
	 * Set region size & attribute and enable region
	 * [31:29] - Reserved.
	 * [28]    - XN (Execute Never) = 0
	 * [27]    - Reserved.
	 * [26:24] - AP                 = 011 (Full access)
	 * [23:22] - Reserved.
	 * [21:19,18,17,16] - TEX,S,C,B = 001000 (Normal memory)
	 * [15:8]  - SRD                = 0 (Subregions enabled)
	 * [7:6]   - Reserved.
	 * [5:1]   - SIZE               = 01001 (1K)
	 * [0]     - ENABLE             = 1 (enabled)
	 */
	CPU_MPU_RASR = 0x03080013;
}

static int chip_system_init(void)
{
	/*
	 * Check BBRAM power status.
	 */
	chip_bbram_status_check();

	system_mpu_config();

	return 0;
}
/*
 * The priority should be lower than CROS_BBRAM_NPCX_INIT_PRIORITY.
 */
#if (CONFIG_CROS_SYSTEM_NPCX_PRE_INIT_PRIORITY <= CONFIG_BBRAM_INIT_PRIORITY)
#error CONFIG_CROS_SYSTEM_NPCX_PRE_INIT_PRIORITY must greater than \
	CONFIG_BBRAM_INIT_PRIORITY
#endif
SYS_INIT(chip_system_init, PRE_KERNEL_1,
	 CONFIG_CROS_SYSTEM_NPCX_PRE_INIT_PRIORITY);
