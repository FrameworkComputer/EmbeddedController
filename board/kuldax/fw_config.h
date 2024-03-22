/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __BOARD_BRASK_FW_CONFIG_H_
#define __BOARD_BRASK_FW_CONFIG_H_

#include <stdint.h>

/****************************************************************************
 * CBI FW_CONFIG layout for Brask board.
 *
 * Source of truth is the project/brask/brask/config.star configuration file.
 */
enum ec_cfg_audio_type { DB_AUDIO_UNKNOWN = 0, DB_NAU88L25B_I2S = 1 };

enum ec_cfg_bj_power {
	BJ_150W = 0,
	BJ_230W = 1,
	BJ_65W = 2,
	BJ_135W = 3,
	BJ_90W = 4
};

/*
 * Peripheral charger  (Bits 5)
 */
enum ec_cfg_peripheral_charger {
	PERIPHERAL_CHARGER_ENABLE = 0,
	PERIPHERAL_CHARGER_DISABLE = 1
};

/*
 * MB USB Type-C  (Bits 6-7)
 */
enum conf_mb_usbc_type { MB_TC_USB4 = 0, MB_TC_USB3 = 1 };

union brask_cbi_fw_config {
	struct {
		uint32_t audio : 3;
		uint32_t bj_power : 2;
		uint32_t peripheral_charger : 1;
		uint32_t usbc_type : 2;
		uint32_t bj_power_extended : 2;
		uint32_t reserved_1 : 22;
	};
	uint32_t raw_value;
};

/**
 * Read the cached FW_CONFIG.  Guaranteed to have valid values.
 *
 * @return the FW_CONFIG for the board.
 */
union brask_cbi_fw_config get_fw_config(void);

/**
 * Get the barrel-jack power from FW_CONFIG.
 */
void ec_bj_power(uint32_t *voltage, uint32_t *current);

/**
 * SWITCH the peripheral charger function enable/disable from FW_CONFIG.
 */
bool ec_cfg_has_peripheral_charger(void);

/**
 * Get the USB main board type from FW_CONFIG.
 *
 * @return the USB main board type.
 */
enum conf_mb_usbc_type get_mb_usbc_type(void);

#endif /* __BOARD_BRASK_FW_CONFIG_H_ */
