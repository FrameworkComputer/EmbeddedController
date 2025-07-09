/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Pujjoniru sub-board declarations */

#ifndef __CROS_EC_NISSA_NISSA_SUB_BOARD_H__
#define __CROS_EC_NISSA_NISSA_SUB_BOARD_H__

enum pujjoniru_sub_board_type {
	PUJJONIRU_SB_UNKNOWN = -1, /* Uninitialised */
	PUJJONIRU_SB_NONE = 0,
	PUJJONIRU_SB_C = 1, /* USB type C */
};

enum pujjoniru_sub_board_type pujjoniru_get_sb_type(void);

#endif /* __CROS_EC_NISSA_NISSA_SUB_BOARD_H__ */
