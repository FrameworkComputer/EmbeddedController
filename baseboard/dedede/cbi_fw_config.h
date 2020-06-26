/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef _DEDEDE_CBI_FW_CONFIG__H_
#define _DEDEDE_CBI_FW_CONFIG__H_

/****************************************************************************
 * Dedede CBI FW Configuration
 */

/*
 * Daughter Board (Bits 0-3)
 */
enum fw_config_db {
	DB_NONE,
	DB_2C,
	DB_1C_LTE,
	DB_1A_HDMI,
	DB_1C,
};
#define FW_CONFIG_DB_OFFSET	0
#define FW_CONFIG_DB_MASK	GENMASK(3, 0)

enum fw_config_db get_cbi_fw_config_db(void);

#endif /* _DEDEDE_CBI_FW_CONFIG__H_ */
