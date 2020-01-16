/* Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * PS8802 retimer.
 */

#ifndef __CROS_EC_USB_RETIMER_PS8802_H
#define __CROS_EC_USB_RETIMER_PS8802_H

/*
 * PS8802 uses 7-bit I2C addresses 0x08 to 0x17 (ADDR=L).
 * Page 0 = 0x08, Page 1 = 0x09, Page 2 = 0x0A.
 * We only need to read and write the Mode Selection register in Page 2.
 */
#define PS8802_I2C_ADDR_FLAGS	0x0A

#define PS8802_REG_MODE		0x06
#define PS8802_MODE_DP_REG_CONTROL	BIT(7)
#define PS8802_MODE_DP_ENABLE		BIT(6)
#define PS8802_MODE_USB_REG_CONTROL	BIT(5)
#define PS8802_MODE_USB_ENABLE		BIT(4)
#define PS8802_MODE_FLIP_REG_CONTROL	BIT(3)
#define PS8802_MODE_FLIP_ENABLE		BIT(2)
#define PS8802_MODE_IN_HPD_REG_CONTROL	BIT(1)
#define PS8802_MODE_IN_HPD_ENABLE	BIT(0)

extern const struct usb_mux_driver ps8802_usb_mux_driver;
extern const struct usb_retimer_driver ps8802_usb_retimer;

int ps8802_detect(int port);

#endif /* __CROS_EC_USB_RETIMER_PS8802_H */
