/*
 * Copyright 2017 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <asm/byteorder.h>
#include <endian.h>
#include <fcntl.h>
#include <fmap.h>
#include <getopt.h>
#include <libusb.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#ifndef __packed
#define __packed __attribute__((packed))
#endif

#include "compile_time_macros.h"
#include "misc_util.h"
#include "update_fw.h"
#include "usb_descriptor.h"
#include "vb21_struct.h"

#ifdef DEBUG
#define debug printf
#else
#define debug(fmt, args...)
#endif

/*
 * This file contains the source code of a Linux application used to update
 * EC device firmware (common code only, gsctool takes care of cr50).
 */

#define VID USB_VID_GOOGLE
#define PID 0x5022
#define SUBCLASS USB_SUBCLASS_GOOGLE_UPDATE
#define PROTOCOL USB_PROTOCOL_GOOGLE_UPDATE

enum exit_values {
	noop = 0, /* All up to date, no update needed. */
	all_updated = 1, /* Update completed, reboot required. */
	rw_updated = 2, /* RO was not updated, reboot required. */
	update_error = 3 /* Something went wrong. */
};

typedef struct {
	uint8_t addr; /* Endpoint address */
	uint8_t len; /* Max. packet size */
} ep_info_t;

struct usb_endpoint {
	struct libusb_device_handle *devh;
	ep_info_t in_ep;
	ep_info_t out_ep;
};

struct transfer_descriptor {
	/*
	 * offsets of section available for update (not currently active).
	 */
	uint32_t offset;

	struct usb_endpoint uep;
};

/* Information about the target */
static struct first_response_pdu targ;

static uint16_t protocol_version;
static uint16_t header_type;
static char *progname;
static char *short_opts = "bd:efg:hjlnp:rsS:tuw";
static const struct option long_opts[] = {
	/* name    hasarg *flag val */
	{ "binvers", 1, NULL, 'b' },
	{ "device", 1, NULL, 'd' },
	{ "entropy", 0, NULL, 'e' },
	{ "fwver", 0, NULL, 'f' },
	{ "tp_debug", 1, NULL, 'g' },
	{ "help", 0, NULL, 'h' },
	{ "jump_to_rw", 0, NULL, 'j' },
	{ "follow_log", 0, NULL, 'l' },
	{ "no_reset", 0, NULL, 'n' },
	{ "tp_update", 1, NULL, 'p' },
	{ "reboot", 0, NULL, 'r' },
	{ "stay_in_ro", 0, NULL, 's' },
	{ "serial", 1, NULL, 'S' },
	{ "tp_info", 0, NULL, 't' },
	{ "unlock_rollback", 0, NULL, 'u' },
	{ "unlock_rw", 0, NULL, 'w' },
	{},
};

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
	       "This updates EC firmware over USB (common code EC, no cr50).\n"
	       "The required argument is the full RO+RW image.\n"
	       "\n"
	       "Options:\n"
	       "\n"
	       "  -b,--binvers             Report versions of image's "
	       "RW and RO, do not update\n"
	       "  -d,--device  VID:PID     USB device (default %04x:%04x)\n"
	       "  -e,--entropy             Add entropy to device secret\n"
	       "  -f,--fwver               Report running firmware versions.\n"
	       "  -g,--tp_debug <hex data> Touchpad debug command\n"
	       "  -h,--help                Show this message\n"
	       "  -j,--jump_to_rw          Tell EC to jump to RW\n"
	       "  -l,--follow_log          Get console log\n"
	       "  -p,--tp_update file      Update touchpad FW\n"
	       "  -r,--reboot              Tell EC to reboot\n"
	       "  -s,--stay_in_ro          Tell EC to stay in RO\n"
	       "  -S,--serial              Device serial number\n"
	       "  -t,--tp_info             Get touchpad information\n"
	       "  -u,--unlock_rollback     Tell EC to unlock the rollback region\n"
	       "  -w,--unlock_rw           Tell EC to unlock the RW region\n"
	       "\n",
	       progname, VID, PID);

	exit(errs ? update_error : noop);
}

