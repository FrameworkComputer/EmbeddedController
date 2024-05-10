/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Pujjoga sub-board declarations */

#ifndef __CROS_EC_NISSA_PUJJOGA_SUB_BOARD_H__
#define __CROS_EC_NISSA_PUJJOGA_SUB_BOARD_H__

enum pujjoga_sub_board_type {
	PUJJOGA_SB_UNKNOWN = -1, /* Uninitialised */
	PUJJOGA_SB_NONE = 0, /* No board defined */
	PUJJOGA_SB_HDMI_A = 1, /* HDMI, USB type A */
};

enum pujjoga_sub_board_type pujjoga_get_sb_type(void);

#endif /* __CROS_EC_NISSA_PUJJOGA_SUB_BOARD_H__ */
