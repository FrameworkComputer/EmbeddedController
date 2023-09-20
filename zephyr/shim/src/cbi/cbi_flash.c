/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "cbi_config.h"
#include "cbi_flash.h"
#include "console.h"
#include "cros_board_info.h"
#include "flash.h"
#include "system.h"
#include "write_protect.h"

#include <zephyr/devicetree.h>

LOG_MODULE_REGISTER(cbi_flash, LOG_LEVEL_ERR);

BUILD_ASSERT(DT_NODE_EXISTS(CBI_FLASH_NODE) == 1,
	     "CBI flash DT node label not found");
BUILD_ASSERT((CBI_FLASH_OFFSET % CONFIG_FLASH_ERASE_SIZE) == 0,
	     "CBI flash section offset is not erase-size aligned");
BUILD_ASSERT(CBI_IMAGE_SIZE > 0,
	     "CBI image size in EC flash must be greater than zero");
BUILD_ASSERT(CBI_IMAGE_SIZE >= 256,
	     "CBI image size in EC flash is less than 256 bytes");
BUILD_ASSERT(CBI_FLASH_SIZE >= CBI_IMAGE_SIZE,
	     "CBI flash section size is less than CBI image size");

static bool is_cbi_section(uint8_t offset, int len)
{
	if (len < 0) {
		return false;
	}
	if (offset + len > CBI_IMAGE_SIZE) {
		return false;
	}
	return true;
}

static int flash_load(uint8_t offset, uint8_t *data, int len)
{
	if (!is_cbi_section(offset, len)) {
		return EC_ERROR_INVAL;
	}
	return crec_flash_unprotected_read(CBI_FLASH_OFFSET + offset, len,
					   (char *)data);
}

static int flash_is_write_protected(void)
{
	return system_is_locked();
}

static int flash_store(uint8_t *cbi)
{
	int rv;

	rv = crec_flash_physical_erase(CBI_FLASH_OFFSET, CBI_FLASH_SIZE);
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

const struct cbi_storage_config_t flash_cbi_config = {
	.storage_type = CBI_STORAGE_TYPE_FLASH,
	.drv = &flash_drv,
};
