/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* USB charging control module for Chrome EC */

#ifndef __CROS_EC_USB_CHARGE_H
#define __CROS_EC_USB_CHARGE_H

#include "board.h"

enum usb_charge_mode {
	/* Disable USB port. */
	USB_CHARGE_MODE_DISABLED,
	/* Set USB port to be dedicated charging port, auto selecting charging
	 * schemes. */
	USB_CHARGE_MODE_CHARGE_AUTO,
	/* Set USB port to be dedicated charging port following USB Battery
	 * Charging Specification 1.2. */
	USB_CHARGE_MODE_CHARGE_BC12,
	/* Set USB port to be standard downstream port, with current limit set
	 * to 500mA or 1500mA. */
	USB_CHARGE_MODE_DOWNSTREAM_500MA,
	USB_CHARGE_MODE_DOWNSTREAM_1500MA,

	USB_CHARGE_MODE_COUNT
};

int usb_charge_set_mode(int usb_port_id, enum usb_charge_mode);

int usb_charge_init(void);

#endif  /* __CROS_EC_USB_CHARGE_H */
