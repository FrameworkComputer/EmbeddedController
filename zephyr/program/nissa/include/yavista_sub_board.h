/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Yavista sub-board declarations */

#ifndef __CROS_EC_NISSA_NISSA_SUB_BOARD_H__
#define __CROS_EC_NISSA_NISSA_SUB_BOARD_H__

enum yavista_sub_board_type {
	YAVISTA_SB_UNKNOWN = -1, /* Uninitialised */
	YAVISTA_SB_A = 0, /* Only USB type A */
	YAVISTA_SB_C_A = 1, /* USB type C, USB type A */
};

enum yavista_sub_board_type yavista_get_sb_type(void);

#endif /* __CROS_EC_NISSA_NISSA_SUB_BOARD_H__ */
