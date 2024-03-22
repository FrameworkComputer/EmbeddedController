/*
 * Copyright 2018 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "usb_if.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Return 0 on error, since it's never gonna be EP 0 */
static int find_endpoint(const struct libusb_interface_descriptor *iface,
			 uint16_t subclass, uint16_t protocol,
			 struct usb_endpoint *uep)
{
	const struct libusb_endpoint_descriptor *ep;

	if (iface->bInterfaceClass == 255 &&
	    iface->bInterfaceSubClass == subclass &&
	    iface->bInterfaceProtocol == protocol && iface->bNumEndpoints) {
		ep = &iface->endpoint[0];
		uep->ep_num = ep->bEndpointAddress & 0x7f;
		uep->chunk_len = ep->wMaxPacketSize;
		return 1;
	}

	return 0;
}

/* Return -1 on error */
static int find_interface(uint16_t subclass, uint16_t protocol,
			  struct usb_endpoint *uep)
{
	int iface_num = -1;
	int r, i, j;
	struct libusb_device *dev;
	struct libusb_config_descriptor *conf = 0;
	const struct libusb_interface *iface0;
	const struct libusb_interface_descriptor *iface;

	dev = libusb_get_device(uep->devh);
	r = libusb_get_active_config_descriptor(dev, &conf);
	if (r < 0) {
		USB_ERROR("libusb_get_active_config_descriptor", r);
		goto out;
	}

	for (i = 0; i < conf->bNumInterfaces; i++) {
		iface0 = &conf->interface[i];
		for (j = 0; j < iface0->num_altsetting; j++) {
			iface = &iface0->altsetting[j];
			if (find_endpoint(iface, subclass, protocol, uep)) {
				iface_num = i;
				goto out;
			}
		}
	}

out:
	libusb_free_config_descriptor(conf);
	return iface_num;
}

static libusb_device_handle *check_device(libusb_device *dev, uint16_t vid,
					  uint16_t pid, const char *serial)
{
	struct libusb_device_descriptor desc;
	libusb_device_handle *handle = NULL;
	char sn[256];
	size_t sn_size = 0;

	if (libusb_get_device_descriptor(dev, &desc) < 0)
		return NULL;

	if (libusb_open(dev, &handle) != LIBUSB_SUCCESS)
		return NULL;

	if (desc.iSerialNumber && serial) {
		sn_size = libusb_get_string_descriptor_ascii(
			handle, desc.iSerialNumber, (unsigned char *)sn,
			sizeof(sn));
	}
	/*
	 * If the VID, PID, and serial number don't match, then it's not the
	 * correct device. Close the handle and return NULL.
	 */
	if ((vid && vid != desc.idVendor) || (pid && pid != desc.idProduct) ||
	    (serial &&
	     ((sn_size != strlen(serial)) || memcmp(sn, serial, sn_size)))) {
		libusb_close(handle);
		return NULL;
	}
	return handle;
}

int usb_findit(const char *serial, uint16_t vid, uint16_t pid,
	       uint16_t subclass, uint16_t protocol, struct usb_endpoint *uep)
{
	int iface_num, r, i;
	libusb_device **devs;
	libusb_device_handle *devh = NULL;
	ssize_t count;

	memset(uep, 0, sizeof(*uep));

	/* Must supply either serial or vendor and product ids. */
	if (!serial && !(vid && pid))
		goto terminate_usb_findit;

	r = libusb_init(NULL);
	if (r < 0) {
		USB_ERROR("libusb_init", r);
		goto terminate_usb_findit;
	}

	printf("finding_device ");
	if (vid)
		printf("%04x:%04x ", vid, pid);
	if (serial)
		printf("%s", serial);
	printf("\n");

	count = libusb_get_device_list(NULL, &devs);
	if (count < 0)
		goto terminate_usb_findit;

	for (i = 0; i < count; i++) {
		devh = check_device(devs[i], vid, pid, serial);
		if (devh) {
			printf("Found device.\n");
			break;
		}
	}

	libusb_free_device_list(devs, 1);

	if (!devh) {
		fprintf(stderr, "Can't find device\n");
		goto terminate_usb_findit;
	}
	uep->devh = devh;

	iface_num = find_interface(subclass, protocol, uep);
	if (iface_num < 0) {
		fprintf(stderr, "USB interface %d is not found\n", uep->ep_num);
		goto terminate_usb_findit;
	}
	if (!uep->chunk_len) {
		fprintf(stderr, "wMaxPacketSize isn't valid\n");
		goto terminate_usb_findit;
	}

	printf("found interface %d endpoint %d, chunk_len %d\n", iface_num,
	       uep->ep_num, uep->chunk_len);

	libusb_set_auto_detach_kernel_driver(uep->devh, 1);
	r = libusb_claim_interface(uep->devh, iface_num);
	if (r < 0) {
		USB_ERROR("libusb_claim_interface", r);
		goto terminate_usb_findit;
	}

	printf("READY\n-------\n");
	return 0;

terminate_usb_findit:
	if (uep->devh)
		usb_shut_down(uep);
	return -1;
}

int usb_trx(struct usb_endpoint *uep, uint8_t *outbuf, int outlen,
	    uint8_t *inbuf, int inlen, int allow_less, size_t *rxed_count)
{
	int r, actual;

	/* Send data out */
	if (outbuf && outlen) {
		actual = 0;
		r = libusb_bulk_transfer(uep->devh, uep->ep_num, outbuf, outlen,
					 &actual, 1000);
		if (r < 0) {
			USB_ERROR("libusb_bulk_transfer", r);
			return -1;
		}
		if (actual != outlen) {
			fprintf(stderr, "%s:%d, only sent %d/%d bytes\n",
				__FILE__, __LINE__, actual, outlen);
			usb_shut_down(uep);
		}
	}

	/* Read reply back */
	if (inbuf && inlen) {
		actual = 0;
		r = libusb_bulk_transfer(uep->devh, uep->ep_num | 0x80, inbuf,
					 inlen, &actual, 1000);
		if (r < 0) {
			USB_ERROR("libusb_bulk_transfer", r);
			return -1;
		}
		if ((actual != inlen) && !allow_less) {
			fprintf(stderr, "%s:%d, only received %d/%d bytes\n",
				__FILE__, __LINE__, actual, inlen);
			usb_shut_down(uep);
		}

		if (rxed_count)
			*rxed_count = actual;
	}

	return 0;
}

void usb_shut_down(struct usb_endpoint *uep)
{
	libusb_close(uep->devh);
	libusb_exit(NULL);
}
