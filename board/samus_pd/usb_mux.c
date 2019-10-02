/* Copyright 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Samus PD-custom USB mux driver. */

#include "common.h"
#include "gpio.h"
#include "usb_mux.h"
#include "util.h"

struct usb_port_mux {
	enum gpio_signal ss1_en_l;
	enum gpio_signal ss2_en_l;
	enum gpio_signal dp_mode_l;
	enum gpio_signal dp_polarity;
	enum gpio_signal ss1_dp_mode;
	enum gpio_signal ss2_dp_mode;
};

static const struct usb_port_mux mux_gpios[] = {
	{
		.ss1_en_l    = GPIO_USB_C0_SS1_EN_L,
		.ss2_en_l    = GPIO_USB_C0_SS2_EN_L,
		.dp_mode_l   = GPIO_USB_C0_DP_MODE_L,
		.dp_polarity = GPIO_USB_C0_DP_POLARITY,
		.ss1_dp_mode = GPIO_USB_C0_SS1_DP_MODE,
		.ss2_dp_mode = GPIO_USB_C0_SS2_DP_MODE,
	},
	{
		.ss1_en_l    = GPIO_USB_C1_SS1_EN_L,
		.ss2_en_l    = GPIO_USB_C1_SS2_EN_L,
		.dp_mode_l   = GPIO_USB_C1_DP_MODE_L,
		.dp_polarity = GPIO_USB_C1_DP_POLARITY,
		.ss1_dp_mode = GPIO_USB_C1_SS1_DP_MODE,
		.ss2_dp_mode = GPIO_USB_C1_SS2_DP_MODE,
	},
};
BUILD_ASSERT(ARRAY_SIZE(mux_gpios) == CONFIG_USB_PD_PORT_MAX_COUNT);


static int board_init_usb_mux(int port)
{
	return EC_SUCCESS;
}

static int board_set_usb_mux(int port, mux_state_t mux_state)
{
	const struct usb_port_mux *usb_mux = mux_gpios + port;
	int polarity = mux_state & MUX_POLARITY_INVERTED;

	/* reset everything */
	gpio_set_level(usb_mux->ss1_en_l, 1);
	gpio_set_level(usb_mux->ss2_en_l, 1);
	gpio_set_level(usb_mux->dp_mode_l, 1);
	gpio_set_level(usb_mux->dp_polarity, 1);
	gpio_set_level(usb_mux->ss1_dp_mode, 1);
	gpio_set_level(usb_mux->ss2_dp_mode, 1);

	if (!(mux_state & (MUX_USB_ENABLED | MUX_DP_ENABLED)))
		/* everything is already disabled, we can return */
		return EC_SUCCESS;

	if (mux_state & MUX_USB_ENABLED)
		/* USB 3.0 uses 2 superspeed lanes */
		gpio_set_level(polarity ? usb_mux->ss2_dp_mode :
					  usb_mux->ss1_dp_mode, 0);

	if (mux_state & MUX_DP_ENABLED) {
		/* DP uses available superspeed lanes (x2 or x4) */
		gpio_set_level(usb_mux->dp_polarity, polarity);
		gpio_set_level(usb_mux->dp_mode_l, 0);
	}

	/* switch on superspeed lanes */
	gpio_set_level(usb_mux->ss1_en_l, 0);
	gpio_set_level(usb_mux->ss2_en_l, 0);

	return EC_SUCCESS;
}

static int board_get_usb_mux(int port, mux_state_t *mux_state)
{
	const struct usb_port_mux *usb_mux = mux_gpios + port;

	*mux_state = 0;

	if (!gpio_get_level(usb_mux->ss1_dp_mode) ||
	    !gpio_get_level(usb_mux->ss2_dp_mode))
		*mux_state |= MUX_USB_ENABLED;

	if (!gpio_get_level(usb_mux->dp_mode_l))
		*mux_state |= MUX_DP_ENABLED;

	if (gpio_get_level(usb_mux->dp_polarity))
		*mux_state |= MUX_POLARITY_INVERTED;

	return EC_SUCCESS;
}

const struct usb_mux_driver board_custom_usb_mux_driver = {
	.init = board_init_usb_mux,
	.set = board_set_usb_mux,
	.get = board_get_usb_mux,
};

struct usb_mux usb_muxes[CONFIG_USB_PD_PORT_MAX_COUNT] = {
	{
		.driver = &board_custom_usb_mux_driver,
	},
	{
		.driver = &board_custom_usb_mux_driver,
	},
};
