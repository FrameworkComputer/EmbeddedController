/* Copyright 2016 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * USB HID definitions.
 */

#ifndef __CROS_EC_USB_HID_TOUCHPAD_H
#define __CROS_EC_USB_HID_TOUCHPAD_H

#include "common.h"

#ifdef __cplusplus
extern "C" {
#endif

#define USB_HID_TOUCHPAD_TIMESTAMP_UNIT 100 /* usec */

#define REPORT_ID_TOUCHPAD 0x01
#define REPORT_ID_DEVICE_CAPS 0x0A
#define REPORT_ID_DEVICE_CERT 0x0B

#define MAX_FINGERS 5

/* clang-format off */
#define FINGER_USAGE(max_pressure, logical_max_x, logical_max_y,       \
		     physical_max_x, physical_max_y)                   \
	0x05, 0x0D, /*   Usage Page (Digitizer) */                     \
	0x09, 0x22, /*   Usage (Finger) */                             \
	0xA1, 0x02, /*   Collection (Logical) */                       \
	0x09, 0x47, /*     Usage (Confidence) */                       \
	0x09, 0x42, /*     Usage (Tip Switch) */                       \
	0x09, 0x32, /*     Usage (In Range) */                         \
	0x15, 0x00, /*     Logical Minimum (0) */                      \
	0x25, 0x01, /*     Logical Maximum (1) */                      \
	0x75, 0x01, /*     Report Size (1) */                          \
	0x95, 0x03, /*     Report Count (3) */                         \
	0x81, 0x02, /*     Input (Data,Var,Abs) */                     \
	0x09, 0x51, /*     Usage (0x51) Contact identifier */          \
	0x75, 0x04, /*     Report Size (4) */                          \
	0x95, 0x01, /*     Report Count (1) */                         \
	0x25, 0x0F, /*     Logical Maximum (15) */                     \
	0x81, 0x02, /*     Input (Data,Var,Abs) */                     \
	0x05, 0x0D, /*     Usage Page (Digitizer) */                   \
		    /*     Logical Maximum of Pressure */              \
	0x26, ((max_pressure) & 0xFF), ((max_pressure) >> 8), 0x75,    \
	0x09,       /*     Report Size (9) */                          \
	0x09, 0x30, /*     Usage (Tip pressure) */                     \
	0x81, 0x02, /*     Input (Data,Var,Abs) */                     \
	0x26, 0xFF, 0x0F, /*     Logical Maximum (4095) */             \
	0x75, 0x0C, /*     Report Size (12) */                         \
	0x09, 0x48, /*     Usage (WIDTH) */                            \
	0x81, 0x02, /*     Input (Data,Var,Abs) */                     \
	0x09, 0x49, /*     Usage (HEIGHT) */                           \
	0x81, 0x02, /*     Input (Data,Var,Abs) */                     \
	0x05, 0x01, /*     Usage Page (Generic Desktop Ctrls) */       \
	0x75, 0x0C, /*     Report Size (12) */                         \
	0x55, 0x0E, /*     Unit Exponent (-2) */                       \
	0x65, 0x11, /*     Unit (System: SI Linear, Length: cm) */     \
	0x09, 0x30, /*     Usage (X) */                                \
	0x35, 0x00, /*     Physical Minimum (0) */                     \
		    /*     Logical Maximum */                          \
	0x26, ((logical_max_x) & 0xff), ((logical_max_x) >> 8),        \
		    /*     Physical Maximum (tenth of mm) */           \
	0x46, ((physical_max_x) & 0xff), ((physical_max_x) >> 8),      \
	0x81, 0x02, /*     Input (Data,Var,Abs) */                     \
		    /*     Logical Maximum */                          \
	0x26, ((logical_max_y) & 0xff), ((logical_max_y) >> 8),        \
		    /*     Physical Maximum (tenth of mm) */           \
	0x46, ((physical_max_y) & 0xff), ((physical_max_y) >> 8),      \
	0x09, 0x31, /*     Usage (Y) */                                \
	0x81, 0x02, /*     Input (Data,Var,Abs) */                     \
	0xC0 /*   End Collection */                                    \

