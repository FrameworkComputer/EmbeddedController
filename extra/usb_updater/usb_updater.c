/*
 * Copyright 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <asm/byteorder.h>
#include <endian.h>
#include <fcntl.h>
#include <getopt.h>
#include <libusb.h>
#include <openssl/sha.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>


#ifndef __packed
#define __packed __attribute__((packed))
#endif

#include "misc_util.h"
#include "usb_descriptor.h"
#include "upgrade_fw.h"
#include "config_chip.h"
#include "board.h"
#include "compile_time_macros.h"

#ifdef DEBUG
#define debug printf
#else
#define debug(fmt, args...)
#endif

/*
 * This file contains the source code of a Linux application used to update
 * CR50 device firmware.
 *
 * The CR50 firmware image consists of multiple sections, of interest to this
 * app are the RO and RW code sections, two of each. When firmware update
 * session is established, the CR50 device reports locations of backup RW and RO
 * sections (those not used by the device at the time of transfer).
 *
 * Based on this information this app carves out the appropriate sections form
 * the full CR50 firmware binary image and sends them to the device for
 * programming into flash. Once the new sections are programmed and the device
 * is restarted, the new RO and RW are used if they pass verification and are
 * logically newer than the existing sections.
 *
 * There are two ways to communicate with the CR50 device: USB and SPI (when
 * this app is running on a chromebook with the CR50 device). Originally
 * different protocols were used to communicate over different channels,
 * starting with version 3 the same protocol is used.
 *
 * This app provides backwards compatibility to ensure that earlier CR50
 * devices still can be updated.
 *
 *
 * The host (either a local AP or a workstation) is the master of the firmware
 * update protocol, it sends data to the cr50 device, which proceeses it and
 * responds.
 *
 * The encapsultation format is different between the SPI and USB cases:
 *
 *   4 bytes      4 bytes         4 bytes               variable size
 * +-----------+--------------+---------------+----------~~--------------+
 * + total size| block digest |  dest address |           data           |
 * +-----------+--------------+---------------+----------~~--------------+
 *  \           \                                                       /
 *   \           \                                                     /
 *    \           +-------- FW update PDU sent over SPI --------------+
 *     \                                                             /
 *      +--------- USB frame, requires total size field ------------+
 *
 * The update protocol data unints (PDUs) are passed over SPI, the
 * encapsulation includes integritiy verification and destination address of
 * the data (more of this later). SPI transactions pretty much do not have
 * size limits, whereas the USB data is sent in chunks of the size determined
 * when the USB connestion is set up. This is why USB requires an additional
 * encapsulation int frames to communicate the PDU size to the client side so
 * that the PDU can be reassembled before passing to the programming function.
 *
 * In general, the protocol consists of two phases: connection establishment
 * and actual image transfer.
 *
 * The very first PDU of the transfer session is used to establish the
 * connection. The first PDU does not have any data, and the dest. address
 * field is set to zero. Receiving such a PDU signals the programming function
 * that the host intends to transfer a new image.
 *
 * The response to the first PDU varies depending on the protocol version.
 *
 * Version 1 is used over SPI. The response is either 4 or 1 bytes in size.
 * The 4 byte response is the *base address* of the backup RW section, no
 * support for RO updates. The one byte response is an error indication,
 * possibly reporting flash erase failure, command format error, etc.
 *
 * Version 2 is used over USB. The response is 8 bytes in size. The first four
 * bytes are either the *base address* of the backup RW section (still no RO
 * updates), or an error code, the same as in Version 1. The second 4 bytes
 * are the protocol version number (set to 2).
 *
 * Version 3 is used over both USB and SPI. The response is 16 bytes in size.
 * The first 4 bytes are the error code, the second 4 bytes are the protocol
 * version (set to 3) and then 4 byte *offset* of the RO section followed by
 * the 4 byte *offset* of the RW section.
 *
 * Once the connection is established, the image to be programmed into flash
 * is transferred to the CR50 in 1K PDUs. In versions 1 and 2 the address in
 * the header is the absolute address to place the block to, in version 3 and
 * later it is the offset into the flash.
 *
 * The CR50 device responds to each PDU with a confirmation which is 4 bytes
 * in size in protocol version 2, and 1 byte in size in all other versions.
 * Zero value means succes, non zero value is the error code reported by CR50.
 */

/* Look for Cr50 FW update interface */
#define VID USB_VID_GOOGLE
#define PID CONFIG_USB_PID
#define SUBCLASS USB_SUBCLASS_GOOGLE_CR50
#define PROTOCOL USB_PROTOCOL_GOOGLE_CR50_NON_HC_FW_UPDATE

