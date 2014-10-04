/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * USB billboard definitions.
 */

#ifndef USB_BB_H
#define USB_BB_H

/* per Billboard Device Class Spec Revision 1.0 */

/* device descriptor fields */
#define USB_BB_BCDUSB_MIN 0x0201 /* v2.01 minimum */
#define USB_BB_SUBCLASS 0x00
#define USB_BB_PROTOCOL 0x00
#define USB_BB_EP0_PACKET_SIZE 8
#define USB_BB_CAP_DESC_TYPE 0x0d


#define USB_BB_CAPS_SVID_SIZE 4
struct usb_bb_caps_svid_descriptor {
	uint16_t wSVID;
	uint8_t bAlternateMode;
	uint8_t iAlternateModeString;
} __packed;

#define USB_BB_CAPS_BASE_SIZE 44
struct usb_bb_caps_base_descriptor {
	uint8_t bLength;
	uint8_t bDescriptorType;
	uint8_t bDevCapabilityType;
	uint8_t iAdditionalInfoURL;
	uint8_t bNumberOfAlternateModes;
	uint8_t bPreferredAlternateMode;
	uint16_t VconnPower;
	uint8_t bmConfigured[32]; /* 2b per SVID w/ 128 SVIDs allowed. */
	uint32_t bReserved; /* SBZ */
} __packed;


#define USB_BB_VCONN_PWRON(x)    (x << 15)
#define USB_BB_VCONN_PWR_1W      0
#define USB_BB_VCONN_PWR_1p5W    1
#define USB_BB_VCONN_PWR_2W      2
#define USB_BB_VCONN_PWR_3W      3
#define USB_BB_VCONN_PWR_4W      4
#define USB_BB_VCONN_PWR_5W      5
#define USB_BB_VCONN_PWR_6W      6
/* Note, 7W (111b) is reserved */


#endif /* USB_BB_H */

