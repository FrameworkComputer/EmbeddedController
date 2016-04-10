/*
 * Copyright 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <endian.h>
#include <getopt.h>
#include <libusb.h>
#include <openssl/sha.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "misc_util.h"
#include "usb_upgrade.h"
#include "config_chip.h"

/* Google Cr50 */
#define VID 0x18d1
#define PID 0x5014
#define SUBCLASS UNOFFICIAL_USB_SUBCLASS_GOOGLE_CR50
#define PROTOCOL 0xff

/* Globals */
static char *progname;
static char *short_opts = ":d:h";
static const struct option long_opts[] = {
	/* name    hasarg *flag val */
	{"device",   1,   NULL, 'd'},
	{"help",     0,   NULL, 'h'},
	{NULL,       0,   NULL, 0},
};

static void usage(int errs)
{
	printf("\nUsage: %s [options] ec.bin\n"
	       "\n"
	       "This updates the Cr50 RW firmware over USB.\n"
	       "The required argument is the full RO+RW image.\n"
	       "\n"
	       "Options:\n"
	       "\n"
	       "  -d,--device  VID:PID     USB device (default %04x:%04x)\n"
	       "  -h,--help                Show this message\n"
	       "\n", progname, VID, PID);

	exit(!!errs);
}

/* Read file into buffer */
static uint8_t *get_file_or_die(const char *filename, uint32_t *len_ptr)
{
	FILE *fp;
	struct stat st;
	uint8_t *data;
	uint32_t len;

	fp = fopen(filename, "rb");
	if (!fp) {
		perror(filename);
		exit(1);
	}
	if (fstat(fileno(fp), &st)) {
		perror("stat");
		exit(1);
	}

	len = st.st_size;

	data = malloc(len);
	if (!data) {
		perror("malloc");
		exit(1);
	}

	if (1 != fread(data, st.st_size, 1, fp)) {
		perror("fread");
		exit(1);
	}

	fclose(fp);

	*len_ptr = len;
	return data;
}

#define USB_ERROR(m, r) \
	fprintf(stderr, "%s:%d, %s returned %d (%s)\n", __FILE__, __LINE__, \
		m, r, libusb_strerror(r))

static void xfer(struct libusb_device_handle *devh, uint8_t ep_num,
		 void *outbuf, int outlen, void *inbuf, int inlen) {

	int r, actual;

	/* Send data out */
	if (outbuf && outlen) {
		actual = 0;
		r = libusb_bulk_transfer(devh, ep_num,
					 outbuf, outlen,
					 &actual, 1000);
		if (r < 0) {
			USB_ERROR("libusb_bulk_transfer", r);
			exit(1);
		}
		if (actual != outlen) {
			fprintf(stderr, "%s:%d, only sent %d/%d bytes\n",
				__FILE__, __LINE__, actual, outlen);
			exit(1);
		}
	}

	/* Read reply back */
	if (inbuf && inlen) {

		actual = 0;
		r = libusb_bulk_transfer(devh, ep_num | 0x80,
					 inbuf, inlen,
					 &actual, 1000);
		if (r < 0) {
			USB_ERROR("libusb_bulk_transfer", r);
			exit(1);
		}
		if (actual != inlen) {
			fprintf(stderr, "%s:%d, only received %d/%d bytes\n",
				__FILE__, __LINE__, actual, inlen);
			exit(1);
		}
	}
}


/* Return 0 on error, since it's never gonna be EP 0 */
static int find_endpoint(const struct libusb_interface_descriptor *iface,
			 uint8_t *ep_num_ptr, int *chunk_len_ptr)
{
	const struct libusb_endpoint_descriptor *ep;

	if (iface->bInterfaceClass == 255 &&
	    iface->bInterfaceSubClass == SUBCLASS &&
	    iface->bInterfaceProtocol == PROTOCOL &&
	    iface->bNumEndpoints) {
		ep = &iface->endpoint[0];
		*ep_num_ptr = (ep->bEndpointAddress & 0x7f);
		*chunk_len_ptr = ep->wMaxPacketSize;
		return 1;
	}

	return 0;
}