#define FLASH_BASE 0x40000

/*
 * Need to create an entire TPM PDU when upgrading over /dev/tpm0 and need to
 * have space to prepare the entire PDU.
 */
struct upgrade_pkt {
	__be16	tag;
	__be32	length;
	__be32	ordinal;
	__be16	subcmd;
	__be32	digest;
	__be32	address;
	char data[0];
} __packed;

#define SIGNED_TRANSFER_SIZE 1024
#define MAX_BUF_SIZE	(SIGNED_TRANSFER_SIZE + sizeof(struct upgrade_pkt))
#define EXT_CMD		0xbaccd00a
#define FW_UPGRADE	4

struct usb_endpoint {
	struct libusb_device_handle *devh;
	uint8_t ep_num;
	int     chunk_len;
};

struct transfer_descriptor {
	int update_ro;			/* True if RO update is required. */
	uint32_t ro_offset;
	uint32_t rw_offset;

	enum transfer_type {
		usb_xfer = 0,
		spi_xfer = 1
	} ep_type;
	union {
		struct usb_endpoint uep;
		int tpm_fd;
	};
};

static uint32_t protocol_version;
static char *progname;
static char *short_opts = ":d:hrs";
static const struct option long_opts[] = {
	/* name    hasarg *flag val */
	{"device",   1,   NULL, 'd'},
	{"help",     0,   NULL, 'h'},
	{"ro",       0,   NULL, 'r'},
	{"spi",      0,   NULL, 's'},
	{NULL,       0,   NULL,  0},
};

/* Prepare and transfer a block to /dev/tpm0, get a reply. */
static int tpm_send_pkt(int fd, unsigned int digest, unsigned int addr,
			const void *data, int size,
			void *response, size_t *response_size)
{
	/* Used by transfer to /dev/tpm0 */
	static uint8_t outbuf[MAX_BUF_SIZE];

	struct upgrade_pkt *out = (struct upgrade_pkt *)outbuf;
	/* Use the same structure, it will not be filled completely. */
	int len, done;
	int response_offset = offsetof(struct upgrade_pkt, digest);

	debug("%s: sending to %#x %d bytes\n", __func__, addr, size);

	len = size + sizeof(struct upgrade_pkt);

	out->tag = __cpu_to_be16(0x8001);
	out->length = __cpu_to_be32(len);
	out->ordinal = __cpu_to_be32(EXT_CMD);
	out->subcmd = __cpu_to_be16(FW_UPGRADE);
	out->digest = digest;
	out->address = __cpu_to_be32(addr);
	memcpy(out->data, data, size);
#ifdef DEBUG
	{
		int i;

		debug("Writing %d bytes to TPM at %x\n", len, addr);
		for (i = 0; i < 20; i++)
			debug("%2.2x ", outbuf[i]);
		debug("\n");
	}
#endif
	done = write(fd, out, len);
	if (done < 0) {
		perror("Could not write to TPM");
		return -1;
	} else if (done != len) {
		fprintf(stderr, "Error: Wrote %d bytes, expected to write %d\n",
			done, len);
		return -1;
	}

	/*
	 * Let's reuse the output buffer as the receve buffer; the combined
	 * size of the two structures below is sure enough for any expected
	 * response size.
	 */
	len = read(fd, outbuf, sizeof(struct upgrade_pkt) +
		   sizeof(struct first_response_pdu));
#ifdef DEBUG
	debug("Read %d bytes from TPM\n", len);
	if (len > 0) {
		int i;

		for (i = 0; i < len; i++)
			debug("%2.2x ", outbuf[i]);
		debug("\n");
	}
#endif
	len = len - response_offset;
	if (len < 0) {
		fprintf(stderr, "Problems reading from TPM, got %d bytes\n",
			len + response_offset);
		return -1;
	}

	len = MIN(len, *response_size);
	memcpy(response, outbuf + response_offset, len);
	*response_size = len;
	return 0;
}

/* Release USB device and return error to the OS. */
static void shut_down(struct usb_endpoint *uep)
{
	libusb_close(uep->devh);
	libusb_exit(NULL);
	exit(1);
}

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
	       "  -r,--ro                  Update RO section along with RW\n"
	       "  -s,--spi                 Use /dev/tmp0 (-d is ignored)\n"
	       "\n", progname, VID, PID);

	exit(!!errs);
}

