/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Guybrush CrOS Board Info(CBI) utilities */

#include "base_fw_config.h"
#include "console.h"
#include "common.h"
#include "cros_board_info.h"
#include "hooks.h"

uint32_t get_sku_id(void)
{
	static uint32_t sku_id;

	if (sku_id == 0) {
		uint32_t val;

		if (cbi_get_sku_id(&val) != EC_SUCCESS)
			return 0;
		sku_id = val;
	}
	return sku_id;
}

uint32_t get_board_version(void)
{
	static uint32_t board_version;

	if (board_version == 0) {
		uint32_t val;

		if (cbi_get_board_version(&val) != EC_SUCCESS)
			return -1;
		board_version = val;
	}
	return board_version;
}

uint32_t get_fw_config(void)
{
	static uint32_t fw_config = UNINITIALIZED_FW_CONFIG;

	if (fw_config == UNINITIALIZED_FW_CONFIG) {
		uint32_t val;

		if (cbi_get_fw_config(&val) != EC_SUCCESS)
			return UNINITIALIZED_FW_CONFIG;
		fw_config = val;
	}
	return  fw_config;
}


int get_fw_config_field(uint8_t offset, uint8_t width)
{
	uint32_t fw_config = get_fw_config();

	if (fw_config == UNINITIALIZED_FW_CONFIG)
		return -1;

	return (fw_config >> offset) & ((1 << width) - 1);
}


__overridable void board_cbi_init(void)
{
}

static void cbi_init(void)
{
	uint32_t board_ver = get_board_version();
	uint32_t sku_id = get_sku_id();
	uint32_t fw_config = get_fw_config();

	if (board_ver != 0)
		ccprints("Board Version: %d (0x%x)", board_ver, board_ver);
	else
		ccprints("Board Version: not set in cbi");

	if (sku_id != 0)
		ccprints("SKU ID: %d (0x%x)", sku_id, sku_id);
	else
		ccprints("SKU ID: not set in cbi");

	if (fw_config != UNINITIALIZED_FW_CONFIG)
		ccprints("FW Config: %d (0x%x)", fw_config, fw_config);
	else
		ccprints("FW Config: not set in cbi");

	/* Allow the board project to make runtime changes based on CBI data */
	board_cbi_init();
}
DECLARE_HOOK(HOOK_INIT, cbi_init, HOOK_PRIO_INIT_I2C + 1);
