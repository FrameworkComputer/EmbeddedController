/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "system.h"

#include <zephyr/devicetree.h>
#include <zephyr/drivers/flash.h>

#define FT9001_CONFIG_PAGE_ADDR DT_REG_SIZE(DT_NODELABEL(bootloader))
#define FT9001_CONFIG_PAGE_SIZE CONFIG_FLASH_ERASE_SIZE

#define flash_ctrl_dev DEVICE_DT_GET(DT_CHOSEN(zephyr_flash_controller))

void chip_enter_bootloader(uint8_t mode)
{
	/* Erase a configuration page to stay in bootloader after reboot. */
	int ret = flash_erase(flash_ctrl_dev, FT9001_CONFIG_PAGE_ADDR,
			      FT9001_CONFIG_PAGE_SIZE);

	if (ret) {
		printk("Failed to erase secure rom address\n");
	}
	system_reset(SYSTEM_RESET_HARD);
}
