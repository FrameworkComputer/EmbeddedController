/* Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __BOARD_VELL_FW_CONFIG_H_
#define __BOARD_VELL_FW_CONFIG_H_

#include <stdint.h>

/****************************************************************************
 * CBI FW_CONFIG layout for Vell board.
 *
 * Source of truth is the project/brya/vell/config.star configuration file.
 */
union vell_cbi_fw_config {
	struct {
		uint32_t lte_db : 1;
		uint32_t kb_color : 1;
		uint32_t storage_nand : 1;
		uint32_t wifi_sar_id : 2;
		uint32_t reserved_1 : 27;
	};
	uint32_t raw_value;
};

#endif /* __BOARD_VELL_FW_CONFIG_H_ */