static void str2hex(const char *str, uint8_t *data, int *len)
{
	int i;
	int slen = strlen(str);

	if (slen / 2 > *len) {
		fprintf(stderr, "Hex string too long.\n");
		exit(update_error);
	}

	if (slen % 2 != 0) {
		fprintf(stderr, "Hex string length not a multiple of 2.\n");
		exit(update_error);
	}

	for (i = 0, *len = 0; i < slen; i += 2, (*len)++) {
		char *end;
		char tmp[3];

		tmp[0] = str[i];
		tmp[1] = str[i + 1];
		tmp[2] = 0;

		data[*len] = strtol(tmp, &end, 16);

		if (*end != 0) {
			fprintf(stderr, "Invalid hex string.\n");
			exit(update_error);
		}
	}
}

static void hexdump(const uint8_t *data, int len)
{
	int i;

	for (i = 0; i < len; i++) {
		printf("%02x", data[i]);
		if ((i % 16) == 15)
			printf("\n");
	}

	if ((len % 16) != 0)
		printf("\n");
}

static void dump_touchpad_info(const uint8_t *data, int len)
{
	const struct touchpad_info *info = (const struct touchpad_info *)data;

	if (len != sizeof(struct touchpad_info)) {
		fprintf(stderr, "Hex string length is not %zu",
			sizeof(struct touchpad_info));
		hexdump(data, len);
		return;
	}

	printf("\n");
	printf("status:         0x%02x\n", info->status);
	printf("vendor:         0x%04x\n", info->vendor);
	printf("fw_address:     0x%08x\n", info->fw_address);
	printf("fw_size:        0x%08x\n", info->fw_size);

	printf("allowed_fw_hash:\n");
	hexdump(info->allowed_fw_hash, sizeof(info->allowed_fw_hash));

	switch (info->vendor) {
	case 0x04f3: /* ELAN */
	case 0x0483: /* ST */
		printf("id:             0x%04x\n", info->elan.id);
		printf("fw_version:     0x%04x\n", info->elan.fw_version);
		printf("fw_fw_checksum: 0x%04x\n", info->elan.fw_checksum);
		break;
	default:
		fprintf(stderr, "Unknown vendor, vendor specific data:\n");
		hexdump((const uint8_t *)&info->elan, sizeof(info->elan));
		break;
	}
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

	if (fread(data, st.st_size, 1, fp) != 1) {
		perror("fread");
		exit(update_error);
	}

	fclose(fp);

	*len_ptr = len;
	return data;
}

#define USB_ERROR(m, r)                                                        \
	fprintf(stderr, "%s:%d, %s returned %d (%s)\n", __FILE__, __LINE__, m, \
		r, libusb_strerror(r))

/*
 * Actual USB transfer function, the 'allow_less' flag indicates that the
 * valid response could be shortef than allotted memory, the 'rxed_count'
 * pointer, if provided along with 'allow_less' lets the caller know how mavy
 * bytes were received.
 */
static void do_xfer(struct usb_endpoint *uep, void *outbuf, int outlen,
		    void *inbuf, int inlen, int allow_less, size_t *rxed_count)
{
	int r, actual;

	/* Send data out */
	if (outbuf && outlen) {
		actual = 0;
		r = libusb_bulk_transfer(uep->devh, uep->out_ep.addr, outbuf,
					 outlen, &actual, 2000);
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
		r = libusb_bulk_transfer(uep->devh, uep->in_ep.addr, inbuf,
					 inlen, &actual, 5000);
		if (r < 0) {
			USB_ERROR("libusb_bulk_transfer", r);
			exit(update_error);
		}
		if ((actual != inlen) && !allow_less) {
			fprintf(stderr, "%s:%d, only received %d/%d bytes\n",
				__FILE__, __LINE__, actual, inlen);
			hexdump(inbuf, actual);
			shut_down(uep);
		}

		if (rxed_count)
			*rxed_count = actual;
	}
}

static void xfer(struct usb_endpoint *uep, void *outbuf, size_t outlen,
		 void *inbuf, size_t inlen, int allow_less)
{
	do_xfer(uep, outbuf, outlen, inbuf, inlen, allow_less, NULL);
}

