/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef _GUYBRUSH_BASE_FW_CONFIG__H_
#define _GUYBRUSH_BASE_FW_CONFIG__H_

#define UNINITIALIZED_FW_CONFIG 0xFFFFFFFF

#include <stdbool.h>
#include <stdint.h>

/*
 * Takes a bit offset and bit width and returns the fw_config field at that
 * offset and width. Returns -1 if an error occurs.
 */
int get_fw_config_field(uint8_t offset, uint8_t width);

/*
 * Each Guybrush board variant will define a board specific fw_config schema.
 * Below is the schema agnostic interface for fw_config fields.
 * Fields that are not applicable outside a specific Guybrush variant do not
 * need to be included here.
 */

enum board_usb_a1_retimer {
	USB_A1_RETIMER_UNKNOWN,
	USB_A1_RETIMER_PS8811,
	USB_A1_RETIMER_ANX7491
};

enum board_usb_c1_mux {
	USB_C1_MUX_UNKNOWN,
	USB_C1_MUX_PS8818,
	USB_C1_MUX_ANX7451
};

enum board_form_factor {
	FORM_FACTOR_UNKNOWN,
	FORM_FACTOR_CLAMSHELL,
	FORM_FACTOR_CONVERTIBLE
};

bool board_has_kblight(void);
enum board_usb_a1_retimer board_get_usb_a1_retimer(void);
enum board_usb_c1_mux board_get_usb_c1_mux(void);
enum board_form_factor board_get_form_factor(void);
bool board_is_convertible(void);

#endif /* _GUYBRUSH_BASE_FW_CONFIG__H_ */
