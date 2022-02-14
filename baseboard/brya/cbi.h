/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* brya family-specific CBI functions, shared with Zephyr */

#ifndef __CROS_EC_BASEBOARD_CBI_H
#define __CROS_EC_BASEBOARD_CBI_H

#include "common.h"

/*
 * Return the board revision number.
 */
uint8_t get_board_id(void);

/**
 * Configure run-time data structures and operation based on CBI data. This
 * typically includes customization for changes in the BOARD_VERSION and
 * FW_CONFIG fields in CBI. This routine is called from the baseboard after
 * the CBI data has been initialized.
 */
__override_proto void board_cbi_init(void);

/**
 * Initialize the FW_CONFIG from CBI data. If the CBI data is not valid, set the
 * FW_CONFIG to the board specific defaults.
 */
__override_proto void board_init_fw_config(void);

/**
 * Initialize the SSFC from CBI data. If the CBI data is not valid, set the
 * SSFC to the board specific defaults.
 */
__override_proto void board_init_ssfc(void);

#endif /* __CROS_EC_BASEBOARD_CBI_H */
