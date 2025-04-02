/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Epic sub-board declarations */

#ifndef __CROS_EC_NISSA_EPIC_SUB_BOARD_H__
#define __CROS_EC_NISSA_EPIC_SUB_BOARD_H__

enum epic_sub_board_type {
	EPIC_SB_UNKNOWN = -1, /* Uninitialised */
	EPIC_SB_NONE = 0, /* No board defined */
	EPIC_SB_C_A = 1, /* USB type C, USB type A */
	EPIC_SB_C_A_LTE = 2, /* USB type C, USB type A, WWAN LTE */
	EPIC_SB_A = 3, /* USB type A */
	EPIC_SB_A_HDMI_LTE = 6, /* HDMI, USB type A, WWAN LTE */
};

enum epic_sub_board_type epic_get_sb_type(void);

#endif /* __CROS_EC_NISSA_EPIC_SUB_BOARD_H__ */
