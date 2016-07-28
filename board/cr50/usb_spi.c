/* Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "console.h"
#include "gpio.h"
#include "hooks.h"
#include "registers.h"
#include "spi.h"
#include "timer.h"
#include "usb_spi.h"

#define CPRINTS(format, args...) cprints(CC_USB, format, ## args)

static uint8_t update_in_progress;

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

	/* hold AP in reset */
	gpio_set_level(GPIO_SYS_RST_L_OUT, 0);
}

int usb_spi_update_in_progress(void)
{
	return update_in_progress;
}

static void update_finished(void)
{
	update_in_progress = 0;

	/*
	 * The AP and EC are reset in usb_spi_enable so the TPM is in a bad
	 * state. Assert SYS_RST_L to reset the state.
	 */
	ASSERT(GREAD(PINMUX, GPIO0_GPIO4_SEL) == GC_PINMUX_DIOM0_SEL);
	GWRITE(PINMUX, DIOM0_SEL, GC_PINMUX_GPIO0_GPIO4_SEL);
	gpio_set_flags(GPIO_SYS_RST_L_OUT, GPIO_OUT_HIGH);
	gpio_set_level(GPIO_SYS_RST_L_OUT, 0);
}
DECLARE_DEFERRED(update_finished);

void usb_spi_board_enable(struct usb_spi_config const *config)
{
	hook_call_deferred(&update_finished_data, -1);
	update_in_progress = 1;

	disable_spi();

	if (config->state->enabled_host == USB_SPI_EC)
		enable_ec_spi();
	else if (config->state->enabled_host == USB_SPI_AP)
		enable_ap_spi();
	else {
		CPRINTS("DEVICE NOT SUPPORTED");
		return;
	}

	/* Connect DIO A4, A8, and A14 to the SPI peripheral */
	GWRITE(PINMUX, DIOA4_SEL, 0); /* SPI_MOSI */
	GWRITE(PINMUX, DIOA8_SEL, 0); /* SPI_CS_L */
	GWRITE(PINMUX, DIOA14_SEL, 0); /* SPI_CLK */
	/* Set SPI_CS to be an internal pull up */
	GWRITE_FIELD(PINMUX, DIOA14_CTL, PU, 1);

	CPRINTS("usb_spi enable %s",
		gpio_get_level(GPIO_AP_FLASH_SELECT) ? "AP" : "EC");

	spi_enable(CONFIG_SPI_FLASH_PORT, 1);
}

void usb_spi_board_disable(struct usb_spi_config const *config)
{
	CPRINTS("usb_spi disable");
	spi_enable(CONFIG_SPI_FLASH_PORT, 0);
	disable_spi();

	/* Disconnect SPI peripheral to tri-state pads */
	/* Disable internal pull up */
	GWRITE_FIELD(PINMUX, DIOA14_CTL, PU, 0);
	/* TODO: Implement way to get the gpio */
	ASSERT(GREAD(PINMUX, GPIO0_GPIO7_SEL) == GC_PINMUX_DIOA4_SEL);
	ASSERT(GREAD(PINMUX, GPIO0_GPIO8_SEL) == GC_PINMUX_DIOA8_SEL);
	ASSERT(GREAD(PINMUX, GPIO0_GPIO9_SEL) == GC_PINMUX_DIOA14_SEL);

	/* Set SPI MOSI, CLK, and CS_L as inputs */
	GWRITE(PINMUX, DIOA4_SEL, GC_PINMUX_GPIO0_GPIO7_SEL);
	GWRITE(PINMUX, DIOA8_SEL, GC_PINMUX_GPIO0_GPIO8_SEL);
	GWRITE(PINMUX, DIOA14_SEL, GC_PINMUX_GPIO0_GPIO9_SEL);

	/*
	 * TODO(crosbug.com/p/52366): remove once sys_rst just resets the TPM
	 * instead of cr50.
	 * Resetting the EC and AP cause sys_rst to be asserted currently that
	 * will cause cr50 to do a soft reset. Delay the end of the transaction
	 * to prevent cr50 from resetting during a series of usb_spi calls.
	 */
	hook_call_deferred(&update_finished_data, 1 * SECOND);
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
