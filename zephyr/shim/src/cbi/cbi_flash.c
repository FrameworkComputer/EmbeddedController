/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#define DT_DRV_COMPAT cros_ec_flash_layout

#include "console.h"
#include "cros_board_info.h"
#include "flash.h"
#include "system.h"
#include "write_protect.h"

#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(cbi_flash, LOG_LEVEL_ERR);

#define CBI_FLASH_NODE DT_NODELABEL(cbi_flash)
#define CBI_FLASH_OFFSET DT_PROP(CBI_FLASH_NODE, offset)
#define CBI_FLASH_PRESERVE DT_PROP(CBI_FLASH_NODE, preserve)

BUILD_ASSERT(DT_NODE_EXISTS(CBI_FLASH_NODE) == 1,
	     "CBI flash DT node label not found");
BUILD_ASSERT((CBI_FLASH_OFFSET % CONFIG_FLASH_ERASE_SIZE) == 0,
	     "CBI flash offset is not erase-size aligned");
BUILD_ASSERT((CBI_IMAGE_SIZE % CONFIG_FLASH_ERASE_SIZE) == 0,
	     "CBI flash size is not erase-size aligned");
BUILD_ASSERT(CBI_IMAGE_SIZE > 0, "CBI flash size must be greater than zero");

static int flash_load(uint8_t offset, uint8_t *data, int len)
{
	return crec_flash_read(CBI_FLASH_OFFSET, CBI_IMAGE_SIZE, (char *)data);
}

static int flash_is_write_protected(void)
{
	return system_is_locked();
}

static int flash_store(uint8_t *cbi)
{
	int rv;

	rv = crec_flash_physical_erase(CBI_FLASH_OFFSET, CBI_IMAGE_SIZE);
	if (rv) {
		LOG_ERR("CBI flash erase before write failed, rv: %d\n", rv);
		return rv;
	}
	return crec_flash_physical_write(CBI_FLASH_OFFSET, CBI_IMAGE_SIZE,
					 (const char *)cbi);
}

static const struct cbi_storage_driver flash_drv = {
	.store = flash_store,
	.load = flash_load,
	.is_protected = flash_is_write_protected,
};

const struct cbi_storage_config_t cbi_config = {
	.storage_type = CBI_STORAGE_TYPE_FLASH,
	.drv = &flash_drv,
};
