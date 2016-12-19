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

#include "config_chip.h"
#include "board.h"

#include "compile_time_macros.h"
#include "misc_util.h"
#include "signed_header.h"
#include "tpm_vendor_cmds.h"
#include "upgrade_fw.h"
#include "usb_descriptor.h"

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
 * All versions above 2 behave the same over SPI and USB.
 *
 * Version 3 response is 16 bytes in size. The first 4 bytes are the error code
 * the second 4 bytes are the protocol version (set to 3) and then 4 byte
 * *offset* of the RO section followed by the 4 byte *offset* of the RW section.
 *
 * Version 4 response in addition to version 3 provides header revision fields
 * for active RO and RW images running on the target.
 *
 * Once the connection is established, the image to be programmed into flash
 * is transferred to the CR50 in 1K PDUs. In versions 1 and 2 the address in
 * the header is the absolute address to place the block to, in version 3 and
 * later it is the offset into the flash.
 *
 * Protocol version 5 includes RO and RW key ID information into the first PDU
 * response. The key ID could be used to tell between prod and dev signing
 * modes, among other things.
 *
 * Protocol version 6 does not change the format of the first PDU response,
 * but it indicates the target's ablitiy to channel TPM venfor commands
 * through USB connection.
 *
 * When channeling TPM vendor commands the USB frame looks as follows:
 *
 *   4 bytes      4 bytes         4 bytes       2 bytes      variable size
 * +-----------+--------------+---------------+-----------+------~~~-------+
 * + total size| block digest |    EXT_CMD    | Vend. sub.|      data      |
 * +-----------+--------------+---------------+-----------+------~~~-------+
 *
 * Where 'Vend. sub' is the vendor subcommand, and data field is subcommand
 * dependent. The target tells between update PDUs and encapsulated vendor
 * subcommands by looking at the EXT_CMD value - it is set to 0xbaccd00a and
 * as such is guaranteed not to be a valid update PDU destination address.
 *
 * The vendor command response size is not fixed, it is subcommand dependent.
 *
 * The CR50 device responds to each update PDU with a confirmation which is 4
 * bytes in size in protocol version 2, and 1 byte in size in all other
 * versions. Zero value means success, non zero value is the error code
 * reported by CR50.
 *
 * Again, vendor command responses are subcommand specific.
 */

/* Look for Cr50 FW update interface */
#define VID USB_VID_GOOGLE
#define PID CONFIG_USB_PID
#define SUBCLASS USB_SUBCLASS_GOOGLE_CR50
#define PROTOCOL USB_PROTOCOL_GOOGLE_CR50_NON_HC_FW_UPDATE

#define FLASH_BASE 0x40000

enum exit_values {
	noop = 0,	  /* All up to date, no update needed. */
	all_updated = 1,  /* Update completed, reboot required. */
	rw_updated  = 2,  /* RO was not updated, reboot required. */
	update_error = 3  /* Something went wrong. */
};

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

struct usb_endpoint {
	struct libusb_device_handle *devh;
	uint8_t ep_num;
	int     chunk_len;
};

struct transfer_descriptor {
	/*
	 * Set to true for use in an upstart script. Do not reboot after
	 * transfer, and do not transfer RW if versions are the same.
	 *
	 * When using in development environment it is beneficial to transfer
	 * RW images with the same version, as they get started based on the
	 * header timestamp.
	 */
	uint32_t upstart_mode;

