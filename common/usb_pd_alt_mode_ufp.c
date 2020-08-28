/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Alternate Mode Upstream Facing Port (UFP) USB-PD module.
 */
#include "usb_pd.h"
#include "usb_tbt_alt_mode.h"

static uint32_t ufp_enter_mode[CONFIG_USB_PD_PORT_MAX_COUNT];

/* Save port partner's enter mode message */
void pd_ufp_set_enter_mode(int port, uint32_t *payload)
{
	ufp_enter_mode[port] = payload[1];
}

/* Return port partner's enter mode message */
uint32_t pd_ufp_get_enter_mode(int port)
{
	return ufp_enter_mode[port];
}