/* Return 0 on error, since it's never gonna be EP 0 */
static int find_endpoint(const struct libusb_interface_descriptor *iface,
			 struct usb_endpoint *uep)
{
	const struct libusb_endpoint_descriptor *ep;

	if (iface->bInterfaceClass == 255 &&
	    iface->bInterfaceSubClass == SUBCLASS &&
	    iface->bInterfaceProtocol == PROTOCOL) {
		if (iface->bNumEndpoints == 2) {
			for (int i = 0; i < iface->bNumEndpoints; i++) {
				ep = &iface->endpoint[i];
				if ((ep->bEndpointAddress &
				     LIBUSB_ENDPOINT_DIR_MASK) ==
				    LIBUSB_ENDPOINT_IN) {
					uep->in_ep.addr = ep->bEndpointAddress;
					uep->in_ep.len = ep->wMaxPacketSize;
				} else {
					uep->out_ep.addr = ep->bEndpointAddress;
					uep->out_ep.len = ep->wMaxPacketSize;
				}
			}
		} else {
			USB_ERROR("Invalid endpoint number",
				  iface->bNumEndpoints);
			return 0;
		}
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
				iface_num = iface->bInterfaceNumber;
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

	*vid_ptr = (uint16_t)strtoull(copy, &e, 16);
	if (!*optarg || (e && *e))
		return 0;

	*pid_ptr = (uint16_t)strtoull(s, &e, 16);
	if (!*optarg || (e && *e))
		return 0;

	return 1;
}

static libusb_device_handle *check_device(libusb_device *dev, uint16_t vid,
					  uint16_t pid, char *serialno)
{
	struct libusb_device_descriptor desc;
	libusb_device_handle *handle = NULL;
	char sn[256];
	int ret;
	int match = 1;
	int snvalid = 0;

	ret = libusb_get_device_descriptor(dev, &desc);
	if (ret < 0)
		return NULL;

	ret = libusb_open(dev, &handle);

	if (ret != LIBUSB_SUCCESS)
		return NULL;

	if (desc.iSerialNumber) {
		ret = libusb_get_string_descriptor_ascii(handle,
							 desc.iSerialNumber,
							 (unsigned char *)sn,
							 sizeof(sn));
		if (ret > 0)
			snvalid = 1;
	}

	if (vid != 0 && vid != desc.idVendor)
		match = 0;
	if (pid != 0 && pid != desc.idProduct)
		match = 0;
	if (serialno != NULL && (!snvalid || strstr(sn, serialno) == NULL))
		match = 0;

	if (match)
		return handle;

	libusb_close(handle);
	return NULL;
}

static void usb_findit(uint16_t vid, uint16_t pid, char *serialno,
		       struct usb_endpoint *uep)
{
	int iface_num, r, i;
	libusb_device **devs;
	libusb_device_handle *devh = NULL;
	ssize_t count;

	memset(uep, 0, sizeof(*uep));

	r = libusb_init(NULL);
	if (r < 0) {
		USB_ERROR("libusb_init", r);
		exit(update_error);
	}

	count = libusb_get_device_list(NULL, &devs);
	if (count < 0)
		return;

	for (i = 0; devs[i]; i++) {
		devh = check_device(devs[i], vid, pid, serialno);
		if (devh) {
			printf("Found device.\n");
			break;
		}
	}

	libusb_free_device_list(devs, 1);

	if (!devh) {
		fprintf(stderr, "Can't find device\n");
		exit(update_error);
	}

	uep->devh = devh;

	iface_num = find_interface(uep);
	if (iface_num < 0) {
		fprintf(stderr, "USB FW update not supported by that device\n");
		shut_down(uep);
	}
	if (!uep->in_ep.len || !uep->out_ep.len) {
		fprintf(stderr, "wMaxPacketSize isn't valid\n");
		shut_down(uep);
	}

	printf("Found interface %d, IN ep 0x%x(%d), OUT ep 0x%x(%d)\n",
	       iface_num, uep->in_ep.addr, uep->in_ep.len, uep->out_ep.addr,
	       uep->out_ep.len);

