/* Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "case_closed_debug.h"
#include "console.h"
#include "gpio.h"
#include "hooks.h"
#include "registers.h"
#include "spi.h"
#include "system.h"
#include "timer.h"
#include "usb_spi.h"

#define CPRINTS(format, args...) cprints(CC_USB, format, ## args)

static void disable_ec_ap_spi(void)
{
	/* Configure SPI GPIOs */
	gpio_set_level(GPIO_AP_FLASH_SELECT, 0);
	gpio_set_level(GPIO_EC_FLASH_SELECT, 0);

	/* Release AP and EC */
	deassert_ec_rst();
	deassert_sys_rst();
}

static void enable_ec_spi(void)
{
	/* Select EC flash */
	gpio_set_level(GPIO_AP_FLASH_SELECT, 0);
	gpio_set_level(GPIO_EC_FLASH_SELECT, 1);

	/* Hold EC in reset. This will also hold the AP in reset. */
	assert_ec_rst();
}

static void enable_ap_spi(void)
{
	/* Select AP flash */
	gpio_set_level(GPIO_AP_FLASH_SELECT, 1);
	gpio_set_level(GPIO_EC_FLASH_SELECT, 0);

	/*
	 * On some systems SYS_RST_L is not level sensitive, so the only way to
	 * be sure we're holding the AP in reset is to hold the EC in reset.
	 */
	assert_ec_rst();
}

int usb_spi_board_enable(struct usb_spi_config const *config)
{
	disable_ec_ap_spi();

	if (config->state->enabled_host == USB_SPI_EC) {
		if (!ccd_is_cap_enabled(CCD_CAP_EC_FLASH)) {
			CPRINTS("EC SPI access denied");
			return EC_ERROR_ACCESS_DENIED;
		}
		enable_ec_spi();
	} else if (config->state->enabled_host == USB_SPI_AP) {
		if (!ccd_is_cap_enabled(CCD_CAP_AP_FLASH)) {
			CPRINTS("AP SPI access denied");
			return EC_ERROR_ACCESS_DENIED;
		}
		enable_ap_spi();
	} else {
		CPRINTS("DEVICE NOT SUPPORTED");
		return EC_ERROR_INVAL;
	}

	GWRITE_FIELD(PINMUX, DIOA4_CTL, PD, 0);    /* SPI_MOSI */
	GWRITE_FIELD(PINMUX, DIOA8_CTL, PD, 0);    /* SPI_CLK */

	/* Connect DIO A4, A8, and A14 to the SPI peripheral */
	GWRITE(PINMUX, DIOA4_SEL, 0); /* SPI_MOSI */
	GWRITE(PINMUX, DIOA8_SEL, 0); /* SPI_CS_L */
	GWRITE(PINMUX, DIOA14_SEL, 0); /* SPI_CLK */
	/* Set SPI_CS to be an internal pull up */
	GWRITE_FIELD(PINMUX, DIOA14_CTL, PU, 1);

	CPRINTS("usb_spi enable %s",
		gpio_get_level(GPIO_AP_FLASH_SELECT) ? "AP" : "EC");

	spi_enable(CONFIG_SPI_FLASH_PORT, 1);

	return EC_SUCCESS;
}

void usb_spi_board_disable(struct usb_spi_config const *config)
{
	CPRINTS("usb_spi disable");
	spi_enable(CONFIG_SPI_FLASH_PORT, 0);

	/* Disconnect SPI peripheral to tri-state pads */
	/* Disable internal pull up */
	GWRITE_FIELD(PINMUX, DIOA14_CTL, PU, 0);
	/* TODO: Implement way to get the gpio */
	ASSERT(GREAD(PINMUX, GPIO0_GPIO7_SEL) == GC_PINMUX_DIOA4_SEL);
	ASSERT(GREAD(PINMUX, GPIO0_GPIO8_SEL) == GC_PINMUX_DIOA8_SEL);
	ASSERT(GREAD(PINMUX, GPIO0_GPIO9_SEL) == GC_PINMUX_DIOA14_SEL);

	GWRITE_FIELD(PINMUX, DIOA4_CTL, PD, 1);    /* SPI_MOSI */
	GWRITE_FIELD(PINMUX, DIOA8_CTL, PD, 1);    /* SPI_CLK */

	/* Set SPI MOSI, CLK, and CS_L as inputs */
	GWRITE(PINMUX, DIOA4_SEL, GC_PINMUX_GPIO0_GPIO7_SEL);
	GWRITE(PINMUX, DIOA8_SEL, GC_PINMUX_GPIO0_GPIO8_SEL);
	GWRITE(PINMUX, DIOA14_SEL, GC_PINMUX_GPIO0_GPIO9_SEL);

	disable_ec_ap_spi();
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
		/* Fall through... */
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
