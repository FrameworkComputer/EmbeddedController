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

/* TODO(crosbug.com/p/38333) remove me */
#define GPIO_USBC_SS1_USB_MODE_L GPIO_USBC_MUX_CONF0
#define GPIO_USBC_SS2_USB_MODE_L GPIO_USBC_MUX_CONF1
#define GPIO_USBC_SS_EN_L GPIO_USBC_MUX_CONF2

static int p4_board_set_usb_mux(int port, mux_state_t mux_state)
{
	int polarity = mux_state & MUX_POLARITY_INVERTED;

	/* reset everything */
	gpio_set_level(GPIO_USBC_SS_EN_L, 1);
	gpio_set_level(GPIO_USBC_DP_MODE_L, 1);
	gpio_set_level(GPIO_USBC_DP_POLARITY, 1);
	gpio_set_level(GPIO_USBC_SS1_USB_MODE_L, 1);
	gpio_set_level(GPIO_USBC_SS2_USB_MODE_L, 1);

	if (!(mux_state & (MUX_USB_ENABLED | MUX_DP_ENABLED)))
		/* everything is already disabled, we can return */
		return EC_SUCCESS;

	if (mux_state & MUX_USB_ENABLED)
		/* USB 3.0 uses 2 superspeed lanes */
		gpio_set_level(polarity ? GPIO_USBC_SS2_USB_MODE_L :
					  GPIO_USBC_SS1_USB_MODE_L, 0);

	if (mux_state & MUX_DP_ENABLED) {
		/* DP uses available superspeed lanes (x2 or x4) */
		gpio_set_level(GPIO_USBC_DP_POLARITY, polarity);
		gpio_set_level(GPIO_USBC_DP_MODE_L, 0);
	}

	/* switch on superspeed lanes */
	gpio_set_level(GPIO_USBC_SS_EN_L, 0);

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

static int p4_board_get_usb_mux(int port, mux_state_t *mux_state)
{
	*mux_state = 0;

	if (!gpio_get_level(GPIO_USBC_SS1_USB_MODE_L) ||
	    !gpio_get_level(GPIO_USBC_SS2_USB_MODE_L))
		*mux_state |= MUX_USB_ENABLED;

	if (!gpio_get_level(GPIO_USBC_DP_MODE_L))
		*mux_state |= MUX_DP_ENABLED;

	if (gpio_get_level(GPIO_USBC_DP_POLARITY))
		*mux_state |= MUX_POLARITY_INVERTED;

	return EC_SUCCESS;
}

const struct usb_mux_driver p4_board_custom_usb_mux_driver = {
	.init = board_init_usb_mux,
	.set = p4_board_set_usb_mux,
	.get = p4_board_get_usb_mux,
};

const struct usb_mux_driver p5_board_custom_usb_mux_driver = {
	.init = board_init_usb_mux,
	.set = board_set_usb_mux,
	.get = board_get_usb_mux,
};

