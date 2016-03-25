/*
 * Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <errno.h>
#include <getopt.h>
#include <libusb.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Options */
static uint16_t vid = 0x18d1;			/* Google */
static uint16_t pid = 0x5014;			/* Cr50 */

static char *progname;

static void usage(int errs)
{
	printf("\nUsage: %s [vid:pid] [value]\n"
	       "\n"
	       "Set/Get the USB Device Configuration value\n"
	       "\n"
	       "The default vid:pid is %04x:%04x\n"
	       "\n", progname, vid, pid);

	exit(!!errs);
}

/* Globals */
struct libusb_device_handle *devh = 0;

static void stupid_usb(const char *format, ...)
{
	va_list ap;

	va_start(ap, format);
	vfprintf(stderr, format, ap);
	va_end(ap);

	if (devh)
		libusb_close(devh);

	libusb_exit(NULL);

	exit(1);
}


int main(int argc, char *argv[])
{
	int r = 1;
	int errorcnt = 0;
	int do_set = 0;
	uint16_t setval = 0;
	uint8_t buf[80];			/* Arbitrary size */
	int i;

	progname = strrchr(argv[0], '/');
	if (progname)
		progname++;
	else
		progname = argv[0];

	opterr = 0;				/* quiet, you */
	while ((i = getopt(argc, argv, "")) != -1) {
		switch (i) {
		case 'h':
			usage(errorcnt);
			break;
		case 0:				/* auto-handled option */
			break;
		case '?':
			if (optopt)
				printf("Unrecognized option: -%c\n", optopt);
			else
				printf("Unrecognized option: %s\n",
				       argv[optind - 1]);
			errorcnt++;
			break;
		case ':':
			printf("Missing argument to %s\n", argv[optind - 1]);
			errorcnt++;
			break;
		default:
			printf("Internal error at %s:%d\n", __FILE__, __LINE__);
			exit(1);
		}
	}

	if (errorcnt)
		usage(errorcnt);

	if (optind < argc) {
		uint16_t v, p;

		if (2 == sscanf(argv[optind], "%hx:%hx", &v, &p)) {
			vid = v;
			pid = p;
			optind++;
		}
	}

	if (optind < argc) {
		do_set = 1;
		setval = atoi(argv[optind]);
	}

	r = libusb_init(NULL);
	if (r) {
		printf("libusb_init() returned 0x%x: %s\n",
		       r, libusb_error_name(r));
		return 1;
	}

	devh = libusb_open_device_with_vid_pid(NULL, vid, pid);
	if (!devh) {
		perror(progname);
		stupid_usb("Can't open device %04x:%04x\n", vid, pid);
	}


	/* Set config*/
	if (do_set) {
		printf("SetCfg %d\n", setval);
		r = libusb_control_transfer(
			devh,
			0x00,			/* bmRequestType */
			0x09,			/* bRequest */
			setval,			/* wValue */
			0x0000,			/* wIndex */
			NULL,			/* data */
			0x0000,			/* wLength */
			1000);			/* timeout (ms) */

		if (r < 0)
			printf("transfer returned 0x%x %s\n",
			       r, libusb_error_name(r));
	}

	/* Get config */
	memset(buf, 0, sizeof(buf));

	r = libusb_control_transfer(
		devh,
		0x80,				/* bmRequestType */
		0x08,				/* bRequest */
		0x0000,				/* wValue */
		0x0000,				/* wIndex */
		buf,				/* data */
		0x0001,				/* wLength */
		1000);				/* timeout (ms) */

	if (r <= 0)
		stupid_usb("GetCfg transfer() returned 0x%x %s\n",
			   r, libusb_error_name(r));

	printf("GetCfg returned %d bytes:", r);
	for (i = 0; i < r; i++)
		printf(" 0x%02x", buf[i]);
	printf("\n");

	/* done */
	if (devh)
		libusb_close(devh);
	libusb_exit(NULL);

	return 0;
}
