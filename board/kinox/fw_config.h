/* Copyright 2022 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __BOARD_KINOX_FW_CONFIG_H_
#define __BOARD_KINOX_FW_CONFIG_H_

#include <stdint.h>

/****************************************************************************
 * CBI FW_CONFIG layout for kinox board.
 *
 * Source of truth is the project/brask/kinox/config.star configuration file.
 */

enum ec_cfg_dp_display {
	ABSENT = 0,
	DB_HDMI = 1,
	DB_DP = 2
};

union kinox_cbi_fw_config {
	struct {
		uint32_t dp_display : 4;
		uint32_t reserved_1 : 28;
	};
	uint32_t raw_value;
};

/**
 * Read the cached FW_CONFIG.  Guaranteed to have valid values.
 *
 * @return the FW_CONFIG for the board.
 */
union kinox_cbi_fw_config get_fw_config(void);

#endif /* __BOARD_KINOX_FW_CONFIG_H_ */