	libusb_set_auto_detach_kernel_driver(uep->devh, 1);
	r = libusb_claim_interface(uep->devh, iface_num);
	if (r < 0) {
		USB_ERROR("libusb_claim_interface", r);
		shut_down(uep);
	}

	printf("READY\n-------\n");
}

static int transfer_block(struct usb_endpoint *uep,
			  struct update_frame_header *ufh,
			  uint8_t *transfer_data_ptr, size_t payload_size)
{
	size_t transfer_size;
	uint32_t reply;
	int actual;
	int r;

	/* First send the header. */
	xfer(uep, ufh, sizeof(*ufh), NULL, 0, 0);

	/* Now send the block, chunk by chunk. */
	for (transfer_size = 0; transfer_size < payload_size;) {
		int chunk_size;

		chunk_size = MIN((size_t)uep->out_ep.len,
				 payload_size - transfer_size);
		xfer(uep, transfer_data_ptr, chunk_size, NULL, 0, 0);
		transfer_data_ptr += chunk_size;
		transfer_size += chunk_size;
	}

	/* Now get the reply. */
	r = libusb_bulk_transfer(uep->devh, uep->in_ep.addr, (void *)&reply,
				 sizeof(reply), &actual, 5000);
	if (r) {
		if (r == -7) {
			fprintf(stderr, "Timeout!\n");
			return r;
		}
		USB_ERROR("libusb_bulk_transfer", r);
		shut_down(uep);
	}

	reply = *((uint8_t *)&reply);
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
 * smart_update - non-zero to enable the smart trailing of 0xff.
 */
static void transfer_section(struct transfer_descriptor *td, uint8_t *data_ptr,
			     uint32_t section_addr, size_t data_len,
			     uint8_t smart_update)
{
	/*
	 * Actually, we can skip trailing chunks of 0xff, as the entire
	 * section space must be erased before the update is attempted.
	 *
	 * FIXME: We can be smarter than this and skip blocks within the image.
	 */
	if (smart_update)
		while (data_len && (data_ptr[data_len - 1] == 0xff))
			data_len--;

	printf("sending 0x%zx bytes to %#x\n", data_len, section_addr);
	while (data_len) {
		size_t payload_size;
		uint32_t block_base;
		int max_retries;

		/* prepare the header to prepend to the block. */
		payload_size = MIN(data_len, targ.common.maximum_pdu_size);

		block_base = htobe32(section_addr);

		struct update_frame_header ufh;

		ufh.block_size = htobe32(payload_size +
					 sizeof(struct update_frame_header));
		ufh.cmd.block_base = block_base;
		ufh.cmd.block_digest = 0;
		for (max_retries = 10; max_retries; max_retries--)
			if (!transfer_block(&td->uep, &ufh, data_ptr,
					    payload_size))
				break;

		if (!max_retries) {
			fprintf(stderr, "Failed to transfer block, %zd to go\n",
				data_len);
			exit(update_error);
		}
		data_len -= payload_size;
		data_ptr += payload_size;
		section_addr += payload_size;
	}
}

/*
 * Each RO or RW section of the new image can be in one of the following
 * states.
 */
enum upgrade_status {
	not_needed = 0, /* Version below or equal that on the target. */
	not_possible, /*
		       * RO is newer, but can't be transferred due to
		       * target RW shortcomings.
		       */
	needed /*
		* This section needs to be transferred to the
		* target.
		*/
};

/* This array describes all sections of the new image. */
static struct {
	const char *name;
	uint32_t offset;
	uint32_t size;
	enum upgrade_status ustatus;
	char version[32];
	int32_t rollback;
	uint32_t key_version;
} sections[] = { { "RO" }, { "RW" } };

static const struct fmap_area *fmap_find_area_or_die(const struct fmap *fmap,
						     const char *name)
{
	const struct fmap_area *fmaparea;

	fmaparea = fmap_find_area(fmap, name);
	if (!fmaparea) {
		fprintf(stderr, "Cannot find FMAP area %s\n", name);
		exit(update_error);
	}

	return fmaparea;
}

/*
 * Scan the new image and retrieve versions of all sections.
 */
static void fetch_header_versions(const uint8_t *image, size_t len)
{
	const struct fmap *fmap;
	const struct fmap_area *fmaparea;
	long int offset;
	size_t i;

	offset = fmap_find(image, len);
	if (offset < 0) {
		fprintf(stderr, "Cannot find FMAP in image\n");
		exit(update_error);
	}
	fmap = (const struct fmap *)(image + offset);

	/* FIXME: validate fmap struct more than this? */
	if (fmap->size != len) {
		fprintf(stderr, "Mismatch between FMAP size and image size\n");
		exit(update_error);
	}

	for (i = 0; i < ARRAY_SIZE(sections); i++) {
		const char *fmap_name;
		const char *fmap_fwid_name;
		const char *fmap_rollback_name = NULL;
		const char *fmap_key_name = NULL;

		if (!strcmp(sections[i].name, "RO")) {
			fmap_name = "EC_RO";
			fmap_fwid_name = "RO_FRID";
		} else if (!strcmp(sections[i].name, "RW")) {
			fmap_name = "EC_RW";
			fmap_fwid_name = "RW_FWID";
			fmap_rollback_name = "RW_RBVER";
			/*
			 * Key version comes from key RO (RW signature does not
			 * contain the key version.
			 */
			fmap_key_name = "KEY_RO";
		} else {
			fprintf(stderr, "Invalid section name\n");
			exit(update_error);
		}

		fmaparea = fmap_find_area_or_die(fmap, fmap_name);

		/* FIXME: endianness? */
		sections[i].offset = fmaparea->offset;
		sections[i].size = fmaparea->size;

		fmaparea = fmap_find_area_or_die(fmap, fmap_fwid_name);

		if (fmaparea->size != sizeof(sections[i].version)) {
			fprintf(stderr, "Invalid fwid size\n");
			exit(update_error);
		}
		memcpy(sections[i].version, image + fmaparea->offset,
		       fmaparea->size);

		sections[i].rollback = -1;
		if (fmap_rollback_name) {
			fmaparea = fmap_find_area(fmap, fmap_rollback_name);
			if (fmaparea)
				memcpy(&sections[i].rollback,
				       image + fmaparea->offset,
				       sizeof(sections[i].rollback));
		}

		sections[i].key_version = -1;
		if (fmap_key_name) {
			fmaparea = fmap_find_area(fmap, fmap_key_name);
			if (fmaparea) {
				const struct vb21_packed_key *key =
					(const void *)(image +
						       fmaparea->offset);
				sections[i].key_version = key->key_version;
			}
		}
	}
}

static int show_headers_versions(const void *image)
{
	size_t i;

	for (i = 0; i < ARRAY_SIZE(sections); i++) {
		printf("%s off=%08x/%08x v=%.32s rb=%d kv=%d\n",
		       sections[i].name, sections[i].offset, sections[i].size,
		       sections[i].version, sections[i].rollback,
		       sections[i].key_version);
	}
	return 0;
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

		/* Skip currently active section. */
		if (offset != td->offset)
			continue;

		sections[i].ustatus = needed;
	}
}

static void setup_connection(struct transfer_descriptor *td)
{
	size_t rxed_size;
	size_t i;
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

	struct update_frame_header ufh;
	uint8_t inbuf[td->uep.in_ep.len];
	int actual = 0;

	/* Flush all data from endpoint to recover in case of error. */
	while (!libusb_bulk_transfer(td->uep.devh, td->uep.in_ep.addr,
				     (void *)&inbuf, td->uep.in_ep.len, &actual,
				     10)) {
		printf("flush\n");
	}

	memset(&ufh, 0, sizeof(ufh));
	ufh.block_size = htobe32(sizeof(ufh));
	do_xfer(&td->uep, &ufh, sizeof(ufh), &start_resp, sizeof(start_resp), 1,
		&rxed_size);

	/* We got something. Check for errors in response */
	if (rxed_size < 8) {
		fprintf(stderr, "Unexpected response size %zd: ", rxed_size);
		for (i = 0; i < rxed_size; i++)
			fprintf(stderr, " %02x", ((uint8_t *)&start_resp)[i]);
		fprintf(stderr, "\n");
		exit(update_error);
	}

	protocol_version = be16toh(start_resp.rpdu.protocol_version);
	if (protocol_version < 5 || protocol_version > 6) {
		fprintf(stderr, "Unsupported protocol version %d\n",
			protocol_version);
		exit(update_error);
	}

	header_type = be16toh(start_resp.rpdu.header_type);

	printf("target running protocol version %d (type %d)\n",
	       protocol_version, header_type);
	if (header_type != UPDATE_HEADER_TYPE_COMMON) {
		fprintf(stderr, "Unsupported header type %d\n", header_type);
		exit(update_error);
	}

	error_code = be32toh(start_resp.rpdu.return_value);

	if (error_code) {
		fprintf(stderr, "Target reporting error %d\n", error_code);
		shut_down(&td->uep);
		exit(update_error);
	}

	td->offset = be32toh(start_resp.rpdu.common.offset);
	memcpy(targ.common.version, start_resp.rpdu.common.version,
	       sizeof(start_resp.rpdu.common.version));
	targ.common.maximum_pdu_size =
		be32toh(start_resp.rpdu.common.maximum_pdu_size);
	targ.common.flash_protection =
		be32toh(start_resp.rpdu.common.flash_protection);
	targ.common.min_rollback = be32toh(start_resp.rpdu.common.min_rollback);
	targ.common.key_version = be32toh(start_resp.rpdu.common.key_version);

	printf("maximum PDU size: %d\n", targ.common.maximum_pdu_size);
	printf("Flash protection status: %04x\n", targ.common.flash_protection);
	printf("version: %32s\n", targ.common.version);
	printf("key_version: %d\n", targ.common.key_version);
	printf("min_rollback: %d\n", targ.common.min_rollback);
	printf("offset: writable at %#x\n", td->offset);

	pick_sections(td);
}

/*
 * Channel TPM extension/vendor command over USB. The payload of the USB frame
 * in this case consists of the 2 byte subcommand code concatenated with the
 * command body. The caller needs to indicate if a response is expected, and
 * if it is - of what maximum size.
 */
static int ext_cmd_over_usb(struct usb_endpoint *uep, uint16_t subcommand,
			    void *cmd_body, size_t body_size, void *resp,
			    size_t *resp_size, int allow_less)
{
	struct update_frame_header *ufh;
	uint16_t *frame_ptr;
	size_t usb_msg_size;

	usb_msg_size = sizeof(struct update_frame_header) + sizeof(subcommand) +
		       body_size;

	ufh = malloc(usb_msg_size);
	if (!ufh) {
		printf("%s: failed to allocate %zd bytes\n", __func__,
		       usb_msg_size);
		return -1;
	}

	ufh->block_size = htobe32(usb_msg_size);
	ufh->cmd.block_digest = 0;
	ufh->cmd.block_base = htobe32(UPDATE_EXTRA_CMD);
	frame_ptr = (uint16_t *)(ufh + 1);
	*frame_ptr = htobe16(subcommand);

	if (body_size)
		memcpy(frame_ptr + 1, cmd_body, body_size);

	xfer(uep, ufh, usb_msg_size, resp, resp_size ? *resp_size : 0,
	     allow_less);

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
	out = htobe32(UPDATE_DONE);
	xfer(uep, &out, sizeof(out), &out, 1, 0);
}

static void send_subcommand(struct transfer_descriptor *td, uint16_t subcommand,
			    void *cmd_body, size_t body_size, uint8_t *response,
			    size_t response_size)
{
	send_done(&td->uep);

	ext_cmd_over_usb(&td->uep, subcommand, cmd_body, body_size, response,
			 &response_size, 0);
	printf("sent command %x, resp %x\n", subcommand, response[0]);
}

/* Returns number of successfully transmitted image sections. */
static int transfer_image(struct transfer_descriptor *td, uint8_t *data,
			  size_t data_len)
{
	size_t i;
	int num_txed_sections = 0;

	for (i = 0; i < ARRAY_SIZE(sections); i++)
		if (sections[i].ustatus == needed) {
			transfer_section(td, data + sections[i].offset,
					 sections[i].offset, sections[i].size,
					 1);
			num_txed_sections++;
		}

	/*
	 * Move USB receiver sate machine to idle state so that vendor
	 * commands can be processed later, if any.
	 */
	send_done(&td->uep);

	if (!num_txed_sections)
		printf("nothing to do\n");
	else
		printf("-------\nupdate complete\n");
	return num_txed_sections;
}

static void generate_reset_request(struct transfer_descriptor *td)
{
	size_t response_size;
	uint8_t response;
	uint16_t subcommand;
	uint8_t command_body[2]; /* Max command body size. */
	size_t command_body_size;

	if (protocol_version < 6) {
		/*
		 * Send a second stop request, which should reboot
		 * without replying.
		 */
		send_done(&td->uep);
		/* Nothing we can do over /dev/tpm0 running versions below 6. */
		return;
	}

	/*
	 * If the user explicitly wants it, request post reset instead of
	 * immediate reset. In this case next time the target reboots, the h1
	 * will reboot as well, and will consider running the uploaded code.
	 *
	 * In case target RW version is 19 or above, to reset the target the
	 * host is supposed to send the command to enable the uploaded image
	 * disabled by default.
	 *
	 * Otherwise the immediate reset command would suffice.
	 */
	/* Most common case. */
	command_body_size = 0;
	response_size = 1;
	subcommand = UPDATE_EXTRA_CMD_IMMEDIATE_RESET;
	ext_cmd_over_usb(&td->uep, subcommand, command_body, command_body_size,
			 &response, &response_size, 0);

	printf("reboot not triggered\n");
}

static void get_random(uint8_t *data, int len)
{
	FILE *fp;
	int i = 0;

	fp = fopen("/dev/random", "rb");
	if (!fp) {
		perror("Can't open /dev/random");
		exit(update_error);
	}

	while (i < len) {
		int ret = fread(data + i, len - i, 1, fp);

		if (ret < 0) {
			perror("fread");
			exit(update_error);
		}

		i += ret;
	}

	fclose(fp);
}

static void read_console(struct transfer_descriptor *td)
{
	uint8_t payload[] = { 0x1 };
	uint8_t response[64];
	size_t response_size = 64;
	struct timespec sleep_duration = {
		/* 100 ms */
		.tv_sec = 0,
		.tv_nsec = 100l * 1000l * 1000l,
	};

	send_done(&td->uep);

	printf("\n");
	while (1) {
		response_size = 1;
		ext_cmd_over_usb(&td->uep, UPDATE_EXTRA_CMD_CONSOLE_READ_INIT,
				 NULL, 0, response, &response_size, 0);

		if (response[0] != 0) {
			printf("failed to read console, ret %d\n", response[0]);
			return;
		}

		while (1) {
			response_size = 64;
			ext_cmd_over_usb(&td->uep,
					 UPDATE_EXTRA_CMD_CONSOLE_READ_NEXT,
					 payload, sizeof(payload), response,
					 &response_size, 1);
			if (response[0] == 0)
				break;
			/* make sure it's null-terminated. */
			response[response_size - 1] = 0;
			printf("%s", (const char *)response);
		}
		nanosleep(&sleep_duration, NULL);
	}
}

int main(int argc, char *argv[])
{
	struct transfer_descriptor td;
	int errorcnt;
	uint8_t *data = 0;
	size_t data_len = 0;
	uint16_t vid = VID, pid = PID;
	char *serialno = NULL;
	int i;
	size_t j;
	int transferred_sections = 0;
	int binary_vers = 0;
	int show_fw_ver = 0;
	int no_reset_request = 0;
	int touchpad_update = 0;
	int extra_command = -1;
	uint8_t extra_command_data[50];
	int extra_command_data_len = 0;
	uint8_t extra_command_answer[64];
	int extra_command_answer_len = 1;

	progname = strrchr(argv[0], '/');
	if (progname)
		progname++;
	else
		progname = argv[0];

	/* Usb transfer - default mode. */
	memset(&td, 0, sizeof(td));

	errorcnt = 0;
	opterr = 0; /* quiet, you */
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
		case 'e':
			get_random(extra_command_data, 32);
			extra_command_data_len = 32;
			extra_command = UPDATE_EXTRA_CMD_INJECT_ENTROPY;
			break;
		case 'f':
			show_fw_ver = 1;
			break;
		case 'g':
			extra_command = UPDATE_EXTRA_CMD_TOUCHPAD_DEBUG;
			/* Maximum length. */
			extra_command_data_len = 50;
			str2hex(optarg, extra_command_data,
				&extra_command_data_len);
			hexdump(extra_command_data, extra_command_data_len);
			extra_command_answer_len = 64;
			break;
		case 'h':
			usage(errorcnt);
			break;
		case 'j':
			extra_command = UPDATE_EXTRA_CMD_JUMP_TO_RW;
			break;
		case 'l':
			extra_command = UPDATE_EXTRA_CMD_CONSOLE_READ_INIT;
			break;
		case 'n':
			no_reset_request = 1;
			break;
		case 'p':
			touchpad_update = 1;

			data = get_file_or_die(optarg, &data_len);
			printf("read %zd(%#zx) bytes from %s\n", data_len,
			       data_len, argv[optind - 1]);

			break;
		case 'r':
			extra_command = UPDATE_EXTRA_CMD_IMMEDIATE_RESET;
			break;
		case 's':
			extra_command = UPDATE_EXTRA_CMD_STAY_IN_RO;
			break;
		case 'S':
			serialno = optarg;
			break;
		case 't':
			extra_command = UPDATE_EXTRA_CMD_TOUCHPAD_INFO;
			extra_command_answer_len = sizeof(struct touchpad_info);
			break;
		case 'u':
			extra_command = UPDATE_EXTRA_CMD_UNLOCK_ROLLBACK;
			break;
		case 'w':
			extra_command = UPDATE_EXTRA_CMD_UNLOCK_RW;
			break;
		case 0: /* auto-handled option */
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

	if (!show_fw_ver && extra_command == -1 && !touchpad_update) {
		if (optind >= argc) {
			fprintf(stderr,
				"\nERROR: Missing required <binary image>\n\n");
			usage(1);
		}

		data = get_file_or_die(argv[optind], &data_len);
		printf("read %zd(%#zx) bytes from %s\n", data_len, data_len,
		       argv[optind]);

		fetch_header_versions(data, data_len);

		if (binary_vers)
			exit(show_headers_versions(data));
	} else {
		if (optind < argc)
			printf("Ignoring binary image %s\n", argv[optind]);
	}

	usb_findit(vid, pid, serialno, &td.uep);

	setup_connection(&td);

	if (show_fw_ver) {
		printf("Current versions:\n");
		printf("Writable %32s\n", targ.common.version);
	}

	if (data) {
		if (touchpad_update) {
			transfer_section(&td, data, 0x80000000, data_len, 0);
			free(data);

			send_done(&td.uep);
		} else {
			transferred_sections =
				transfer_image(&td, data, data_len);
			free(data);

			if (transferred_sections && !no_reset_request)
				generate_reset_request(&td);
		}
	} else if (extra_command == UPDATE_EXTRA_CMD_CONSOLE_READ_INIT) {
		read_console(&td);
	} else if (extra_command > -1) {
		send_subcommand(&td, extra_command, extra_command_data,
				extra_command_data_len, extra_command_answer,
				extra_command_answer_len);

		switch (extra_command) {
		case UPDATE_EXTRA_CMD_TOUCHPAD_INFO:
			dump_touchpad_info(extra_command_answer,
					   extra_command_answer_len);
			break;
		case UPDATE_EXTRA_CMD_TOUCHPAD_DEBUG:
			hexdump(extra_command_answer, extra_command_answer_len);
			break;
		}
	}

	libusb_close(td.uep.devh);
	libusb_exit(NULL);

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
