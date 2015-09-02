/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Case Closed Debug common implementation
 */

#include "case_closed_debug.h"

#include "common.h"
#include "usb_api.h"
#include "usb_console.h"
#include "usb_spi.h"

#if !defined(CONFIG_USB)
#error "CONFIG_USB must be defined to use Case Closed Debugging"
#endif

#if !defined(CONFIG_USB_CONSOLE)
#error "CONFIG_USB_CONSOLE must be defined to use Case Closed Debugging"
#endif

#if !defined(CONFIG_USB_INHIBIT_INIT)
#error "CONFIG_USB_INHIBIT_INIT must be defined to use Case Closed Debugging"
#endif

#if defined(CONFIG_USB_SPI)
USB_SPI_CONFIG(ccd_usb_spi, USB_IFACE_SPI, USB_EP_SPI);
#endif

static enum ccd_mode current_mode = CCD_MODE_DISABLED;

void ccd_set_mode(enum ccd_mode new_mode)
{
	if (new_mode == current_mode)
		return;

	if (current_mode != CCD_MODE_DISABLED)
		usb_release();

	current_mode = new_mode;

	/*
	 * The forwarding of the local console over USB is read-only
	*  if we are not in the fully enabled mode.
	 */
	usb_console_enable(new_mode != CCD_MODE_DISABLED,
			   new_mode != CCD_MODE_ENABLED);

#if defined(CONFIG_USB_SPI)
	usb_spi_enable(&ccd_usb_spi, new_mode == CCD_MODE_ENABLED);
#endif

	if (new_mode != CCD_MODE_DISABLED)
		usb_init();
}
