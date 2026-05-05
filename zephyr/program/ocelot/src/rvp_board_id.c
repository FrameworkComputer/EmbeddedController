/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "cros_board_info.h"

#include <zephyr/logging/log.h>

LOG_MODULE_DECLARE(rvp_model_id, LOG_LEVEL_DBG);

#define BOARD_GPIOS_COUNT 6

#define BOARD_ID_MASK (BIT(BOARD_GPIOS_COUNT) - 1)

/** Cache RVP model ID for future reads */
static int rvp_model_id = -1;

__override int board_get_version(void)
{
	if (rvp_model_id == -1) {
		int sku_id;

		if (cbi_get_sku_id(&sku_id) != EC_SUCCESS) {
			LOG_ERR("RVP_ID: SKU ID not available");
			return -1;
		}

		switch (sku_id) {
		case 1:
		case 2:
		case 3:
			/* SKU IDs 1, 2, and 3 are LP5x */
			rvp_model_id = 33;
			break;
		case 4:
			/* SKU ID 4 is DDR5 */
			rvp_model_id = 32;
			break;
		default:
			LOG_ERR("RVP_ID: Unknown SKU ID %d", sku_id);
			return -1;
		}

		LOG_INF("RVP_ID: sku_id=%d, rvp_model_id=%d", sku_id,
			rvp_model_id);
	}

	/* return only board id from model id */
	return rvp_model_id & BOARD_ID_MASK;
}
