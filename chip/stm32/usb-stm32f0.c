/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * STM32F0 Family specific USB functionality
 */

#include "registers.h"
#include "usb_api.h"

void usb_connect(void)
{
	STM32_USB_BCDR |= (1 << 15) /* DPPU */;
}

void usb_disconnect(void)
{
	/* disable pull-up on DP to disconnect */
	STM32_USB_BCDR &= ~(1 << 15) /* DPPU */;
}
