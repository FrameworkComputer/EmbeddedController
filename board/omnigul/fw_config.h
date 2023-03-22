/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __BOARD_OMNIGUL_FW_CONFIG_H_
#define __BOARD_OMNIGUL_FW_CONFIG_H_

#include <stdint.h>

/****************************************************************************
 * CBI FW_CONFIG layout for Omnigul board.
 *
 * Source of truth is the project/brya/brya/config.star configuration file.
 */

enum ec_cfg_keyboard_backlight_type {
	KEYBOARD_BACKLIGHT_DISABLED = 0,
	KEYBOARD_BACKLIGHT_ENABLED = 1
};

enum ec_cfg_tabletmode_type { TABLETMODE_DISABLED = 0, TABLETMODE_ENABLED = 1 };

enum ec_cfg_storage_type {
	STORAGE_UNPROVISION = 0,
	STORAGE_UFS = 1,
	STORAGE_NVMe = 2
};

enum ec_cfg_audio_type {
	AUDIO_ALC5682I_ALC1019 = 0,
	AUDIO_ALC5682I_ALC1019_3MIC = 1
};

enum ec_cfg_sar_id { SAR_ID_0 = 0, SAR_ID_1 = 1 };

union omnigul_cbi_fw_config {
	struct {
		enum ec_cfg_keyboard_backlight_type kb_bl : 1;
		enum ec_cfg_tabletmode_type tab_mode : 1;
		enum ec_cfg_storage_type stg : 2;
		enum ec_cfg_audio_type aud : 1;
		enum ec_cfg_sar_id sar_id : 1;
		uint32_t reserved_1 : 26;
	};
	uint32_t raw_value;
};

/**
 * Read the cached FW_CONFIG.  Guaranteed to have valid values.
 *
 * @return the FW_CONFIG for the board.
 */
union omnigul_cbi_fw_config get_fw_config(void);

#endif /* __BOARD_OMNIGUL_FW_CONFIG_H_ */