	/*
	 * offsets of RO and WR sections available for update (not currently
	 * active).
	 */
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
static char *short_opts = ":bcfd:hsu";
static const struct option long_opts[] = {
	/* name    hasarg *flag val */
	{"binvers",  0,   NULL, 'b'},
	{"corrupt",  0,   NULL, 'c'},
	{"device",   1,   NULL, 'd'},
	{"help",     0,   NULL, 'h'},
	{"spi",      0,   NULL, 's'},
	{"upstart",  0,   NULL, 'u'},
	{"fwver",    0,   NULL, 'f'},
	{NULL,       0,   NULL,  0},
};

/* Prepare and transfer a block to /dev/tpm0, get a reply. */
static int tpm_send_pkt(int fd, unsigned int digest, unsigned int addr,
			const void *data, int size,
			void *response, size_t *response_size,
			uint16_t subcmd)
{
	/* Used by transfer to /dev/tpm0 */
	static uint8_t outbuf[MAX_BUF_SIZE];

	struct upgrade_pkt *out = (struct upgrade_pkt *)outbuf;
	/* Use the same structure, it will not be filled completely. */
	int len, done;
	int response_offset = offsetof(struct upgrade_pkt, digest);

	debug("%s: sending to %#x %d bytes\n", __func__, addr, size);

	len = size + sizeof(struct upgrade_pkt);

	out->tag = htobe16(0x8001);
	out->length = htobe32(len);
	if (subcmd <= LAST_EXTENSION_COMMAND)
		out->ordinal = htobe32(CONFIG_EXTENSION_COMMAND);
	else
		out->ordinal = htobe32(TPM_CC_VENDOR_BIT_MASK);
	out->subcmd = htobe16(subcmd);
	out->digest = digest;
	out->address = htobe32(addr);
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
	exit(update_error);
}

static void usage(int errs)
{
	printf("\nUsage: %s [options] <binary image>\n"
	       "\n"
	       "This updates the Cr50 RW firmware over USB.\n"
	       "The required argument is the full RO+RW image.\n"
	       "\n"
	       "Options:\n"
	       "\n"
	       "  -b,--binvers             Report versions of image's "
				"RW and RO headers, do not update\n"
	       "  -c,--corrupt             Corrupt the inactive rw.\n"
	       "  -d,--device  VID:PID     USB device (default %04x:%04x)\n"
	       "  -f,--fwver               Report running firmware versions.\n"
	       "  -h,--help                Show this message\n"
	       "  -s,--spi                 Use /dev/tmp0 (-d is ignored)\n"
	       "  -u,--upstart             "
			"Upstart mode (strict header checks)\n"
	       "\n", progname, VID, PID);

	exit(errs ? update_error : noop);
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
		exit(update_error);
	}
	if (fstat(fileno(fp), &st)) {
		perror("stat");
		exit(update_error);
	}

	len = st.st_size;

	data = malloc(len);
	if (!data) {
		perror("malloc");
		exit(update_error);
	}

	if (1 != fread(data, st.st_size, 1, fp)) {
		perror("fread");
		exit(update_error);
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
			exit(update_error);
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
			exit(update_error);
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
		exit(update_error);
	}

	printf("open_device %04x:%04x\n", vid, pid);
	/* NOTE: This doesn't handle multiple matches! */
	uep->devh = libusb_open_device_with_vid_pid(NULL, vid, pid);
	if (!uep->devh) {
		fprintf(stderr, "Can't find device\n");
		exit(update_error);
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
		fprintf(stderr, "Error: status %#x\n", reply);
		exit(update_error);
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
				exit(update_error);
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
					 &rxed_size,
					 EXTENSION_FW_UPGRADE) < 0) {
				fprintf(stderr,
					"Failed to trasfer block, %zd to go\n",
					data_len);
				exit(update_error);
			}
			if (rxed_size != 1) {
				fprintf(stderr, "Unexpected return size %zd\n",
					rxed_size);
				exit(update_error);
			}

			if (error_code[0]) {
				fprintf(stderr, "Error %d\n", error_code[0]);
				exit(update_error);
			}
		}
		data_len -= payload_size;
		data_ptr += payload_size;
		section_addr += payload_size;
	}
}

/* Information about the target */
static struct first_response_pdu targ;

/*
 * Each RO or RW section of the new image can be in one of the following
 * states.
 */
enum upgrade_status {
	not_needed = 0,  /* Version below or equal that on the target. */
	not_possible,    /*
			  * RO is newer, but can't be transferred due to
			  * target RW shortcomings.
			  */
	needed            /*
			   * This section needs to be transferred to the
			   * target.
			   */
};