#define REPORT_DESC(max_pressure, logical_max_x, logical_max_y,                \
		    physical_max_x, physical_max_y)                            \
	{                                                                      \
		/* Touchpad Collection */                                      \
		0x05, 0x0D, /* Usage Page (Digitizer) */                       \
		0x09, 0x05, /* Usage (Touch Pad) */                            \
		0xA1, 0x01, /* Collection (Application) */                     \
		0x85, REPORT_ID_TOUCHPAD, /*   Report ID (1, Touch) */         \
		/* Finger 0 */                                                 \
		FINGER_USAGE(max_pressure, logical_max_x, logical_max_y,       \
			     physical_max_x, physical_max_y),                  \
		/* Finger 1 */                                                 \
		FINGER_USAGE(max_pressure, logical_max_x, logical_max_y,       \
			     physical_max_x, physical_max_y),                  \
		/* Finger 2 */                                                 \
		FINGER_USAGE(max_pressure, logical_max_x, logical_max_y,       \
			     physical_max_x, physical_max_y),                  \
		/* Finger 3 */                                                 \
		FINGER_USAGE(max_pressure, logical_max_x, logical_max_y,       \
			     physical_max_x, physical_max_y),                  \
		/* Finger 4 */                                                 \
		FINGER_USAGE(max_pressure, logical_max_x, logical_max_y,       \
			     physical_max_x, physical_max_y),                  \
		/* Contact count */                                            \
		0x05, 0x0D, /*   Usage Page (Digitizer) */                     \
		0x09, 0x54, /*   Usage (Contact count) */                      \
		0x25, MAX_FINGERS, /*   Logical Maximum (MAX_FINGERS) */       \
		0x75, 0x07, /*   Report Size (7) */                            \
		0x95, 0x01, /*   Report Count (1) */                           \
		0x81, 0x02, /*   Input (Data,Var,Abs) */ /* Button */          \
		0x05, 0x01, /*   Usage Page(Generic Desktop Ctrls) */          \
		0x05, 0x09, /*   Usage (Button) */                             \
		0x19, 0x01, /*   Usage Minimum (0x01) */                       \
		0x29, 0x01, /*   Usage Maximum (0x01) */                       \
		0x15, 0x00, /*   Logical Minimum (0) */                        \
		0x25, 0x01, /*   Logical Maximum (1) */                        \
		0x75, 0x01, /*   Report Size (1) */                            \
		0x95, 0x01, /*   Report Count (1) */                           \
		0x81, 0x02, /*   Input (Data,Var,Abs) */ /* Timestamp */       \
		0x05, 0x0D, /*   Usage Page (Digitizer) */                     \
		0x55, 0x0C, /*   Unit Exponent (-4) */                         \
		0x66, 0x01, 0x10, /*   Unit (Seconds) */                       \
		0x47, 0xFF, 0xFF, 0x00, 0x00, /*   Physical Maximum (65535) */ \
		0x27, 0xFF, 0xFF, 0x00, 0x00, /*   Logical Maximum (65535) */  \
		0x75, 0x10, /*   Report Size (16) */                           \
		0x95, 0x01, /*   Report Count (1) */                           \
		0x09, 0x56, /*   Usage (0x56, Relative Scan Time) */           \
		0x81, 0x02, /*   Input (Data,Var,Abs) */                       \
									       \
		/* Report ID (Device Capabilities) */                          \
		0x85, REPORT_ID_DEVICE_CAPS,                                   \
		0x09, 0x55, /*   Usage (Contact Count Maximum) */              \
		0x09, 0x59, /*   Usage (Pad Type) */                           \
		0x25, 0x0F, /*   Logical Maximum (15) */                       \
		0x75, 0x08, /*   Report Size (8) */                            \
		0x95, 0x02, /*   Report Count (2) */                           \
		0xB1, 0x02, /*   Feature (Data,Var,Abs) */                     \
									       \
		/* Page 0xFF, usage 0xC5 is device certificate. */             \
		0x06, 0x00, 0xFF, /*   Usage Page (Vendor Defined) */          \
		/* Report ID (Device Certification) */                         \
		0x85, REPORT_ID_DEVICE_CERT,                                   \
		0x09, 0xC5, /*   Usage (Vendor Usage 0xC5) */                  \
		0x15, 0x00, /*   Logical Minimum (0) */                        \
		0x26, 0xFF, 0x00, /*   Logical Maximum (255) */                \
		0x75, 0x08, /*   Report Size (8) */                            \
		0x96, 0x00, 0x01, /*   Report Count (256) */                   \
		0xB1, 0x02, /*   Feature (Data,Var,Abs) */                     \
									       \
		0xC0, /* End Collection */                                     \
	}
/* clang-format on */

struct usb_hid_touchpad_report {
	uint8_t id; /* 0x01 */
	struct {
		uint16_t confidence : 1;
		uint16_t tip : 1;
		uint16_t inrange : 1;
		uint16_t id : 4;
		uint16_t pressure : 9;
		uint16_t width : 12;
		uint16_t height : 12;
		uint16_t x : 12;
		uint16_t y : 12;
	} __packed finger[MAX_FINGERS];
	uint8_t count : 7;
	uint8_t button : 1;
	uint16_t timestamp;
} __packed;

/* class implementation interfaces */
void set_touchpad_report(struct usb_hid_touchpad_report *report);

#ifdef __cplusplus
}
#endif

#endif
