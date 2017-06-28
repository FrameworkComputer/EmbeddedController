/* Copyright 2017 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "console.h"
#include "gpio.h"
#include "hooks.h"
#include "registers.h"
#include "signing.h"
#include "spi.h"
#include "system.h"
#include "timer.h"
#include "usb_spi.h"

#define CPRINTS(format, args...) cprints(CC_USB, format, ## args)

int usb_spi_board_enable(struct usb_spi_config const *config)
{
	spi_enable(CONFIG_SPI_FLASH_PORT, 1);

	/* Enable SPI framing for H1 bootloader */
	gpio_set_level(GPIO_SPI_CS_ALT_L, 0);

	return EC_SUCCESS;
}

void usb_spi_board_disable(struct usb_spi_config const *config)
{
	/* End SPI framing for H1 bootloader */
	gpio_set_level(GPIO_SPI_CS_ALT_L, 1);

	spi_enable(CONFIG_SPI_FLASH_PORT, 0);
}

int usb_spi_interface(struct usb_spi_config const *config,
		      struct usb_setup_packet *req)
{
	if (req->bmRequestType != (USB_DIR_OUT |
				    USB_TYPE_VENDOR |
				    USB_RECIP_INTERFACE))
		return 1;

	if ((req->wValue != 0 && req->wValue  != 1)  ||
	    req->wIndex  != config->interface ||
	    req->wLength != 0)
		return 1;

	if (!config->state->enabled_device)
		return 1;

	switch (req->bRequest) {
	case USB_SPI_REQ_ENABLE_H1:
		config->state->enabled_host = USB_SPI_H1;
		break;

	/* Set reset and DFU pins. Both active high. */
	case USB_SPI_REQ_RESET:
		gpio_set_level(GPIO_DUT_RST_L, !req->wValue);
		break;
	case USB_SPI_REQ_BOOT_CFG:
		gpio_set_level(GPIO_DUT_BOOT_CFG, req->wValue);
		break;
	/* Set socket power. */
	case USB_SPI_REQ_SOCKET:
		if (req->wValue)
			enable_socket();
		else
			disable_socket();
		break;
	case USB_SPI_REQ_SIGNING_START:
		sig_start(stream_spi);
		break;
	case USB_SPI_REQ_SIGNING_SIGN:
		sig_sign(stream_spi);
		break;
	case USB_SPI_REQ_ENABLE_AP:
	case USB_SPI_REQ_ENABLE:
		CPRINTS("ERROR: Must specify target");
	case USB_SPI_REQ_DISABLE:
		config->state->enabled_host = USB_SPI_DISABLE;
		break;

	default:
		return 1;
	}

	/*
	 * Our state has changed, call the deferred function to handle the
	 * state change.
	 */
	hook_call_deferred(config->deferred, 0);
	return 0;
}
