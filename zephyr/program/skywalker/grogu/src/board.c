/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * USB-C board functions for the Grogu reference board only
 */

#include "cros_board_info.h"
#include "cros_cbi.h"
#include "hooks.h"

#include <stdint.h>

#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(grogu_board, LOG_LEVEL_INF);

static void board_init(void)
{
	uint32_t board_id, sku_id;
	uint32_t new_sku_id, new_fw_config;
	uint32_t model_id = 0;
	uint8_t dram_buf[20];
	uint8_t size = sizeof(dram_buf);
	bool change_flag = true;

	cbi_get_board_version(&board_id);

	if (board_id == 2) {
		LOG_INF("Grogu: Board id %d", board_id);
		cbi_get_sku_id(&sku_id);
		cbi_get_board_info(CBI_TAG_DRAM_PART_NUM, dram_buf, &size);

		if ((sku_id == 0x110003) && (dram_buf[4] == 0x39)) {
			new_sku_id = 0x110001;
			new_fw_config = 0x96c00020;
		} else if (sku_id == 0x110002) {
			new_sku_id = 0x110000;
			new_fw_config = 0x96c00000;
		} else if ((sku_id == 0x140004) && (dram_buf[4] == 0x39)) {
			new_sku_id = 0x140000;
			new_fw_config = 0x96c00140;
		} else if (sku_id == 0x140007) {
			new_sku_id = 0x140003;
			new_fw_config = 0x96c001a0;
		} else {
			change_flag = false;
		}

		if (change_flag) {
			cbi_set_board_info(CBI_TAG_SKU_ID,
					   (uint8_t *)&new_sku_id,
					   sizeof(new_sku_id));
			cbi_set_board_info(CBI_TAG_FW_CONFIG,
					   (uint8_t *)&new_fw_config,
					   sizeof(new_fw_config));
			cbi_set_model_id(model_id);
			LOG_INF("Modify CBI setting sku id: 0x%x, fw config: 0x%x",
				new_sku_id, new_fw_config);
		}
	}
}
DECLARE_HOOK(HOOK_INIT, board_init, HOOK_PRIO_INIT_I2C + 1);
