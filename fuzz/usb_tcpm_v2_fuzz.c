/* Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Stubs needed for fuzz testing the USB TCPMv2 state machines.
 */

#define HIDE_EC_STDLIB
#include "usb_pd.h"
#include "charge_manager.h"

const struct svdm_response svdm_rsp = {
	.identity = NULL,
	.svids = NULL,
	.modes = NULL,
};

int pd_check_vconn_swap(int port)
{
	return 1;
}

void dfp_consume_cable_response(int port, int cnt, uint32_t *payload,
				uint16_t head)
{
}
