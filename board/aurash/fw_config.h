/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __BOARD_AURASH_FW_CONFIG_H_
#define __BOARD_AURASH_FW_CONFIG_H_

#include <stdint.h>

/****************************************************************************
 * CBI FW_CONFIG layout for Aurash board.
 *
 * Source of truth is the project/brask/aurash/config.star configuration file.
 */

enum ec_cfg_bj_power { BJ_90W = 0, BJ_135W = 1 };

enum ec_cfg_power_on_monitor {
	POWER_ON_MONITOR_ENABLE = 0,
	POWER_ON_MONITOR_DISABLE = 1
};

union aurash_cbi_fw_config {
	struct {
		uint32_t bj_power : 2;
		uint32_t mlb_usb_tbt : 2;
		uint32_t storage : 2;
		uint32_t audio : 1;
		enum ec_cfg_power_on_monitor po_mon : 1;
		uint32_t reserved_1 : 24;
	};
	uint32_t raw_value;
};

/**
 * Read the cached FW_CONFIG.  Guaranteed to have valid values.
 *
 * @return the FW_CONFIG for the board.
 */
union aurash_cbi_fw_config get_fw_config(void);

/**
 * Get the barrel-jack power from FW_CONFIG.
 */
void ec_bj_power(uint32_t *voltage, uint32_t *current);

/**
 * Get enable/disable power on by monitor from FW_CONFIG.
 */
enum ec_cfg_power_on_monitor ec_cfg_power_on_monitor(void);

#endif /* __BOARD_AURASH_FW_CONFIG_H_ */