/* Return -1 on error */
static int find_interface(struct libusb_device_handle *devh,
			  uint8_t *ep_num_ptr, int *chunk_len_ptr)
{
	int iface_num = -1;
	int r, i, j;
	struct libusb_device *dev;
	struct libusb_config_descriptor *conf = 0;
	const struct libusb_interface *iface0;
	const struct libusb_interface_descriptor *iface;

	dev = libusb_get_device(devh);
	r = libusb_get_active_config_descriptor(dev, &conf);
	if (r < 0) {
		USB_ERROR("libusb_get_active_config_descriptor", r);
		goto out;
	}

	for (i = 0; i < conf->bNumInterfaces; i++) {
		iface0 = &conf->interface[i];
		for (j = 0; j < iface0->num_altsetting; j++) {
			iface = &iface0->altsetting[j];
			if (find_endpoint(iface, ep_num_ptr, chunk_len_ptr)) {
				iface_num = i;
				goto out;
			}
		}
	}

out:
	libusb_free_config_descriptor(conf);
	return iface_num;
}

/* Returns true if parsed. */
static int parse_vidpid(const char *input, uint16_t *vid_ptr, uint16_t *pid_ptr)
{
	char *copy, *s, *e = 0;

	copy = strdup(input);

	s = strchr(copy, ':');
	if (!s)
		return 0;
	*s++ = '\0';

	*vid_ptr = (uint16_t) strtoul(copy, &e, 16);
	if (!*optarg || (e && *e))
		return 0;

	*pid_ptr = (uint16_t) strtoul(s, &e, 16);
	if (!*optarg || (e && *e))
		return 0;

	return 1;
}


static struct libusb_device_handle *usb_connect(uint16_t vid, uint16_t pid,
						uint8_t *ep_num, int *chunk_len)
{
	struct libusb_device_handle *devh;
	int iface_num, r;

	r = libusb_init(NULL);
	if (r < 0) {
		USB_ERROR("libusb_init", r);
		exit(1);
	}

	printf("open_device %04x:%04x\n", vid, pid);
	/* NOTE: This doesn't handle multiple matches! */
	devh = libusb_open_device_with_vid_pid(NULL, vid, pid);
	if (!devh) {
		fprintf(stderr, "can't find device\n");
		exit(1);
	}

	iface_num = find_interface(devh, ep_num, chunk_len);
	if (iface_num < 0) {
		fprintf(stderr, "USB FW update not supported by that device\n");
		exit(1);
	}
	if (!chunk_len) {
		fprintf(stderr, "wMaxPacketSize isn't valid\n");
		exit(1);
	}

	printf("found interface %d endpoint %d, chunk_len %d\n",
	       iface_num, *ep_num, *chunk_len);

	libusb_set_auto_detach_kernel_driver(devh, 1);
	r = libusb_claim_interface(devh, iface_num);
	if (r < 0) {
		USB_ERROR("libusb_claim_interface", r);
		exit(1);
	}

	printf("READY\n-------\n");
	return devh;
}

#define SIGNED_TRANSFER_SIZE 1024
struct upgrade_command {
	uint32_t  block_digest;
	uint32_t  block_base;
};

struct update_pdu {
	uint32_t block_size; /* Total block size, include this field's size. */
	struct upgrade_command cmd;
	/* The actual payload goes here. */
};

#define FLASH_BASE 0x40000