/* This array describes all four sections of the new image. */
static struct {
	const char *name;
	uint32_t    offset;
	uint32_t    size;
	enum upgrade_status  ustatus;
	struct signed_header_version shv;
	uint32_t keyid;
} sections[] = {
	{"RO_A", CONFIG_RO_MEM_OFF, CONFIG_RO_SIZE},
	{"RW_A", CONFIG_RW_MEM_OFF, CONFIG_RW_SIZE},
	{"RO_B", CHIP_RO_B_MEM_OFF, CONFIG_RO_SIZE},
	{"RW_B", CONFIG_RW_B_MEM_OFF, CONFIG_RW_SIZE}
};

/*
 * Scan the new image and retrieve versions of all four sections, two RO and
 * two RW.
 */
static void fetch_header_versions(const void *image)
{
	size_t i;

	for (i = 0; i < ARRAY_SIZE(sections); i++) {
		const struct SignedHeader *h;

		h = (const struct SignedHeader *)((uintptr_t)image +
						  sections[i].offset);
		sections[i].shv.epoch = h->epoch_;
		sections[i].shv.major = h->major_;
		sections[i].shv.minor = h->minor_;
		sections[i].keyid = h->keyid;
	}
}


/* Compare to signer headers and determine which one is newer. */
static int a_newer_than_b(struct signed_header_version *a,
			  struct signed_header_version *b)
{
	uint32_t fields[][3] = {
		{a->epoch, a->major, a->minor},
		{b->epoch, b->major, b->minor},
	};
	size_t i;

	/*
	 * Even though header version fields are 32 bits in size, we don't
	 * exepect any version field ever exceed say 1000. Anything in excess
	 * of 1000 should is considered zero.
	 *
	 * This would cover old images where one of the RO version fields is
	 * the number of git patches since last tag (and is in excess of
	 * 4000), and images where there is no code in a section (all fields
	 * are set to 0xffffffff).
	 */
	for (i = 0; i < ARRAY_SIZE(fields[0]); i++) {
		uint32_t a_value;
		uint32_t b_value;

		a_value = fields[0][i];
		b_value = fields[1][i];

		if (a_value > 4000)
			a_value = 0;

		if (b_value > 4000)
			b_value = 0;

		if (a_value != b_value)
			return a_value > b_value;
	}

	return 0;	/* All else being equal A is no newer than B. */
}
/*
 * Pick sections to transfer based on information retrieved from the target,
 * the new image, and the protocol version the target is running.
 */
