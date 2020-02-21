/* Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * PI3DPX1207 retimer.
 */
#include "gpio.h"

#ifndef __CROS_EC_USB_RETIMER_PI3PDX1207_H
#define __CROS_EC_USB_RETIMER_PI3PDX1207_H

#define PI3DPX1207_I2C_ADDR_FLAGS	0x57
#define PI3DPX1207_NUM_REGISTERS	32

/* Register Offset 0 - Revision and Vendor ID */
#define PI3DPX1207_VID_OFFSET		0

#define PI3DPX1207B_VID				0x03
#define PI3DPX1207C_VID				0x13

/* Register Offset 1 - Device Type/ID */
#define PI3DPX1207_DID_OFFSET		1

#define PI3DPX1207_DID_ACTIVE_MUX		0x11

/* Register Offset 3 - Mode Control */
#define PI3DPX1207_MODE_OFFSET		3

#define PI3DPX1207_MODE_WATCHDOG_EN		0x02

#define PI3DPX1207B_MODE_GEN_APP_EN		0x08

#define PI3DPX1207_MODE_CONF_SAFE		0x00
#define PI3DPX1207_MODE_CONF_DP			0x20
#define PI3DPX1207_MODE_CONF_DP_FLIP		0x30
#define PI3DPX1207_MODE_CONF_USB		0x40
#define PI3DPX1207_MODE_CONF_USB_FLIP		0x50
#define PI3DPX1207_MODE_CONF_USB_DP		0x60
#define PI3DPX1207_MODE_CONF_USB_DP_FLIP	0x70
#define PI3DPX1207_MODE_CONF_USB_SUPER		0xC0

/* Supported USB retimer drivers */
extern const struct usb_mux_driver pi3dpx1207_usb_retimer;

/* Retimer driver hardware specific controls */
struct pi3dpx1207_usb_control {
	/* Retimer enable */
	const enum gpio_signal enable_gpio;
	/* DP Mode enable */
	const enum gpio_signal dp_enable_gpio;
};
extern const struct pi3dpx1207_usb_control pi3dpx1207_controls[];

#endif /* __CROS_EC_USB_RETIMER_PI3PDX1207_H */
