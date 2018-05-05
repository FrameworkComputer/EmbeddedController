/* Copyright 2018 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __CROS_EC_USB_DWC_I2C_H
#define __CROS_EC_USB_DWC_I2C_H
#include "usb_i2c.h"

/* I2C over USB interface. This gets declared in usb_i2c.c */
extern struct dwc_usb_ep i2c_usb__ep_ctl;

#endif /* __CROS_EC_USB_DWC_I2C_H */
