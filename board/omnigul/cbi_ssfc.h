/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef _OMNIGUL_CBI_SSFC__H_
#define _OMNIGUL_CBI_SSFC__H_

#include "stdint.h"

/****************************************************************************
 * Omnigul CBI Second Source Factory Cache
 */

/*
 * Keyboard layout (Bits 0-1)
 */
enum ec_ssfc_keyboard_layout { KEYBOARD_DEFAULT = 0, KEYBOARD_ANSI = 1 };

union omnigul_cbi_ssfc {
	struct {
		uint32_t keyboard_layout : 2;
		uint32_t reserved_2 : 30;
	};
	uint32_t raw_value;
};

enum ec_ssfc_keyboard_layout get_cbi_ssfc_keyboard_layout(void);

#endif /* _OMNIGUL_CBI_SSFC__H_ */
