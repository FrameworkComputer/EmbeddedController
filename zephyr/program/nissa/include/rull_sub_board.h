/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Anraggar sub-board declarations */

#ifndef __CROS_EC_NISSA_NISSA_SUB_BOARD_H__
#define __CROS_EC_NISSA_NISSA_SUB_BOARD_H__

enum rull_sub_board_type {
	RULL_SB_UNKNOWN = -1, /* Uninitialised */
	RULL_SB_NONE = 0,
	RULL_SB_C = 1, /* USB type C */
};

enum rull_sub_board_type rull_get_sb_type(void);

#endif /* __CROS_EC_NISSA_NISSA_SUB_BOARD_H__ */
