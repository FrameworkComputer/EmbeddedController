/* Copyright 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Ryu-custom USB mux driver. */

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
	/* reset everything */
	gpio_set_level(GPIO_USBC_MUX_CONF0, 0);
	gpio_set_level(GPIO_USBC_MUX_CONF1, 0);
	gpio_set_level(GPIO_USBC_MUX_CONF2, 0);

	if (!(mux_state & (MUX_USB_ENABLED | MUX_DP_ENABLED)))
		/* everything is already disabled, we can return */
		return EC_SUCCESS;

	gpio_set_level(GPIO_USBC_MUX_CONF0, mux_state & MUX_POLARITY_INVERTED);

	if (mux_state & MUX_USB_ENABLED)
		/* USB 3.0 uses 2 superspeed lanes */
		gpio_set_level(GPIO_USBC_MUX_CONF2, 1);

	if (mux_state & MUX_DP_ENABLED)
		/* DP uses available superspeed lanes (x2 or x4) */
		gpio_set_level(GPIO_USBC_MUX_CONF1, 1);

	return EC_SUCCESS;
}

static int board_get_usb_mux(int port, mux_state_t *mux_state)
{
	*mux_state = 0;

	if (gpio_get_level(GPIO_USBC_MUX_CONF2))
		*mux_state |= MUX_USB_ENABLED;
	if (gpio_get_level(GPIO_USBC_MUX_CONF1))
		*mux_state |= MUX_DP_ENABLED;
	if (gpio_get_level(GPIO_USBC_MUX_CONF0))
		*mux_state |= MUX_POLARITY_INVERTED;

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
