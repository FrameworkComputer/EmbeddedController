/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * KB8010: Active redriver
 *
 * Public functions, definitions, and structures.
 */

#ifndef __CROS_EC_USB_RETIMER_KB8010_PUBLIC_H
#define __CROS_EC_USB_RETIMER_KB8010_PUBLIC_H

#include "usb_mux.h"

extern const struct usb_mux_driver kb8010_usb_retimer_driver;

/* Retimer driver hardware specific controls */
struct kb8010_control {
	/* Retimer reset */
	enum gpio_signal retimer_rst_gpio;
	/* DP enable */
	const enum gpio_signal dp_enable_gpio;
};
extern const struct kb8010_control kb8010_controls[];

#endif /* __CROS_EC_USB_RETIMER_KB8010_PUBLIC_H */
