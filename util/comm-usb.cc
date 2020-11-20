/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <assert.h>
#include <endian.h>
#include <errno.h>
#include <libusb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "comm-host.h"
#include "comm-usb.h"
#include "ec_commands.h"
#include "misc_util.h"
#include "usb_descriptor.h"

#define USB_ERROR(m, r) print_libusb_error(__FILE__, __LINE__, m, r)

#ifdef DEBUG
#define debug(fmt, arg...) \
	fprintf(stderr, "%s:%d: " fmt, __FILE__, __LINE__, ##arg)
#else
#define debug(...)
#endif

struct usb_endpoint {
	struct libusb_device_handle *devh;
	int iface_num;
	uint8_t ep_num;
	int chunk_len;
};

struct usb_endpoint uep;

static void print_libusb_error(const char *file, int line, const char *message,
			       int error_code)
{
	/*
	 * TODO(b/247573723): Remove cast when libusb is upgraded.
	 */
	fprintf(stderr, "%s:%d, %s returned %d (%s)\n", file, line, message,
		error_code, libusb_strerror((enum libusb_error)error_code));
}

void comm_usb_exit(void)
{
	debug("Exit libusb.\n");

	if (uep.iface_num)
		libusb_release_interface(uep.devh, uep.iface_num);
	if (uep.devh)
		libusb_close(uep.devh);
	libusb_exit(NULL);

	memset(&uep, 0, sizeof(uep));
}

/*
 * Actual USB transfer function, <allow_less> indicates that a valid response
 * (e.g. EC_CMD_GET_BUILD_INFO) could be shorter than <inlen>.
 *
 * Returns enum libusb_error (< 0) or -EECRESULT on error. On success, returns
 * actually transferred OUT data size or IN data size if read is performed.
 */
static int do_xfer(struct usb_endpoint *uep, void *outbuf, int outlen,
		   void *inbuf, int inlen, int allow_less)
{
	int r, actual;

	/* Send data out */
	if (outbuf && outlen) {
		actual = 0;
		r = libusb_bulk_transfer(uep->devh, uep->ep_num,
					 (uint8_t *)outbuf, outlen, &actual,
					 2000);
		if (r != 0) {
			USB_ERROR("libusb_bulk_transfer", r);
			return r;
		}
		if (actual != outlen) {
			fprintf(stderr, "%s:%d, only sent %d/%d bytes\n",
				__FILE__, __LINE__, actual, outlen);
			return -EECRESULT;
		}
	}
	debug("Sent %d bytes, expecting to receive %d bytes.\n", outlen, inlen);

	/* Read reply back */
	if (inbuf && inlen) {
		actual = 0;
		/*
		 * libusb_bulk_transfer may time out if actual < inlen and
		 * actual is a multiple of ep->wMaxPacketSize.
		 */
		r = libusb_bulk_transfer(uep->devh, uep->ep_num | USB_DIR_IN,
					 (uint8_t *)inbuf, inlen, &actual,
					 5000);
		if (r != 0) {
			USB_ERROR("libusb_bulk_transfer", r);
			return r;
		}
		if ((actual != inlen) && !allow_less) {
			fprintf(stderr, "%s:%d, only received %d/%d bytes\n",
				__FILE__, __LINE__, actual, inlen);
			return -EECRESULT;
		}
	}

	debug("Received %d bytes.\n", actual);

	/* actual is useful for allow_less. */
	return actual;
}

/* Return iface # or -1 if not found. */
static int find_interface_with_endpoint(struct usb_endpoint *uep)
{
	struct libusb_device *dev;
	struct libusb_config_descriptor *conf = 0;
	const struct libusb_interface *iface0;
	const struct libusb_interface_descriptor *iface;
	const struct libusb_endpoint_descriptor *ep;
	int r, i, j, k;

	dev = libusb_get_device(uep->devh);
	r = libusb_get_active_config_descriptor(dev, &conf);
	if (r < 0) {
		USB_ERROR("Failed to get_active_config", r);
		return -1;
	}

	for (i = 0; i < conf->bNumInterfaces; i++) {
		iface0 = &conf->interface[i];
		for (j = 0; j < iface0->num_altsetting; j++) {
			iface = &iface0->altsetting[j];
			for (k = 0; k < iface->bNumEndpoints; k++) {
				ep = &iface->endpoint[k];
				if (ep->bEndpointAddress == uep->ep_num) {
					uep->chunk_len = ep->wMaxPacketSize;
					r = iface->bInterfaceNumber;
					libusb_free_config_descriptor(conf);
					return r;
				}
			}
		}
	}

	libusb_free_config_descriptor(conf);
	return -1;
}

int parse_vidpid(const char *input, uint16_t *vid_ptr, uint16_t *pid_ptr)
{
	char *copy, *s, *e;

	int ret = 1;

	copy = strdup(input);

	s = strchr(copy, ':');
	if (!s) {
		ret = 0;
		goto cleanup;
	}
	*s++ = '\0';

	e = NULL;
	*vid_ptr = strtoul(copy, &e, 16);
	if (e && *e) {
		ret = 0;
		goto cleanup;
	}

	e = NULL;
	*pid_ptr = strtoul(s, &e, 16);
	if (e && *e) {
		ret = 0;
		goto cleanup;
	}
cleanup:
	free(copy);
	return ret;
}

static libusb_device_handle *check_device(libusb_device *dev, uint16_t vid,
					  uint16_t pid, char *serialno)
{
	struct libusb_device_descriptor desc;
	libusb_device_handle *handle = NULL;
	char sn[256];
	int snvalid = 0;
	int r;

	r = libusb_get_device_descriptor(dev, &desc);
	if (r < 0)
		return NULL;

	r = libusb_open(dev, &handle);
	if (r < 0)
		return NULL;

	if (desc.iSerialNumber) {
		r = libusb_get_string_descriptor_ascii(handle,
						       desc.iSerialNumber,
						       (unsigned char *)sn,
						       sizeof(sn));
		if (r > 0)
			snvalid = 1;
	}

	if (vid != 0 && vid != desc.idVendor)
		return NULL;
	if (pid != 0 && pid != desc.idProduct)
		return NULL;
	if (serialno != NULL && (!snvalid || strstr(sn, serialno) == NULL))
		return NULL;

	return handle;
}

static int find_endpoint(uint16_t vid, uint16_t pid, char *serialno,
			 struct usb_endpoint *uep)
{
	int iface_num, r, i;
	libusb_device **devs;
	libusb_device_handle *devh = NULL;

	r = libusb_init(NULL);
	if (r < 0) {
		USB_ERROR("libusb_init", r);
		return -1;
	}

	r = libusb_get_device_list(NULL, &devs);
	if (r < 0) {
		USB_ERROR("No device is found.\n", r);
		return -1;
	}

	for (i = 0; devs[i]; i++) {
		devh = check_device(devs[i], vid, pid, serialno);
		if (devh) {
			debug("Found device.\n");
			break;
		}
	}

	libusb_free_device_list(devs, 1);

	if (!devh) {
		fprintf(stderr, "Can't find device\n");
		return -1;
	}

	uep->devh = devh;
	uep->ep_num = 2; /* USB_EP_HOSTCMD */

	iface_num = find_interface_with_endpoint(uep);
	if (iface_num < 0) {
		fprintf(stderr, "USB HOSTCMD not supported by that device\n");
		return -1;
	}

	if (!uep->chunk_len) {
		fprintf(stderr, "wMaxPacketSize isn't valid\n");
		return -1;
	}

	debug("Found interface %d endpoint=%d, chunk_len=%d\n", iface_num,
	      uep->ep_num, uep->chunk_len);

	libusb_set_auto_detach_kernel_driver(uep->devh, 1);
	r = libusb_claim_interface(uep->devh, iface_num);
	if (r < 0) {
		USB_ERROR("libusb_claim_interface", r);
		return -1;
	}
	uep->iface_num = iface_num;

	debug("READY\n-------\n");
	return 1;
}

static int sum_bytes(const void *data, int length)
{
	const uint8_t *bytes = (const uint8_t *)data;
	int sum = 0;
	int i;

	for (i = 0; i < length; i++)
		sum += bytes[i];
	return sum;
}

static int ec_command_usb(int command, int version, const void *outdata,
			  int outsize, void *indata, int insize)
{
	struct ec_host_request *req;
	struct ec_host_response *res;
	size_t req_len, res_len;
	int rv = -1;

	assert(outsize == 0 || outdata != NULL);
	assert(insize == 0 || indata != NULL);

	req_len = sizeof(*req) + outsize;
	req = (struct ec_host_request *)malloc(req_len);
	res_len = sizeof(*res) + insize;
	res = (struct ec_host_response *)malloc(res_len);
	if (req == NULL || res == NULL)
		goto out;

	req->struct_version = EC_HOST_REQUEST_VERSION; /* 3 */
	req->checksum = 0;
	req->command = command;
	req->command_version = version;
	req->reserved = 0;
	req->data_len = outsize;
	if (outdata)
		memcpy(&req[1], outdata, outsize);
	req->checksum = (uint8_t)(-sum_bytes(req, req_len));

	memset(res, 0, res_len);

	debug("Running command 0x%04x\n", command);
	rv = do_xfer(&uep, req, req_len, res, res_len, 1);
	if (rv < 0)
		goto out;

	if (indata)
		memcpy(indata, &res[1], insize);
	if (res->result == EC_RES_SUCCESS)
		rv = res->data_len;
	else
		rv = -EECRESULT - res->result;

out:
	if (req)
		free(req);
	if (res)
		free(res);

	return rv;
}

int comm_init_usb(uint16_t vid, uint16_t pid)
{
	debug("Initializing for %04x:%04x\n", vid, pid);

	memset(&uep, 0, sizeof(uep));

	if (find_endpoint(vid, pid, NULL, &uep) < 0)
		return -1;

	ec_command_proto = ec_command_usb;

	/* Set large size temporarily, will be updated (reduced) later. */
	ec_max_outsize = 0x400;
	ec_max_insize = 0x400;

	return 0;
}
