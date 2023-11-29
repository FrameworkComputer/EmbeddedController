/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Quandiso sub-board declarations */

#ifndef __CROS_EC_NISSA_NISSA_SUB_BOARD_H__
#define __CROS_EC_NISSA_NISSA_SUB_BOARD_H__

enum quandiso_sub_board_type {
	QUANDISO_SB_UNKNOWN = -1, /* Uninitialised */
	QUANDISO_SB_ABSENT = 0, /* sub-board absent */
	QUANDISO_SB_C_A = 1, /* 1C+1A */
	QUANDISO_SB_LTE = 2, /* Only LTE */
	QUANDISO_SB_C_LTE = 3 /* 1C+1LTE */
};

enum quandiso_sub_board_type quandiso_get_sb_type(void);

#endif /* __CROS_EC_NISSA_NISSA_SUB_BOARD_H__ */
