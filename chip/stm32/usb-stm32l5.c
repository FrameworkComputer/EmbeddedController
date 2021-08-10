/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "registers.h"
#include "system.h"
#include "usb_api.h"

void usb_connect(void)
{
	/* USB is in use */
	disable_sleep(SLEEP_MASK_USB_DEVICE);

	STM32_USB_BCDR |= STM32_USB_BCDR_DPPU;
}

void usb_disconnect(void)
{
	/* disable pull-up on DP to disconnect */
	STM32_USB_BCDR &= ~STM32_USB_BCDR_DPPU;

	/* USB is off, so sleep whenever */
	enable_sleep(SLEEP_MASK_USB_DEVICE);
}
