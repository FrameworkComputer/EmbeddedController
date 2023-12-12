/*
 * Copyright 2018 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef __EC_EXTRA_USB_UPDATER_USB_IF_H
#define __EC_EXTRA_USB_UPDATER_USB_IF_H

#include <libusb.h>

/* This describes USB endpoint used to communicate with Cr50. */
struct usb_endpoint {
	struct libusb_device_handle *devh;
	uint8_t ep_num;
	int chunk_len;
};

/*
 * Find the requested USB endpoint. This finds the device using the device
 * serial number, vendor id, and product id. The subclass and protocol are used
 * to find the correct endpoint. If a matching endpoint is found, fill up the
 * uep structure. If succeeded, usb_shut_down() must be invoked before program
 * exits.
 *
 * Return 0 on success, -1 on failure.
 */
int usb_findit(const char *serialno, uint16_t vid, uint16_t pid,
	       uint16_t subclass, uint16_t protocol, struct usb_endpoint *uep);

/*
 * Actual USB transfer function, the 'allow_less' flag indicates that the
 * valid response could be shorter than allotted memory, the 'rxed_count'
 * pointer, if provided along with 'allow_less', lets the caller know how many
 * bytes were received.
 */
int usb_trx(struct usb_endpoint *uep, uint8_t *outbuf, int outlen,
	    uint8_t *inbuf, int inlen, int allow_less, size_t *rxed_count);

/*
 * This function should be called for graceful tear down of the USB interface
 * when the program exits, either normally or due to error. This is required
 * only after USB connection was established, i.e. after successful invocation
 * of usb_findit().
 */
void usb_shut_down(struct usb_endpoint *uep);

#define USB_ERROR(m, r)                                                        \
	fprintf(stderr, "%s:%d, %s returned %d (%s)\n", __FILE__, __LINE__, m, \
		r, libusb_strerror(r))

#endif /* ! __EC_EXTRA_USB_UPDATER_USB_IF_H */
