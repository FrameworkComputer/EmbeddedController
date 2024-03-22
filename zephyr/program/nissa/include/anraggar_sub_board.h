/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Anraggar sub-board declarations */

#ifndef __CROS_EC_NISSA_NISSA_SUB_BOARD_H__
#define __CROS_EC_NISSA_NISSA_SUB_BOARD_H__

enum anraggar_sub_board_type {
	ANRAGGAR_SB_UNKNOWN = -1, /* Uninitialised */
	ANRAGGAR_SB_NONE = 0,
	ANRAGGAR_SB_C = 1, /* USB type C */
};

enum anraggar_sub_board_type anraggar_get_sb_type(void);

#endif /* __CROS_EC_NISSA_NISSA_SUB_BOARD_H__ */
