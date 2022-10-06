/* Copyright 2014 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * STM32F3 Family specific USB functionality
 */

#include "usb-stm32f3.h"

#include "system.h"
#include "usb_api.h"

void usb_connect(void)
{
	/* USB is in use */
	disable_sleep(SLEEP_MASK_USB_DEVICE);

	usb_board_connect();
}

void usb_disconnect(void)
{
	usb_board_disconnect();

	/* USB is off, so sleep whenever */
	enable_sleep(SLEEP_MASK_USB_DEVICE);
}
