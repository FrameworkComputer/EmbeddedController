/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "cros_board_info.h"
#include "hooks.h"
#include "usb_mux.h"
#include "usbc/usb_muxes.h"

#include <zephyr/devicetree.h>

#define PS8802_MUX USB_MUX_STRUCT_NAME(DT_NODELABEL(ps8802_mux_0))

/* Setup mux address based on CBI board version. */
void setup_mux_config(void)
{
	int board_version;
	if ((cbi_get_board_version(&board_version) == EC_SUCCESS) &&
	    (board_version == 0))
		usb_muxes[1].mux = &PS8802_MUX;
}
DECLARE_HOOK(HOOK_INIT, setup_mux_config, HOOK_PRIO_INIT_I2C + 2);
