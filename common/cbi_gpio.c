/* Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Support Cros Board Info GPIO */

#include "cbi_config.h"
#include "console.h"
#include "cros_board_info.h"
#include "gpio.h"
#include "system.h"
#include "util.h"

#define CPRINTS(format, args...) cprints(CC_SYSTEM, "CBI " format, ##args)

static int cbi_gpio_read(uint8_t offset, uint8_t *data, int len)
{
	int board_id;
	int sku_id;
	int rv;
	int err = 0;

	if (cbi_get_cache_status() == CBI_CACHE_STATUS_SYNCED)
		return EC_SUCCESS;

	cbi_create();

	board_id = system_get_board_version();
	if (board_id < 0) {
		CPRINTS("Failed (%d) to get a valid board id", -board_id);
		err++;
	} else {
		rv = cbi_set_board_info(CBI_TAG_BOARD_VERSION,
					(uint8_t *)&board_id, sizeof(int));
		if (rv) {
			CPRINTS("Failed (%d) to set BOARD_VERSION tag", rv);
			err++;
		}
	}

	sku_id = system_get_sku_id();
	rv = cbi_set_board_info(CBI_TAG_SKU_ID, (uint8_t *)&sku_id,
				sizeof(int));
	if (rv) {
		CPRINTS("Failed (%d) to set SKU_ID tag", rv);
		err++;
	}

	if (err > 0)
		return EC_ERROR_UNKNOWN;

	return EC_SUCCESS;
}

static int cbi_gpio_is_write_protected(void)
{
	/*
	 * When CBI comes from strapping pins, any attempts for updating CBI
	 * storage should be rejected.
	 */
	return 1;
}

const struct cbi_storage_driver gpio_drv = {
	.load = cbi_gpio_read,
	.is_protected = cbi_gpio_is_write_protected,
};

const struct cbi_storage_config_t gpio_cbi_config = {
	.storage_type = CBI_STORAGE_TYPE_GPIO,
	.drv = &gpio_drv,
};
