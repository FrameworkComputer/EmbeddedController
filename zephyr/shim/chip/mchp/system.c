/* Copyright 2022 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <zephyr/drivers/bbram.h>
#include <zephyr/logging/log.h>

#include "system.h"
#include "system_chip.h"

LOG_MODULE_REGISTER(shim_xec_system, LOG_LEVEL_ERR);

static void chip_bbram_status_check(void)
{
	const struct device *bbram_dev;
	int res;

	bbram_dev = DEVICE_DT_GET(DT_NODELABEL(bbram));
	if (!device_is_ready(bbram_dev)) {
		LOG_ERR("Error: device %s is not ready", bbram_dev->name);
		return;
	}

	res = bbram_check_invalid(bbram_dev);
	if (res != 0 && res != -ENOTSUP)
		LOG_INF("VBAT power drop!");
}

void system_mpu_config(void)
{
	/* Reseve for future use */
}

static int chip_system_init(const struct device *unused)
{
	ARG_UNUSED(unused);

	/*
	 * Check BBRAM power status.
	 */
	chip_bbram_status_check();

	system_mpu_config();

	return 0;
}
/*
 * The priority should be lower than CROS_BBRAM_MCHP_INIT_PRIORITY.
 */
#if (CONFIG_CROS_SYSTEM_XEC_PRE_INIT_PRIORITY <= CONFIG_BBRAM_INIT_PRIORITY)
#error CONFIG_CROS_SYSTEM_XEC_PRE_INIT_PRIORITY must greater than \
	CONFIG_BBRAM_INIT_PRIORITY
#endif
SYS_INIT(chip_system_init, PRE_KERNEL_1,
	 CONFIG_CROS_SYSTEM_XEC_PRE_INIT_PRIORITY);
