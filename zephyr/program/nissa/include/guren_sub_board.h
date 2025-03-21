/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Guren sub-board declarations */

#ifndef __CROS_EC_NISSA_NISSA_SUB_BOARD_H__
#define __CROS_EC_NISSA_NISSA_SUB_BOARD_H__

enum guren_sub_board_type {
	GUREN_SB_UNKNOWN = -1, /* Uninitialised */
	GUREN_SB_NONE = 0, /* No board defined */
	GUREN_SB_1C = 1, /* USB type C */
	GUREN_SB_1A = 2, /* USB type A */
	GUREN_SB_1C_1A = 3, /* USB type C, USB type A */
	GUREN_SB_1C_LTE = 4, /* USB type C, LTE */
	GUREN_SB_HDMI_LTE = 5, /* HDMI, LTE */
	GUREN_SB_HDMI = 6, /* HDMI */
	GUREN_SB_HDMI_1A = 7, /* HDMI, USB type A */
};

enum guren_sub_board_type guren_get_sb_type(void);
void nissa_configure_hdmi_rails(void);

#endif /* __CROS_EC_NISSA_NISSA_SUB_BOARD_H__ */
