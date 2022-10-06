/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Nissa common declarations */

#ifndef __CROS_EC_NISSA_NISSA_COMMON_H__
#define __CROS_EC_NISSA_NISSA_COMMON_H__

#include "usb_mux.h"

enum nissa_sub_board_type {
	NISSA_SB_UNKNOWN = -1, /* Uninitialised */
	NISSA_SB_NONE = 0, /* No board defined */
	NISSA_SB_C_A = 1, /* USB type C, USB type A */
	NISSA_SB_C_LTE = 2, /* USB type C, WWAN LTE */
	NISSA_SB_HDMI_A = 3, /* HDMI, USB type A */
};

enum nissa_sub_board_type nissa_get_sb_type(void);

#endif /* __CROS_EC_NISSA_NISSA_COMMON_H__ */
