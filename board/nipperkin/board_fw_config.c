/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "base_fw_config.h"
#include "board_fw_config.h"

bool board_has_kblight(void)
{
	return (get_fw_config_field(FW_CONFIG_KBLIGHT_OFFSET,
			FW_CONFIG_KBLIGHT_WIDTH) == FW_CONFIG_KBLIGHT_YES);
}

enum board_usb_c1_mux board_get_usb_c1_mux(void)
{
	return USB_C1_MUX_PS8818;
};

enum board_usb_a1_retimer board_get_usb_a1_retimer(void)
{
	return USB_A1_RETIMER_PS8811;
};

bool board_has_privacy_panel(void)
{
	return (get_fw_config_field(FW_CONFIG_KEYBOARD_OFFSET,
			FW_CONFIG_KEYBOARD_WIDTH) ==
			FW_CONFIG_KEYBOARD_PRIVACY_YES);
}
