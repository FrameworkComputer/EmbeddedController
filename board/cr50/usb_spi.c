/* Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "console.h"
#include "gpio.h"
#include "registers.h"
#include "spi.h"
#include "usb_spi.h"
#include "hooks.h"

#define CPRINTS(format, args...) cprints(CC_USB, format, ## args)

USB_SPI_CONFIG(usb_spi,
	       USB_IFACE_SPI,
	       USB_EP_SPI)

void disable_spi(void)
{
	/* Configure SPI GPIOs */
	gpio_set_level(GPIO_AP_FLASH_SELECT, 0);
	gpio_set_level(GPIO_EC_FLASH_SELECT, 0);

	/* Release AP and EC */
	GWRITE(RBOX, ASSERT_EC_RST, 0);
	gpio_set_level(GPIO_SYS_RST_L_OUT, 1);

	/* Set SYS_RST_L as an input otherwise cr50 will hold the AP in reset */
	gpio_set_flags(GPIO_SYS_RST_L_OUT, GPIO_INPUT);
}

void enable_ec_spi(void)
{
	/* Select EC flash */
	gpio_set_level(GPIO_AP_FLASH_SELECT, 0);
	gpio_set_level(GPIO_EC_FLASH_SELECT, 1);

	/* hold EC in reset */
	GWRITE(RBOX, ASSERT_EC_RST, 1);
}

void enable_ap_spi(void)
{
	/* Select AP flash */
	gpio_set_level(GPIO_AP_FLASH_SELECT, 1);
	gpio_set_level(GPIO_EC_FLASH_SELECT, 0);

	/* Set SYS_RST_L as an output */
	ASSERT(GREAD(PINMUX, GPIO0_GPIO4_SEL) == GC_PINMUX_DIOM0_SEL);
	GWRITE(PINMUX, DIOM0_SEL, GC_PINMUX_GPIO0_GPIO4_SEL);
	gpio_set_flags(GPIO_SYS_RST_L_OUT, GPIO_OUT_HIGH);

	/* hold EC in reset */
	gpio_set_level(GPIO_SYS_RST_L_OUT, 0);
}

void usb_spi_board_enable(struct usb_spi_config const *config)
{
	disable_spi();
	if (config->state->enabled_host == USB_SPI_EC)
		enable_ec_spi();
	else if (config->state->enabled_host == USB_SPI_AP)
		enable_ap_spi();
	else {
		CPRINTS("DEVICE NOT SUPPORTED");
		return;
	}

	CPRINTS("usb_spi enable %s",
		gpio_get_level(GPIO_AP_FLASH_SELECT) ? "AP" : "EC");

	spi_enable(CONFIG_SPI_FLASH_PORT, 1);
}

void usb_spi_board_disable(struct usb_spi_config const *config)
{
	CPRINTS("usb_spi disable");
	spi_enable(CONFIG_SPI_FLASH_PORT, 0);
	disable_spi();
}

int usb_spi_interface(struct usb_spi_config const *config,
		      struct usb_setup_packet *req)
{
	if (req->bmRequestType != (USB_DIR_OUT |
				    USB_TYPE_VENDOR |
				    USB_RECIP_INTERFACE))
		return 1;

	if (req->wValue  != 0 ||
	    req->wIndex  != config->interface ||
	    req->wLength != 0)
		return 1;

	if (!config->state->enabled_device)
		return 1;

	switch (req->bRequest) {
	case USB_SPI_REQ_ENABLE_AP:
		config->state->enabled_host = USB_SPI_AP;
		break;
	case USB_SPI_REQ_ENABLE_EC:
		config->state->enabled_host = USB_SPI_EC;
		break;
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

static int command_usb_spi(int argc, char **argv)
{
	if (argc > 1) {
		if (!strcasecmp("enable", argv[1]))
			usb_spi_enable(&usb_spi, 1);
		else if (!strcasecmp("disable", argv[1])) {
			usb_spi_enable(&usb_spi, 0);
			disable_spi();
		}
	}

	ccprintf("%sSPI %s\n",
		usb_spi.state->enabled_host == USB_SPI_AP ? "AP " :
		usb_spi.state->enabled_host == USB_SPI_EC ? "EC" : "",
		usb_spi.state->enabled_device ? "enabled" : "disabled");

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(usb_spi, command_usb_spi,
	"[enable|disable]",
	"Get/set the usb spi state",
	NULL);
