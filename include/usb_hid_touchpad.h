/* Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * USB HID definitions.
 */

#ifndef __CROS_EC_USB_HID_TOUCHPAD_H
#define __CROS_EC_USB_HID_TOUCHPAD_H

#define USB_HID_TOUCHPAD_TIMESTAMP_UNIT 100 /* usec */

struct usb_hid_touchpad_report {
	uint8_t id; /* 0x01 */
	struct {
		unsigned tip:1;
		unsigned inrange:1;
		unsigned id:4;
		unsigned pressure:10;
		unsigned width:12;
		unsigned height:12;
		unsigned x:12;
		unsigned y:12;
	} __packed finger[5];
	uint8_t count:7;
	uint8_t button:1;
	uint16_t timestamp;
} __packed;

/* class implementation interfaces */
void set_touchpad_report(struct usb_hid_touchpad_report *report);

#endif
