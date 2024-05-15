/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Rauru sub-board hardware configuration */

#include "cros_board_info.h"
#include "cros_cbi.h"
#include "rauru_sub_board.h"

#include <zephyr/init.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/printk.h>

LOG_MODULE_DECLARE(rauru, CONFIG_RAURU_LOG_LEVEL);

test_export_static enum rauru_sub_board_type rauru_cached_sub_board =
	RAURU_SB_UNKNOWN;

/*
 * Retrieve sub-board type from FW_CONFIG.
 */
enum rauru_sub_board_type rauru_get_sb_type(void)
{
	int ret;
	uint32_t val;

	/*
	 * Return cached value.
	 */
	if (rauru_cached_sub_board != RAURU_SB_UNKNOWN)
		return rauru_cached_sub_board;

	rauru_cached_sub_board = RAURU_SB_NONE; /* Defaults to none */
	ret = cros_cbi_get_fw_config(FW_SUB_BOARD, &val);
	if (ret != 0) {
		LOG_WRN("Error retrieving CBI FW_CONFIG field %d",
			FW_SUB_BOARD);
		return rauru_cached_sub_board;
	}
	switch (val) {
	default:
		LOG_WRN("SB: No sub-board defined");
		break;
	case FW_SB_REDRIVER:
		rauru_cached_sub_board = RAURU_SB_REDRIVER;
		LOG_INF("SB: USB Redriver");
		break;

	case FW_SB_RETIMER:
		rauru_cached_sub_board = RAURU_SB_RETIMER;
		LOG_INF("SB: USB Retimer");
		break;
	}
	return rauru_cached_sub_board;
}
