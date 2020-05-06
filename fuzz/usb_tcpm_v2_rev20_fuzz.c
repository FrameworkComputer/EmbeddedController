/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Stubs needed for fuzz testing the USB TCPMv2 state machines.
 */

#define HIDE_EC_STDLIB
#include "charge_manager.h"
#include "mock/usb_mux_mock.h"
#include "usb_pd.h"

const struct svdm_response svdm_rsp = {
	.identity = NULL,
	.svids = NULL,
	.modes = NULL,
};

/* USB mux configuration */
const struct usb_mux usb_muxes[CONFIG_USB_PD_PORT_MAX_COUNT] = {
	{
		.driver = &mock_usb_mux_driver,
	},
	{
		.driver = &mock_usb_mux_driver,
	}
};

int pd_check_vconn_swap(int port)
{
	return 1;
}

