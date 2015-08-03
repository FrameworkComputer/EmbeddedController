/* Copyright 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Honeybuns-custom USB mux driver. */

#include "common.h"
#include "gpio.h"
#include "usb_mux.h"
#include "util.h"

static int board_init_usb_mux(int port)
{
	return EC_SUCCESS;
}

static int board_set_usb_mux(int port, mux_state_t mux_state)
{
	if (!(mux_state & (MUX_USB_ENABLED | MUX_DP_ENABLED))) {
		/* put the mux in the high impedance state */
		gpio_set_level(GPIO_SS_MUX_OE_L, 1);
		/* Disable display hardware */
		gpio_set_level(GPIO_BRIDGE_RESET_L, 0);
		gpio_set_level(GPIO_SPLITTER_RESET_L, 0);
		/* Put the USB hub under reset */
		hx3_enable(0);
		return EC_SUCCESS;
	}

	/* Trigger USB Hub configuration */
	hx3_enable(1);

	if (mux_state & MUX_USB_ENABLED)
		/* Low selects USB Dock */
		gpio_set_level(GPIO_SS_MUX_SEL, 0);
	else
		/* high selects display port */
		gpio_set_level(GPIO_SS_MUX_SEL, 1);

	/* clear OE line to make mux active */
	gpio_set_level(GPIO_SS_MUX_OE_L, 0);

	if (mux_state & MUX_DP_ENABLED) {
		/* Enable display hardware */
		gpio_set_level(GPIO_BRIDGE_RESET_L, 1);
		gpio_set_level(GPIO_SPLITTER_RESET_L, 1);
	}

	return EC_SUCCESS;
}

static int board_get_usb_mux(int port, mux_state_t *mux_state)
{
	int oe_disabled = gpio_get_level(GPIO_SS_MUX_OE_L);
	int dp_4lanes = gpio_get_level(GPIO_SS_MUX_SEL);

	if (oe_disabled)
		*mux_state = 0;
	else if (dp_4lanes)
		*mux_state = MUX_DP_ENABLED;
	else
		*mux_state = MUX_USB_ENABLED | MUX_DP_ENABLED;

	return EC_SUCCESS;
}

const struct usb_mux_driver board_custom_usb_mux_driver = {
	.init = board_init_usb_mux,
	.set = board_set_usb_mux,
	.get = board_get_usb_mux,
};

struct usb_mux usb_muxes[CONFIG_USB_PD_PORT_COUNT] = {
	{
		.port_addr = 0,
		.driver    = &board_custom_usb_mux_driver,
	},
};
