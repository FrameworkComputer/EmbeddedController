/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __BOARD_LISBON_FW_CONFIG_H_
#define __BOARD_LISBON_FW_CONFIG_H_

#include <stdint.h>

/****************************************************************************
 * CBI FW_CONFIG layout for Lisbon board.
 *
 * Source of truth is the project/brask/lisbon/config.star configuration file.
 */
enum ec_cfg_bj_power { BJ_65W = 0, BJ_90W = 1 };

enum ec_cfg_storage { EMMC = 0, SSD = 1 };

enum ec_cfg_fvm_support { FVM_NO = 0, FVM_YES = 1 };

union lisbon_cbi_fw_config {
	struct {
		uint32_t bj_power : 1;
		uint32_t storage : 1;
		uint32_t fvm_support : 1;
		uint32_t reserved_1 : 29;
	};
	uint32_t raw_value;
};

/**
 * Read the cached FW_CONFIG.  Guaranteed to have valid values.
 *
 * @return the FW_CONFIG for the board.
 */
union lisbon_cbi_fw_config get_fw_config(void);

/**
 * Get the barrel-jack power from FW_CONFIG.
 */
void ec_bj_power(uint32_t *voltage, uint32_t *current);

#endif /* __BOARD_LISBON_FW_CONFIG_H_ */
