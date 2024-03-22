/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Yaviks sub-board declarations */

#ifndef __CROS_EC_NISSA_NISSA_SUB_BOARD_H__
#define __CROS_EC_NISSA_NISSA_SUB_BOARD_H__

enum yaviks_sub_board_type {
	YAVIKS_SB_UNKNOWN = -1, /* Uninitialised */
	YAVIKS_SB_A = 0, /* Only USB type A */
	YAVIKS_SB_C_A = 1, /* USB type C, USB type A */
};

enum yaviks_sub_board_type yaviks_get_sb_type(void);

#endif /* __CROS_EC_NISSA_NISSA_SUB_BOARD_H__ */
