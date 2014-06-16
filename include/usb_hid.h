/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * USB HID definitions.
 */

#ifndef USB_HID_H
#define USB_HID_H

#define USB_HID_SUBCLASS_BOOT     1
#define USB_HID_PROTOCOL_KEYBOARD 1
#define USB_HID_PROTOCOL_MOUSE    2

/* USB HID Class requests */
#define USB_HID_REQ_GET_REPORT    0x01
#define USB_HID_REQ_GET_IDLE      0x02
#define USB_HID_REQ_GET_PROTOCOL  0x03
#define USB_HID_REQ_SET_REPORT    0x09
#define USB_HID_REQ_SET_IDLE      0x0A
#define USB_HID_REQ_SET_PROTOCOL  0x0B

/* USB HID class descriptor types */
#define USB_HID_DT_HID            (USB_TYPE_CLASS | 0x01)
#define USB_HID_DT_REPORT         (USB_TYPE_CLASS | 0x02)
#define USB_HID_DT_PHYSICAL       (USB_TYPE_CLASS | 0x03)

struct usb_hid_class_descriptor {
	uint8_t  bDescriptorType;
	uint16_t wDescriptorLength;
} __packed;

struct usb_hid_descriptor {
	uint8_t  bLength;
	uint8_t  bDescriptorType;
	uint16_t bcdHID;
	uint8_t  bCountryCode;
	uint8_t  bNumDescriptors;
	struct usb_hid_class_descriptor desc[1];
} __packed;

/* class implementation interfaces */
void set_keyboard_report(uint64_t rpt);

#endif /* USB_H */
