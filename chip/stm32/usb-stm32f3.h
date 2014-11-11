/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * STM32F3 Family specific USB functionality
 */

/*
 * A device that uses an STM32F3 part will need to define these two functions
 * which are used to connect and disconnect the device from the USB bus.  This
 * is usually accomplished by enabling a pullup on the DP USB line.  The pullup
 * should be enabled by default so that the STM32 will enumerate correctly in
 * DFU mode (which doesn't know how to enable the DP pullup, so it assumes that
 * the pullup is always there).
 */
void usb_board_connect(void);
void usb_board_disconnect(void);
