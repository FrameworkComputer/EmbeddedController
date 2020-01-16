/* Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * PS8818 retimer.
 */

#ifndef __CROS_EC_USB_RETIMER_PS8818_H
#define __CROS_EC_USB_RETIMER_PS8818_H

#define PS8818_I2C_ADDR_FLAGS	0x28

#define PS8818_REG_FLIP		0x00
#define PS8818_FLIP_CONFIG	BIT(7)

#define PS8818_REG_MODE		0x01
#define PS8818_MODE_DP_ENABLE	BIT(7)
#define PS8818_MODE_USB_ENABLE	BIT(6)

extern const struct usb_retimer_driver ps8818_usb_retimer;

int ps8818_detect(int port);

#endif /* __CROS_EC_USB_RETIMER_PS8818_H */