static void transfer_and_reboot(struct libusb_device_handle *devh,
				uint8_t *data, uint32_t data_len,
				uint8_t ep_num, int chunk_len)
{
	uint32_t out;
	uint32_t reply;
	uint8_t *data_ptr;
	uint32_t next_offset;
	struct update_pdu updu;

	/* Send start/erase request */
	printf("erase\n");

	memset(&updu, 0, sizeof(updu));
	updu.block_size = htobe32(sizeof(updu));
	xfer(devh, ep_num, &updu, sizeof(updu), &reply, sizeof(reply));
/* check the offset here. */
	next_offset = be32toh(reply) - FLASH_BASE;
	printf("Updating at offset 0x%08x\n", next_offset);

	data_ptr = data + next_offset;
	data_len = CONFIG_RW_SIZE;

	/* Actually, we can skip trailing chunks of 0xff */
	while (data_len && (data_ptr[data_len - 1] == 0xff))
		data_len--;

	printf("sending 0x%x/0x%x bytes\n", data_len, CONFIG_RW_SIZE);

	while (data_len) {
		size_t payload_size;
		SHA_CTX ctx;
		uint8_t digest[SHA_DIGEST_LENGTH];

		uint8_t *transfer_data_ptr;
		size_t transfer_size;

		/* prepare the header to prepend to the block. */
		payload_size = MIN(data_len, SIGNED_TRANSFER_SIZE);
		updu.block_size = htobe32(payload_size +
					  sizeof(struct update_pdu));

		updu.cmd.block_base = htobe32(next_offset + FLASH_BASE);

		/* Calculate the digest. */
		SHA1_Init(&ctx);
		SHA1_Update(&ctx, &updu.cmd.block_base,
			    sizeof(updu.cmd.block_base));
		SHA1_Update(&ctx, data_ptr, payload_size);
		SHA1_Final(digest, &ctx);

		/* Copy the first few bytes. */
		memcpy(&updu.cmd.block_digest, digest,
		       sizeof(updu.cmd.block_digest));

		/* Now send the header. */
		xfer(devh, ep_num, &updu, sizeof(updu), NULL, 0);
		/* Now send the block, chunk by chunk. */
		transfer_data_ptr = data_ptr;
		for (transfer_size = 0; transfer_size < payload_size;) {
			int chunk_size;

			chunk_size = MIN(chunk_len,
					 payload_size - transfer_size);
			xfer(devh, ep_num, transfer_data_ptr, chunk_size,
			     NULL, 0);
			transfer_data_ptr += chunk_size;
			transfer_size += chunk_size;
		}

		/* Now get the reply. */
		xfer(devh, ep_num, NULL, 0, &reply, sizeof(reply));
		if (reply) {
			fprintf(stderr, "error: status %#08x remaining %#08x\n",
				be32toh(reply), data_len);
			exit(1);
		}
		/*
		if (!IS_EXPECT_RW(in.status)) {
			fprintf(stderr, "error: status 0x%08x offset 0x%08x\n",
				in.status, in.offset);
			exit(1);
		}
		*/
		/* Block transferred and programmed successfully. */
		/* Show status occasionally
		if (!(in.offset & 0x00003FFF))
			printf("offset 0x%x\n", in.offset);
		*/

		data_len -= payload_size;
		data_ptr += payload_size;
		next_offset += payload_size;
	}

	printf("-------\nupdate complete\n");

	/* Send stop request, ignorign reply. */
	out = htobe32(UPGRADE_DONE);
	xfer(devh, ep_num, &out, sizeof(out), &reply, sizeof(reply));

	printf("reboot\n");

	/* Send a second stop request, which should reboot without replying */
	xfer(devh, ep_num, &out, sizeof(out), 0, 0);
}

int main(int argc, char *argv[])
{
	struct libusb_device_handle *devh;
	int errorcnt;
	uint8_t *data = 0;
	uint32_t data_len = 0;
	uint8_t ep_num = 0;
	int chunk_len = 0;
	uint16_t vid = VID, pid = PID;
	int i;

	progname = strrchr(argv[0], '/');
	if (progname)
		progname++;
	else
		progname = argv[0];

	errorcnt = 0;
	opterr = 0;				/* quiet, you */
	while ((i = getopt_long(argc, argv, short_opts, long_opts, 0)) != -1) {
		switch (i) {
		case 'd':
			if (!parse_vidpid(optarg, &vid, &pid)) {
				printf("Invalid argument: \"%s\"\n", optarg);
				errorcnt++;
			}
			break;
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

	if (optind >= argc) {
		fprintf(stderr, "\nERROR: Missing required ec.bin file\n\n");
		usage(1);
	}

	data = get_file_or_die(argv[optind], &data_len);
	printf("read 0x%x bytes from %s\n", data_len, argv[optind]);
	if (data_len != CONFIG_FLASH_SIZE) {
		fprintf(stderr, "Image file is not %d bytes\n",
			CONFIG_FLASH_SIZE);
		exit(1);
	}

	devh = usb_connect(vid, pid, &ep_num, &chunk_len);

	transfer_and_reboot(devh, data, data_len, ep_num, chunk_len);

	printf("bye\n");
	free(data);
	libusb_close(devh);
	libusb_exit(NULL);

	return 0;
}
