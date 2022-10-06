/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <zephyr/drivers/bbram.h>
#include <zephyr/logging/log.h>

#include "system.h"
#include "system_chip.h"

LOG_MODULE_REGISTER(shim_xec_system, LOG_LEVEL_ERR);

#define GET_BBRAM_OFS(node) DT_PROP(DT_PATH(named_bbram_regions, node), offset)
#define GET_BBRAM_SZ(node) DT_PROP(DT_PATH(named_bbram_regions, node), size)

/*
 * Reset image type back to RO in BBRAM as watchdog resets.
 * Watchdog reset will reset EC chip, ROM loader loads RO
 * image stored in SPI flash chip in default.
 */
void cros_chip_wdt_handler(const struct device *wdt_dev, int channel_id)
{
	const struct device *bbram_dev = DEVICE_DT_GET(DT_NODELABEL(bbram));
	uint32_t value = EC_IMAGE_RO;

	if (!device_is_ready(bbram_dev)) {
		LOG_ERR("WDT ISR: device %s is not ready", bbram_dev->name);
		return;
	}

	bbram_write(bbram_dev, GET_BBRAM_OFS(ec_img_load),
		    GET_BBRAM_SZ(ec_img_load), (uint8_t *)&value);
}

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
