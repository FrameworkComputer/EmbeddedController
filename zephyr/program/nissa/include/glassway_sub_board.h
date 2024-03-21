/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Glassway sub-board declarations */

#ifndef __CROS_EC_NISSA_NISSA_SUB_BOARD_H__
#define __CROS_EC_NISSA_NISSA_SUB_BOARD_H__

enum glassway_sub_board_type {
	GLASSWAY_SB_UNKNOWN = -1, /* Uninitialised */
	GLASSWAY_SB_NONE = 0, /* No board defined */
	GLASSWAY_SB_1C = 1, /* USB type C */
	GLASSWAY_SB_1A = 2, /* USB type A */
	GLASSWAY_SB_1C_1A = 3, /* USB type C, USB type A */
};

enum glassway_sub_board_type glassway_get_sb_type(void);

#endif /* __CROS_EC_NISSA_NISSA_SUB_BOARD_H__ */
