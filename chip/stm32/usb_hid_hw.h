/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * USB HID HW definitions, to be used by class drivers.
 */

#ifndef __CROS_EC_USB_HID_HW_H
#define __CROS_EC_USB_HID_HW_H

/* internal callbacks for HID class drivers */
void hid_tx(int ep);
void hid_reset(int ep, usb_uint *hid_ep_tx_buf, int tx_len,
	       usb_uint *hid_ep_rx_buf, int rx_len);
int hid_iface_request(usb_uint *ep0_buf_rx, usb_uint *ep0_buf_tx,
		      const uint8_t *report_desc, int report_size,
		      const struct usb_hid_descriptor *hid_desc);

#endif
