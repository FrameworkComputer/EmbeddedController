/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"

void board_dc_jack_interrupt(enum gpio_signal signal)
{
}

__override uint8_t board_get_usb_pd_port_count(void)
{
	return CONFIG_USB_PD_PORT_MAX_COUNT;
}