/* Read file into buffer */
static uint8_t *get_file_or_die(const char *filename, size_t *len_ptr)
{
	FILE *fp;
	struct stat st;
	uint8_t *data;
	size_t len;

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

/*
 * Actual USB transfer function, the 'allow_less' flag indicates that the
 * valid response could be shortef than allotted memory, the 'rxed_count'
 * pointer, if provided along with 'allow_less' lets the caller know how mavy
 * bytes were received.
 */
static void do_xfer(struct usb_endpoint *uep, void *outbuf, int outlen,
		    void *inbuf, int inlen, int allow_less,
		    size_t *rxed_count)
{

	int r, actual;

	/* Send data out */
	if (outbuf && outlen) {
		actual = 0;
		r = libusb_bulk_transfer(uep->devh, uep->ep_num,
					 outbuf, outlen,
					 &actual, 1000);
		if (r < 0) {
			USB_ERROR("libusb_bulk_transfer", r);
			exit(1);
		}
		if (actual != outlen) {
			fprintf(stderr, "%s:%d, only sent %d/%d bytes\n",
				__FILE__, __LINE__, actual, outlen);
			shut_down(uep);
		}
	}

	/* Read reply back */
	if (inbuf && inlen) {

		actual = 0;
		r = libusb_bulk_transfer(uep->devh, uep->ep_num | 0x80,
					 inbuf, inlen,
					 &actual, 1000);
		if (r < 0) {
			USB_ERROR("libusb_bulk_transfer", r);
			exit(1);
		}
		if ((actual != inlen) && !allow_less) {
			fprintf(stderr, "%s:%d, only received %d/%d bytes\n",
				__FILE__, __LINE__, actual, inlen);
			shut_down(uep);
		}

		if (rxed_count)
			*rxed_count = actual;
	}
}

static void xfer(struct usb_endpoint *uep, void *outbuf,
		 size_t outlen, void *inbuf, size_t inlen)
{
	do_xfer(uep, outbuf, outlen, inbuf, inlen, 0, NULL);
}

/* Return 0 on error, since it's never gonna be EP 0 */
static int find_endpoint(const struct libusb_interface_descriptor *iface,
			 struct usb_endpoint *uep)
{
	const struct libusb_endpoint_descriptor *ep;

	if (iface->bInterfaceClass == 255 &&
	    iface->bInterfaceSubClass == SUBCLASS &&
	    iface->bInterfaceProtocol == PROTOCOL &&
	    iface->bNumEndpoints) {
		ep = &iface->endpoint[0];
		uep->ep_num = ep->bEndpointAddress & 0x7f;
		uep->chunk_len = ep->wMaxPacketSize;
		return 1;
	}

	return 0;
}

/* Return -1 on error */
static int find_interface(struct usb_endpoint *uep)
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
			if (find_endpoint(iface, uep)) {
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


static void usb_findit(uint16_t vid, uint16_t pid, struct usb_endpoint *uep)
{
	int iface_num, r;

	memset(uep, 0, sizeof(*uep));

	r = libusb_init(NULL);
	if (r < 0) {
		USB_ERROR("libusb_init", r);
		exit(1);
	}

	printf("open_device %04x:%04x\n", vid, pid);
	/* NOTE: This doesn't handle multiple matches! */
	uep->devh = libusb_open_device_with_vid_pid(NULL, vid, pid);
	if (!uep->devh) {
		fprintf(stderr, "can't find device\n");
		exit(1);
	}

	iface_num = find_interface(uep);
	if (iface_num < 0) {
		fprintf(stderr, "USB FW update not supported by that device\n");
		shut_down(uep);
	}
	if (!uep->chunk_len) {
		fprintf(stderr, "wMaxPacketSize isn't valid\n");
		shut_down(uep);
	}

	printf("found interface %d endpoint %d, chunk_len %d\n",
	       iface_num, uep->ep_num, uep->chunk_len);

	libusb_set_auto_detach_kernel_driver(uep->devh, 1);
	r = libusb_claim_interface(uep->devh, iface_num);
	if (r < 0) {
		USB_ERROR("libusb_claim_interface", r);
		shut_down(uep);
	}

	printf("READY\n-------\n");
}

struct update_pdu {
	uint32_t block_size; /* Total block size, include this field's size. */
	struct upgrade_command cmd;
	/* The actual payload goes here. */
};

static int transfer_block(struct usb_endpoint *uep, struct update_pdu *updu,
			  uint8_t *transfer_data_ptr, size_t payload_size)
{
	size_t transfer_size;
	uint32_t reply;
	int actual;
	int r;

	/* First send the header. */
	xfer(uep, updu, sizeof(*updu), NULL, 0);

	/* Now send the block, chunk by chunk. */
	for (transfer_size = 0; transfer_size < payload_size;) {
		int chunk_size;

		chunk_size = MIN(uep->chunk_len, payload_size - transfer_size);
		xfer(uep, transfer_data_ptr, chunk_size, NULL, 0);
		transfer_data_ptr += chunk_size;
		transfer_size += chunk_size;
	}

	/* Now get the reply. */
	r = libusb_bulk_transfer(uep->devh, uep->ep_num | 0x80,
				 (void *) &reply, sizeof(reply),
				 &actual, 1000);
	if (r) {
		if ((r == -7) && (protocol_version >= 2)) {
			fprintf(stderr, "Timeout!\n");
			return r;
		}
		USB_ERROR("libusb_bulk_transfer", r);
		shut_down(uep);
	}

	if (protocol_version > 2)
		reply = *((uint8_t *)&reply);
	else
		reply = be32toh(reply);

	if (reply) {
		fprintf(stderr, "error: status %#x\n", reply);
		exit(1);
	}

	return 0;
}

/**
 * Transfer an image section (typically RW or RO).
 *
 * td           - transfer descriptor to use to communicate with the target
 * data_ptr     - pointer at the section base in the image
 * section_addr - address of the section in the target memory space
 * data_len     - section size
 */
static void transfer_section(struct transfer_descriptor *td,
			     uint8_t *data_ptr,
			     uint32_t section_addr,
			     size_t data_len)
{
	/*
	 * Actually, we can skip trailing chunks of 0xff, as the entire
	 * section space must be erased before the update is attempted.
	 */
	while (data_len && (data_ptr[data_len - 1] == 0xff))
		data_len--;

	printf("sending 0x%zx bytes to %#x\n", data_len, section_addr);
	while (data_len) {
		size_t payload_size;
		SHA_CTX ctx;
		uint8_t digest[SHA_DIGEST_LENGTH];
		int max_retries;
		struct update_pdu updu;

		/* prepare the header to prepend to the block. */
		payload_size = MIN(data_len, SIGNED_TRANSFER_SIZE);
		updu.block_size = htobe32(payload_size +
					  sizeof(struct update_pdu));

		if (protocol_version <= 2)
			updu.cmd.block_base = htobe32(section_addr +
						      FLASH_BASE);
		else
			updu.cmd.block_base = htobe32(section_addr);

		/* Calculate the digest. */
		SHA1_Init(&ctx);
		SHA1_Update(&ctx, &updu.cmd.block_base,
			    sizeof(updu.cmd.block_base));
		SHA1_Update(&ctx, data_ptr, payload_size);
		SHA1_Final(digest, &ctx);

		/* Copy the first few bytes. */
		memcpy(&updu.cmd.block_digest, digest,
		       sizeof(updu.cmd.block_digest));
		if (td->ep_type == usb_xfer) {
			for (max_retries = 10; max_retries; max_retries--)
				if (!transfer_block(&td->uep, &updu,
						    data_ptr, payload_size))
					break;

			if (!max_retries) {
				fprintf(stderr,
					"Failed to transfer block, %zd to go\n",
					data_len);
				exit(1);
			}
		} else {
			uint8_t error_code[4];
			size_t rxed_size = sizeof(error_code);
			uint32_t block_addr;

			if (protocol_version <= 2)
				block_addr = section_addr + FLASH_BASE;
			else
				block_addr = section_addr;

			/*
			 * A single byte response is expected, but let's give
			 * the driver a few extra bytes to catch cases when a
			 * different amount of data is transferred (which
			 * would indicate a synchronization problem).
			 */
			if (tpm_send_pkt(td->tpm_fd,
					 updu.cmd.block_digest,
					 block_addr,
					 data_ptr,
					 payload_size, error_code,
					 &rxed_size) < 0) {
				fprintf(stderr,
					"Failed to trasfer block, %zd to go\n",
					data_len);
				exit(1);
			}
			if (rxed_size != 1) {
				fprintf(stderr, "Unexpected return size %zd\n",
					rxed_size);
				exit(1);
			}

			if (error_code[0]) {
				fprintf(stderr, "error %d\n", error_code[0]);
				exit(1);
			}
		}
		data_len -= payload_size;
		data_ptr += payload_size;
		section_addr += payload_size;
	}
}

static void setup_connection(struct transfer_descriptor *td)
{
	size_t rxed_size;
	uint32_t error_code;

	/*
	 * Need to be backwards compatible, communicate with targets running
	 * different protocol versions.
	 */
	union {
		struct first_response_pdu rpdu;
		uint32_t legacy_resp;
	} start_resp;

	/* Send start/erase request */
	printf("erase\n");

	if (td->ep_type == usb_xfer) {
		struct update_pdu updu;

		memset(&updu, 0, sizeof(updu));
		updu.block_size = htobe32(sizeof(updu));
		do_xfer(&td->uep, &updu, sizeof(updu), &start_resp,
			sizeof(start_resp), 1, &rxed_size);
	} else {
		rxed_size = sizeof(start_resp);
		if (tpm_send_pkt(td->tpm_fd, 0, 0, NULL, 0,
				 &start_resp, &rxed_size) < 0) {
			fprintf(stderr, "Failed to start transfer\n");
			exit(1);
		}
	}

	if (rxed_size <= 4) {
		if (td->ep_type != spi_xfer) {
			fprintf(stderr, "Unexpected response size %zd\n",
				rxed_size);
			exit(1);
		}

		/* This is a protocol version one response. */
		protocol_version = 1;
		if (rxed_size == 1) {
			/* Target is reporting an error. */
			error_code = *((uint8_t *) &start_resp);
		} else {
			/* Target reporting RW base_address. */
			td->rw_offset = be32toh(start_resp.legacy_resp) -
				FLASH_BASE;
			error_code = 0;
		}
	} else {
		protocol_version = be32toh(start_resp.rpdu.protocol_version);
		error_code = be32toh(start_resp.rpdu.return_value);

		if (protocol_version == 2) {
			if (error_code > 256) {
				td->rw_offset = error_code - FLASH_BASE;
				error_code = 0;
			}
		} else {
			/* All newer protocols. */
			td->rw_offset = be32toh
				(start_resp.rpdu.vers3.backup_rw_offset);
		}
	}

	printf("Target running protocol version %d\n", protocol_version);

	if (!error_code) {
		if (protocol_version > 2) {
			td->ro_offset = be32toh
				(start_resp.rpdu.vers3.backup_ro_offset);
			printf("Offsets: backup RO at %#x, backup RW at %#x\n",
			       td->ro_offset, td->rw_offset);
			return;
		}
		if (!td->update_ro)
			return;

		fprintf(stderr, "Target does not support RO updates\n");

	} else {
		fprintf(stderr, "Target reporting error %d\n", error_code);
	}

	if (td->ep_type == usb_xfer)
		shut_down(&td->uep);
	exit(1);
}

static void transfer_and_reboot(struct transfer_descriptor *td,
				uint8_t *data, size_t data_len)
{

	setup_connection(td);

	transfer_section(td, data + td->rw_offset, td->rw_offset,
			 CONFIG_RW_SIZE);

	/* Transfer the RO part if requested. */
	if (td->update_ro)
		transfer_section(td, data + td->ro_offset, td->ro_offset,
				 CONFIG_RO_SIZE);

	printf("-------\nupdate complete\n");
	if (td->ep_type == usb_xfer) {
		uint32_t out;

		/* Send stop request, ignoring reply. */
		out = htobe32(UPGRADE_DONE);
		xfer(&td->uep, &out, sizeof(out), &out,
		     protocol_version < 3 ? sizeof(out) : 1);

		printf("reboot\n");

		/*
		 * Send a second stop request, which should reboot without
		 * replying.
		 */
		xfer(&td->uep, &out, sizeof(out), 0, 0);
	}
}

int main(int argc, char *argv[])
{
	struct transfer_descriptor td;
	int errorcnt;
	uint8_t *data = 0;
	size_t data_len = 0;
	uint16_t vid = VID, pid = PID;
	int i;

	progname = strrchr(argv[0], '/');
	if (progname)
		progname++;
	else
		progname = argv[0];

	/* Usb transfer - default mode. */
	memset(&td, 0, sizeof(td));
	td.ep_type = usb_xfer;

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
		case 'r':
			td.update_ro = 1;
			break;
		case 's':
			td.ep_type = spi_xfer;
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
	printf("read 0x%zx bytes from %s\n", data_len, argv[optind]);
	if (data_len != CONFIG_FLASH_SIZE) {
		fprintf(stderr, "Image file is not %d bytes\n",
			CONFIG_FLASH_SIZE);
		exit(1);
	}

	if (td.ep_type == usb_xfer) {
		usb_findit(vid, pid, &td.uep);
	} else {
		td.tpm_fd = open("/dev/tpm0", O_RDWR);
		if (td.tpm_fd < 0) {
			perror("Could not open TPM");
			exit(1);
		}
	}

	transfer_and_reboot(&td, data, data_len);

	printf("bye\n");
	free(data);
	if (td.ep_type == usb_xfer) {
		libusb_close(td.uep.devh);
		libusb_exit(NULL);
	}

	return 0;
}
