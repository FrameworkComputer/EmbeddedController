/* Copyright 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * USB HID HW definitions, to be used by class drivers.
 */

#ifndef __CROS_EC_USB_HID_HW_H
#define __CROS_EC_USB_HID_HW_H

#include <common.h>

struct usb_hid_config_t {
	const uint8_t *report_desc;
	int report_size;
	const struct usb_hid_descriptor *hid_desc;

	/*
	 * Handle USB HID Get_Report request, can be NULL if not supported.
	 *
	 * @param report_id: ID of the report being requested
	 * @param report_type: 0x1 (INPUT) / 0x2 (OUTPUT) / 0x3 (FEATURE)
	 * @param buffer_ptr: handler should set it to the pointer of buffer to
	 *     return.
	 * @param buffer_size: handler should set it to the size of returned
	 *     buffer.
	 */
	int (*get_report)(uint8_t report_id,
			  uint8_t report_type,
			  const uint8_t **buffer_ptr,
			  int *buffer_size);
};

/* internal callbacks for HID class drivers */
void hid_tx(int ep);
void hid_reset(int ep, usb_uint *hid_ep_tx_buf, int tx_len,
	       usb_uint *hid_ep_rx_buf, int rx_len);
int hid_iface_request(usb_uint *ep0_buf_rx, usb_uint *ep0_buf_tx,
		      const struct usb_hid_config_t *hid_config);

#endif
