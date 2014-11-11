/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * STM32F3 Family specific USB functionality
 */

#include "usb-stm32f3.h"

#include "usb_api.h"

void usb_connect(void)
{
	usb_board_connect();
}

void usb_disconnect(void)
{
	usb_board_connect();
}
