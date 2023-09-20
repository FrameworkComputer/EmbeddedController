/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "console.h"
#include "power.h"
#include "usbc/usb_muxes.h"
#include "ztest/usb_mux_config.h"

#define CPRINTSUSB(format, args...) cprints(CC_USBCHARGE, format, ##args)

int board_mux_set(const struct usb_mux *mux, mux_state_t state)
{
	static mux_state_t current[CONFIG_USB_PD_PORT_MAX_COUNT];

	/* Wake the AP for a dark resume on DP connect/disconnect. */
	if (power_get_state() == POWER_S0ix) {
		if ((state & USB_PD_MUX_DP_ENABLED) !=
		    (current[mux->usb_port] & USB_PD_MUX_DP_ENABLED)) {
			CPRINTSUSB("DP connect/disconnect, waking AP");
			host_set_single_event(EC_HOST_EVENT_USB_MUX);
		}
	}

	current[mux->usb_port] = state;
	return 0;
}
