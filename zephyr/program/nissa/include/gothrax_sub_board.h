/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Gothrax sub-board declarations */

#ifndef __CROS_EC_NISSA_GOTHRAX_SUB_BOARD_H__
#define __CROS_EC_NISSA_GOTHRAX_SUB_BOARD_H__

enum gothrax_sub_board_type {
	GOTHRAX_SB_UNKNOWN = -1, /* Uninitialised */
	GOTHRAX_SB_NONE = 0, /* No board defined */
	GOTHRAX_SB_C_A = 1, /* USB type C, USB type A */
	GOTHRAX_SB_C_A_LTE = 2, /* USB type C, USB type A, WWAN LTE */
	GOTHRAX_SB_A = 3, /* USB type A */
};

enum gothrax_sub_board_type gothrax_get_sb_type(void);

#endif /* __CROS_EC_NISSA_GOTHRAX_SUB_BOARD_H__ */
