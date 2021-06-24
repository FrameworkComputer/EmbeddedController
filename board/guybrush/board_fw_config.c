/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "base_fw_config.h"
#include "board_fw_config.h"

bool board_is_convertible(void)
{
	return (get_fw_config_field(FW_CONFIG_FORM_FACTOR_OFFSET,
			FW_CONFIG_FORM_FACTOR_WIDTH)
			== FW_CONFIG_FORM_FACTOR_CONVERTIBLE);
}

bool board_has_kblight(void)
{
	return (get_fw_config_field(FW_CONFIG_KBLIGHT_OFFSET,
			FW_CONFIG_KBLIGHT_WIDTH) == FW_CONFIG_KBLIGHT_YES);
}

enum board_usb_c1_mux board_get_usb_c1_mux(void)
{
	int usb_db = get_fw_config_field(FW_CONFIG_USB_DB_OFFSET,
					 FW_CONFIG_USB_DB_WIDTH);
	if (usb_db == FW_CONFIG_USB_DB_A1_PS8811_C1_PS8818)
		return USB_C1_MUX_PS8818;
	if (usb_db == FW_CONFIG_USB_DB_A1_ANX7491_C1_ANX7451)
		return USB_C1_MUX_ANX7451;
	return USB_C1_MUX_UNKNOWN;
};

enum board_usb_a1_retimer board_get_usb_a1_retimer(void)
{
	int usb_db = get_fw_config_field(FW_CONFIG_USB_DB_OFFSET,
					 FW_CONFIG_USB_DB_WIDTH);
	if (usb_db == FW_CONFIG_USB_DB_A1_PS8811_C1_PS8818)
		return USB_A1_RETIMER_PS8811;
	if (usb_db == FW_CONFIG_USB_DB_A1_ANX7491_C1_ANX7451)
		return USB_A1_RETIMER_ANX7491;
	return USB_A1_RETIMER_UNKNOWN;
};
