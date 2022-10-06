/* Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Asurada board-specific USB-C configuration */

#include "driver/ppc/syv682x.h"
#include "driver/usb_mux/ps8743.h"
#include "hooks.h"

__override int syv682x_board_is_syv682c(int port)
{
	return board_get_version() > 2;
}

void board_usb_mux_init(void)
{
	if (board_get_sub_board() == SUB_BOARD_TYPEC) {
		ps8743_tune_usb_eq(usb_muxes[1].mux, PS8743_USB_EQ_TX_12_8_DB,
				   PS8743_USB_EQ_RX_12_8_DB);
		ps8743_write(usb_muxes[1].mux, PS8743_REG_HS_DET_THRESHOLD,
			     PS8743_USB_HS_THRESH_NEG_10);
		ps8743_field_update(usb_muxes[1].mux, PS8743_REG_DCI_CONFIG_2,
				    PS8743_AUTO_DCI_MODE_MASK,
				    PS8743_AUTO_DCI_MODE_FORCE_USB);
	}
}
DECLARE_HOOK(HOOK_INIT, board_usb_mux_init, HOOK_PRIO_INIT_I2C + 1);