static void pick_sections(struct transfer_descriptor *td)
{
	size_t i;

	for (i = 0; i < ARRAY_SIZE(sections); i++) {
		uint32_t offset = sections[i].offset;

		if ((offset == CONFIG_RW_MEM_OFF) ||
		    (offset == CONFIG_RW_B_MEM_OFF)) {

			/* Skip currently active section. */
			if (offset != td->rw_offset)
				continue;
			/*
			 * Ok, this would be the RW section to transfer to the
			 * device. Is it newer in the new image than the
			 * running RW section on the device?
			 *
			 * If not in 'upstart' mode - transfer even if
			 * versions are the same, timestamps could be
			 * different.
			 */

			if (a_newer_than_b(&sections[i].shv, &targ.shv[1]) ||
			    !td->upstart_mode)
				sections[i].ustatus = needed;
			continue;
		}

		/*
		 * RO update not supported in versions below 3, another
		 * invocation will be required once the RW is updated to
		 * handle protocol 3 or above.
		 */
		if (protocol_version < 3) {
			sections[i].ustatus = not_possible;
			continue;
		}

		/* Skip currently active section. */
		if (offset != td->ro_offset)
			continue;
		/*
		 * Ok, this would be the RO section to transfer to the device.
		 * Is it newer in the new image than the running RO section on
		 * the device?
		 */
		if (a_newer_than_b(&sections[i].shv, &targ.shv[0]))
			sections[i].ustatus = needed;
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

	/* Send start request. */
	printf("start\n");

	if (td->ep_type == usb_xfer) {
		struct update_pdu updu;

		memset(&updu, 0, sizeof(updu));
		updu.block_size = htobe32(sizeof(updu));
		do_xfer(&td->uep, &updu, sizeof(updu), &start_resp,
			sizeof(start_resp), 1, &rxed_size);
	} else {
		rxed_size = sizeof(start_resp);
		if (tpm_send_pkt(td->tpm_fd, 0, 0, NULL, 0,
				 &start_resp, &rxed_size,
				 EXTENSION_FW_UPGRADE) < 0) {
			fprintf(stderr, "Failed to start transfer\n");
			exit(update_error);
		}
	}

	/* We got something. Check for errors in response */

	if (rxed_size <= 4) {
		if (td->ep_type != spi_xfer) {
			size_t i;

			fprintf(stderr, "Unexpected response size %zd: ",
				rxed_size);

			for (i = 0; i < rxed_size; i++)
				fprintf(stderr, " %02x",
					((uint8_t *)&start_resp)[i]);
			fprintf(stderr, "\n");
			exit(update_error);
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
				(start_resp.rpdu.backup_rw_offset);

			if (protocol_version > 3) {
				size_t i;

				/* Running header versions are available. */
				for (i = 0; i < ARRAY_SIZE(targ.shv); i++) {
					targ.shv[i].minor = be32toh
						(start_resp.rpdu.shv[i].minor);
					targ.shv[i].major = be32toh
						(start_resp.rpdu.shv[i].major);
					targ.shv[i].epoch = be32toh
						(start_resp.rpdu.shv[i].epoch);
				}
			}
			if (protocol_version > 4) {
				size_t i;

				for (i = 0; i < ARRAY_SIZE(targ.keyid); i++)
					targ.keyid[i] = be32toh
						(start_resp.rpdu.keyid[i]);
			}
		}
	}

	printf("target running protocol version %d\n", protocol_version);

	if (!error_code) {
		if (protocol_version > 2) {
			td->ro_offset = be32toh
				(start_resp.rpdu.backup_ro_offset);
			printf("offsets: backup RO at %#x, backup RW at %#x\n",
			       td->ro_offset, td->rw_offset);
		}
		if (protocol_version > 4)
			printf("keyids: RO 0x%08x, RW 0x%08x\n",
			       targ.keyid[0], targ.keyid[1]);
		pick_sections(td);
		return;
	}

	fprintf(stderr, "Target reporting error %d\n", error_code);
	if (td->ep_type == usb_xfer)
		shut_down(&td->uep);
	exit(update_error);
}

/*
 * Channel TPM extension/vendor command over USB. The payload of the USB frame
 * in this case consists of the 2 byte subcommand code concatenated with the
 * command body. The caller needs to indicate if a response is expected, and
 * if it is - of what maximum size.
 */
static int ext_cmd_over_usb(struct usb_endpoint *uep, uint16_t subcommand,
			    void *cmd_body, size_t body_size,
			    void *resp, size_t *resp_size)
{
	struct update_frame_header *ufh;
	uint16_t *frame_ptr;
	size_t usb_msg_size;
	SHA_CTX ctx;
	uint8_t digest[SHA_DIGEST_LENGTH];

	usb_msg_size = sizeof(struct update_frame_header) +
		sizeof(subcommand) + body_size;

	ufh = malloc(usb_msg_size);
	if (!ufh) {
		printf("%s: failed to allocate %zd bytes\n",
		       __func__, usb_msg_size);
		return -1;
	}

	ufh->block_size = htobe32(usb_msg_size);
	ufh->cmd.block_base = htobe32(CONFIG_EXTENSION_COMMAND);
	frame_ptr = (uint16_t *)(ufh + 1);
	*frame_ptr = htobe16(subcommand);

	if (body_size)
		memcpy(frame_ptr + 1, cmd_body, body_size);

	/* Calculate the digest. */
	SHA1_Init(&ctx);
	SHA1_Update(&ctx, &ufh->cmd.block_base,
		    usb_msg_size -
		    offsetof(struct update_frame_header, cmd.block_base));
	SHA1_Final(digest, &ctx);
	memcpy(&ufh->cmd.block_digest, digest, sizeof(ufh->cmd.block_digest));
	xfer(uep, ufh, usb_msg_size, resp, resp_size ? *resp_size : 0);

	free(ufh);
	return 0;
}

/*
 * Indicate to the target that update image transfer has been completed. Upon
 * receiveing of this message the target state machine transitions into the
 * 'rx_idle' state. The host may send an extension command to reset the target
 * after this.
 */
static void send_done(struct usb_endpoint *uep)
{
	uint32_t out;

	/* Send stop request, ignoring reply. */
	out = htobe32(UPGRADE_DONE);
	xfer(uep, &out, sizeof(out), &out,
	     protocol_version < 3 ? sizeof(out) : 1);
}

/*
 * Corrupt the header of the inactive rw image to make sure the system can't
 * rollback
 */
static void invalidate_inactive_rw(struct transfer_descriptor *td)
{
	/* Corrupt the rw image that is not running. */
	uint16_t subcommand = VENDOR_CC_INVALIDATE_INACTIVE_RW;

	if (td->ep_type == usb_xfer) {
		send_done(&td->uep);

		if (protocol_version > 5) {
			ext_cmd_over_usb(&td->uep, subcommand,
					 NULL, 0,
					 NULL, 0);
			printf("inactive rw corrupted\n");
		}
	}
}

/* Returns number of successfully transmitted image sections. */
static int transfer_and_reboot(struct transfer_descriptor *td,
			       uint8_t *data, size_t data_len)
{
	size_t i;
	int num_txed_secitons = 0;
	/* By default target is reset immediately after update. */
	uint16_t subcommand = VENDOR_CC_IMMEDIATE_RESET;

	for (i = 0; i < ARRAY_SIZE(sections); i++)
		if (sections[i].ustatus == needed) {
			transfer_section(td,
					 data + sections[i].offset,
					 sections[i].offset,
					 sections[i].size);
			num_txed_secitons++;
		}

	if (!num_txed_secitons) {
		if (td->ep_type == usb_xfer)
			send_done(&td->uep);

		printf("nothing to do\n");
		return 0;
	}

	printf("-------\nupdate complete\n");

	/*
	 * In upstart mode or in case target is running older protocol version
	 * - post reset is requested.
	 */
	if (td->upstart_mode || (protocol_version <= 5))
		subcommand = EXTENSION_POST_RESET;

	if (td->ep_type == usb_xfer) {
		uint32_t out;

		send_done(&td->uep);

		if (protocol_version > 5) {
			uint8_t response;
			size_t response_size;
			void *presponse;

			/*
			 * Protocol versions 6 and above use vendor command to
			 * communicate reset mode (immediate or posted) to the
			 * target.
			 *
			 * No response is expected in case of immediate reset.
			 */
			if (subcommand == VENDOR_CC_IMMEDIATE_RESET) {
				presponse = NULL;
				response_size = 0;
			} else {
				presponse = &response;
				response_size = sizeof(response);
			}

			ext_cmd_over_usb(&td->uep, subcommand,
					 NULL, 0,
					 presponse, &response_size);
		} else {
			/*
			 * Send a second stop request, which should reboot
			 * without replying.
			 */
			xfer(&td->uep, &out, sizeof(out), 0, 0);
		}
	} else {
		uint8_t response;
		size_t response_size;

		/* Need to send extended command for posted reboot. */
		if (tpm_send_pkt(td->tpm_fd, 0, 0, NULL, 0,
				 &response, &response_size, subcommand) < 0) {
			fprintf(stderr, "Failed to request posted reboot\n");
			exit(update_error);
		}

	}

	printf("reboot %s\n", subcommand == EXTENSION_POST_RESET ?
	       "request posted" : "triggered");

	return num_txed_secitons;
}

static int show_headers_versions(const void *image)
{
	const struct {
		const char *name;
		uint32_t    offset;
	} sections[] = {
		{"RO_A", CONFIG_RO_MEM_OFF},
		{"RW_A", CONFIG_RW_MEM_OFF},
		{"RO_B", CHIP_RO_B_MEM_OFF},
		{"RW_B", CONFIG_RW_B_MEM_OFF}
	};
	size_t i;

	for (i = 0; i < ARRAY_SIZE(sections); i++) {
		const struct SignedHeader *h;

		h = (const struct SignedHeader *)((uintptr_t)image +
						  sections[i].offset);
		printf("%s%s:%d.%d.%d", i ? " " : "", sections[i].name,
		       h->epoch_, h->major_, h->minor_);
	}
	printf("\n");

	return 0;
}

int main(int argc, char *argv[])
{
	struct transfer_descriptor td;
	int errorcnt;
	uint8_t *data = 0;
	size_t data_len = 0;
	uint16_t vid = VID, pid = PID;
	int i;
	size_t j;
	int transferred_sections = 0;
	int binary_vers = 0;
	int show_fw_ver = 0;
	int corrupt_inactive_rw = 0;

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
		case 'b':
			binary_vers = 1;
			break;
		case 'd':
			if (!parse_vidpid(optarg, &vid, &pid)) {
				printf("Invalid argument: \"%s\"\n", optarg);
				errorcnt++;
			}
			break;
		case 'c':
			corrupt_inactive_rw = 1;
			break;
		case 'f':
			show_fw_ver = 1;
			break;
		case 'h':
			usage(errorcnt);
			break;
		case 's':
			td.ep_type = spi_xfer;
			break;
		case 'u':
			td.upstart_mode = 1;
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
			exit(update_error);
		}
	}

	if (errorcnt)
		usage(errorcnt);

	if (!show_fw_ver && !corrupt_inactive_rw) {
		if (optind >= argc) {
			fprintf(stderr,
				"\nERROR: Missing required <binary image>\n\n");
			usage(1);
		}

		data = get_file_or_die(argv[optind], &data_len);
		printf("read %zd(%#zx) bytes from %s\n",
		       data_len, data_len, argv[optind]);
		if (data_len != CONFIG_FLASH_SIZE) {
			fprintf(stderr, "Image file is not %d bytes\n",
				CONFIG_FLASH_SIZE);
			exit(update_error);
		}

		fetch_header_versions(data);

		if (binary_vers)
			exit(show_headers_versions(data));
	} else {
		if (optind < argc)
			printf("Ignoring binary image %s\n", argv[optind]);
	}

	if (td.ep_type == usb_xfer) {
		usb_findit(vid, pid, &td.uep);
	} else {
		td.tpm_fd = open("/dev/tpm0", O_RDWR);
		if (td.tpm_fd < 0) {
			perror("Could not open TPM");
			exit(update_error);
		}
	}

	setup_connection(&td);

	if (show_fw_ver) {
		printf("Current versions:\n");
		printf("RO %d.%d.%d\n", targ.shv[0].epoch, targ.shv[0].major,
		       targ.shv[0].minor);
		printf("RW %d.%d.%d\n", targ.shv[1].epoch, targ.shv[1].major,
		       targ.shv[1].minor);
	}

	if (corrupt_inactive_rw)
		invalidate_inactive_rw(&td);

	if (data) {
		transferred_sections = transfer_and_reboot(&td, data, data_len);
		free(data);
	}

	if (td.ep_type == usb_xfer) {
		if (!data && !corrupt_inactive_rw)
			send_done(&td.uep);
		libusb_close(td.uep.devh);
		libusb_exit(NULL);
	}

	if (!transferred_sections)
		return noop;
	/*
	 * We should indicate if RO update was not done because of the
	 * insufficient RW version.
	 */
	for (j = 0; j < ARRAY_SIZE(sections); j++)
		if (sections[j].ustatus == not_possible) {
			/* This will allow scripting repeat attempts. */
			printf("Failed to update RO, run the command again\n");
			return rw_updated;
		}

	printf("image updated\n");
	return all_updated;
}
