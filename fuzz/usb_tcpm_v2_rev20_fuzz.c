/* Copyright 2020 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Stubs needed for fuzz testing the USB TCPMv2 state machines.
 */

#include "charge_manager.h"
#include "mock/usb_mux_mock.h"
#include "usb_pd.h"

const struct svdm_response svdm_rsp = {
	.identity = NULL,
	.svids = NULL,
	.modes = NULL,
};

/* USB mux configuration */
const struct usb_mux_chain usb_muxes[CONFIG_USB_PD_PORT_MAX_COUNT] = {
	{
		.mux =
			&(const struct usb_mux){
				.driver = &mock_usb_mux_driver,
			},
	},
	{
		.mux =
			&(const struct usb_mux){
				.driver = &mock_usb_mux_driver,
			},
	}
};

int pd_check_vconn_swap(int port)
{
	return 1;
}
