/*
 * Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <libusb.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define CHECK(expression)					\
	({							\
		int error__ = (expression);			\
								\
		if (error__ != 0) {				\
			fprintf(stderr,				\
				"libusb error: %s:%d %s\n",	\
				__FILE__,			\
				__LINE__,			\
				libusb_error_name(error__));	\
			return error__;				\
		}						\
								\
		error__;					\
	})

#define TRANSFER_TIMEOUT_MS 100

static int gpio_write(libusb_device_handle *device,
		      uint32_t set_mask,
		      uint32_t clear_mask)
{
	uint8_t command[8];
	int     transfered;

	command[0] = (set_mask >>  0) & 0xff;
	command[1] = (set_mask >>  8) & 0xff;
	command[2] = (set_mask >> 16) & 0xff;
	command[3] = (set_mask >> 24) & 0xff;

	command[4] = (clear_mask >>  0) & 0xff;
	command[5] = (clear_mask >>  8) & 0xff;
	command[6] = (clear_mask >> 16) & 0xff;
	command[7] = (clear_mask >> 24) & 0xff;

	CHECK(libusb_bulk_transfer(device,
				   LIBUSB_ENDPOINT_OUT | 2,
				   command,
				   sizeof(command),
				   &transfered,
				   TRANSFER_TIMEOUT_MS));

	if (transfered != sizeof(command)) {
		fprintf(stderr,
			"Failed to transfer full command "
			"(sent %d of %d bytes)\n",
			transfered,
			(int)sizeof(command));
		return LIBUSB_ERROR_OTHER;
	}

	return 0;
}

static int gpio_read(libusb_device_handle *device, uint32_t *mask)
{
	uint8_t response[4];
	int     transfered;

	/*
	 * The first query does triggers the sampling of the GPIO values, the
	 * second query reads them back.
	 */
	CHECK(libusb_bulk_transfer(device,
				   LIBUSB_ENDPOINT_IN | 2,
				   response,
				   sizeof(response),
				   &transfered,
				   TRANSFER_TIMEOUT_MS));

	CHECK(libusb_bulk_transfer(device,
				   LIBUSB_ENDPOINT_IN | 2,
				   response,
				   sizeof(response),
				   &transfered,
				   TRANSFER_TIMEOUT_MS));

	if (transfered != sizeof(response)) {
		fprintf(stderr,
			"Failed to transfer full response "
			"(read %d of %d bytes)\n",
			transfered,
			(int)sizeof(response));
		return LIBUSB_ERROR_OTHER;
	}

	*mask = (response[0] <<  0 |
		 response[1] <<  8 |
		 response[2] << 16 |
		 response[3] << 24);

	return 0;
}

int main(int argc, char **argv)
{
	libusb_context       *context;
	libusb_device_handle *device;
	uint16_t              vendor_id  = 0x18d1;
	uint16_t              product_id = 0x500f;

	if (!(argc == 2 && strcmp(argv[1], "read")  == 0) &&
	    !(argc == 4 && strcmp(argv[1], "write") == 0)) {
		puts("Usage: usb_gpio read\n"
		     "       usb_gpio write <set_mask> <clear_mask>\n");
		return 1;
	}

	CHECK(libusb_init(&context));

	device = libusb_open_device_with_vid_pid(context,
						 vendor_id,
						 product_id);

	if (device == NULL) {
		fprintf(stderr,
			"Unable to find device 0x%04x:0x%04x\n",
			vendor_id,
			product_id);
		return 1;
	}

	CHECK(libusb_set_auto_detach_kernel_driver(device, 1));
	CHECK(libusb_claim_interface(device, 0));

	if (argc == 2 && strcmp(argv[1], "read") == 0) {
		uint32_t mask;

		CHECK(gpio_read(device, &mask));

		printf("GPIO mask: 0x%08x\n", mask);
	}

	if (argc == 4 && strcmp(argv[1], "write") == 0) {
		uint32_t set_mask   = strtol(argv[2], NULL, 0);
		uint32_t clear_mask = strtol(argv[3], NULL, 0);

		CHECK(gpio_write(device, set_mask, clear_mask));
	}

	libusb_close(device);
	libusb_exit(context);

	return 0;
}
