/* Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * USB HID definitions.
 */

#ifndef __CROS_EC_USB_HID_KEYBOARD_H
#define __CROS_EC_USB_HID_KEYBOARD_H

struct __attribute__((__packed__)) usb_hid_touchpad_report {
	uint8_t id; /* 0x01 */
	struct __attribute__((__packed__)) {
		uint8_t tip:1;
		uint8_t inrange:1;
		uint8_t id:6;
		unsigned width:12;
		unsigned height:12;
		unsigned x:12;
		unsigned y:12;
		uint8_t pressure;
	} finger[5];
	uint8_t count:7;
	uint8_t button:1;
};

/* class implementation interfaces */
void set_touchpad_report(struct usb_hid_touchpad_report *report);

#endif
