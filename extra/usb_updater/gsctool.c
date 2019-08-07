/*
 * Copyright 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <asm/byteorder.h>
#include <ctype.h>
#include <endian.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <libusb.h>
#include <openssl/sha.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <termios.h>
#include <unistd.h>

#include "config.h"

#include "ccd_config.h"
#include "compile_time_macros.h"
#include "flash_log.h"
#include "generated_version.h"
#include "gsctool.h"
#include "misc_util.h"
#include "signed_header.h"
#include "tpm_registers.h"
#include "tpm_vendor_cmds.h"
#include "upgrade_fw.h"
#include "usb_descriptor.h"
#include "verify_ro.h"

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
 * There are two ways to communicate with the CR50 device: USB and /dev/tpm0
 * (when this app is running on a chromebook with the CR50 device). Originally
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
 * The encapsultation format is different between the /dev/tpm0 and USB cases:
 *
 *   4 bytes      4 bytes         4 bytes               variable size
 * +-----------+--------------+---------------+----------~~--------------+
 * + total size| block digest |  dest address |           data           |
 * +-----------+--------------+---------------+----------~~--------------+
 *  \           \                                                       /
 *   \           \                                                     /
 *    \           +----- FW update PDU sent over /dev/tpm0 -----------+
 *     \                                                             /
 *      +--------- USB frame, requires total size field ------------+
 *
 * The update protocol data unints (PDUs) are passed over /dev/tpm0, the
 * encapsulation includes integritiy verification and destination address of
 * the data (more of this later). /dev/tpm0 transactions pretty much do not
 * have size limits, whereas the USB data is sent in chunks of the size
 * determined when the USB connestion is set up. This is why USB requires an
 * additional encapsulation into frames to communicate the PDU size to the
 * client side so that the PDU can be reassembled before passing to the
 * programming function.
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
 * Note that protocol versions before 5 are described here for completeness,
 * but are not supported any more by this utility.
 *
 * Version 1 is used over /dev/tpm0. The response is either 4 or 1 bytes in
 * size. The 4 byte response is the *base address* of the backup RW section,
 * no support for RO updates. The one byte response is an error indication,
 * possibly reporting flash erase failure, command format error, etc.
 *
 * Version 2 is used over USB. The response is 8 bytes in size. The first four
 * bytes are either the *base address* of the backup RW section (still no RO
 * updates), or an error code, the same as in Version 1. The second 4 bytes
 * are the protocol version number (set to 2).
 *
 * All versions above 2 behave the same over /dev/tpm0 and USB.
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
 * but it indicates the target's ablitiy to channel TPM vendor commands
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

/*
 * Need to create an entire TPM PDU when upgrading over /dev/tpm0 and need to
 * have space to prepare the entire PDU.
 */
struct upgrade_pkt {
	__be16	tag;
	__be32	length;
	__be32	ordinal;
	__be16	subcmd;
	union {
		/*
		 * Upgrade PDUs as opposed to all other vendor and extension
		 * commands include two additional fields in the header.
		 */
		struct {
			__be32	digest;
			__be32	address;
			char data[0];
		} upgrade;
		struct {
			char data[0];
		} command;
	};
} __packed;

/*
 * Structure used to simplify mapping command line options into Boolean
 * variables. If an option is present, the corresponding integer value is set
 * to 1.
 */
struct options_map {
	char opt;
	int *flag;
};

/*
 * Structure used to combine option description used by getopt_long() and help
 * text for the option.
 */
struct option_container {
	struct option opt;
	const char *help_text;
};

/*
 * This by far exceeds the largest vendor command response size we ever
 * expect.
 */
#define MAX_BUF_SIZE	500

/*
 * Max. length of the board ID string representation.
 *
 * Board ID is either a 4-character ASCII alphanumeric string or an 8-digit
 * hex.
 */
#define MAX_BOARD_ID_LENGTH 9

/*
 * Length, in bytes, of the SN Bits serial number bits.
 */
#define SN_BITS_SIZE  (96 >> 3)

/*
 * Max. length of FW version in the format of <epoch>.<major>.<minor>
 * (3 uint32_t string representation + 2 separators + NULL terminator).
 */
#define MAX_FW_VER_LENGTH 33

static int verbose_mode;
static uint32_t protocol_version;
static char *progname;

/*
 * List of command line options, ***sorted by the short form***.
 *
 * The help_text field does not include the short and long option strings,
 * they are retrieved from the opt structure. In case the help text needs to
 * have something printed immediately after the option strings (for example,
 * an optional parameter), it should be included in the beginning of help_text
 * string separated by the % character.
 *
 * usage() function which prints out the help message will concatenate the
 * short and long options and the optional parameter, if present, and then
 * print the rest of the text message at a fixed indentation.
 */
static const struct option_container cmd_line_options[] = {
	/* name   has_arg    *flag  val */
	{{"any", no_argument, NULL, 'a'},
	 "Try any interfaces to find Cr50"
	 " (-d, -s, -t are all ignored)"},
	{{"background_update_supported", no_argument, NULL, 'B'},
	 "Force background update mode (relevant"
	 " only when interacting"
	 " with Cr50 versions before 0.0.19)"
	},
	{{"binvers", no_argument, NULL, 'b'},
	 "Report versions of Cr50 image's "
	 "RW and RO headers, do not update"},
	{{"corrupt", no_argument, NULL, 'c'},
	 "Corrupt the inactive rw"},
	{{"device", required_argument, NULL, 'd'},
	 " VID:PID%USB device (default 18d1:5014)"},
	{{"endorsement_seed", optional_argument, NULL, 'e'},
	 "[state]%get/set the endorsement key seed"},
	{{"fwver", no_argument, NULL, 'f'},
	 "Report running Cr50 firmware versions"},
	{{"factory", required_argument, NULL, 'F'},
	 "[enable|disable]%Control factory mode"},
	{{"help", no_argument, NULL, 'h'},
	 "Show this message"},
	{{"ccd_info", no_argument, NULL, 'I'},
	 "Get information about CCD state"},
	{{"board_id", optional_argument, NULL, 'i'},
	 "[ID[:FLAGS]]%Get or set Info1 board ID fields. ID could be 32 bit "
	 "hex or 4 character string."},
	{{"ccd_lock", no_argument, NULL, 'k'},
	 "Lock CCD"},
	{{"flog", optional_argument, NULL, 'L'},
	 "[prev entry]%Retrieve contents of the flash log"
	 " (newer than <prev entry> if specified)"},
	{{"machine", no_argument, NULL, 'M'},
	 "Output in a machine-friendly way. "
	 "Effective with -b, -f, -i, and -O."},
	{{"tpm_mode", optional_argument, NULL, 'm'},
	 "[enable|disable]%Change or query tpm_mode"},
	{{"serial", required_argument, NULL, 'n'},
	 "Cr50 CCD serial number"},
	{{"openbox_rma", required_argument, NULL, 'O'},
	 "<desc_file>%Verify other device's RO integrity using information "
	 "provided in <desc file>"},
	{{"ccd_open", no_argument, NULL, 'o'},
	 "Start CCD open sequence"},
	{{"password", no_argument, NULL, 'P'},
	 "Set or clear CCD password. Use 'clear:<cur password>' to clear it"},
	{{"post_reset", no_argument, NULL, 'p'},
	 "Request post reset after transfer"},
	{{"sn_rma_inc", required_argument, NULL, 'R'},
	 "RMA_INC%Increment SN RMA count by RMA_INC. RMA_INC should be 0-7."},
	{{"rma_auth", optional_argument, NULL, 'r'},
	 "[auth_code]%Request RMA challenge, process "
	 "RMA authentication code"},
	{{"sn_bits", required_argument, NULL, 'S'},
	 "SN_BITS%Set Info1 SN bits fields. SN_BITS should be 96 bit hex."},
	{{"systemdev", no_argument, NULL, 's'},
	 "Use /dev/tpm0 (-d is ignored)"},
	{{"tstamp", optional_argument, NULL, 'T'},
	 "[<tstamp>]%Get or set flash log timestamp base"},
	{{"trunks_send", no_argument, NULL, 't'},
	 "Use `trunks_send --raw' (-d is ignored)"},
	{{"ccd_unlock", no_argument, NULL, 'U'},
	 "Start CCD unlock sequence"},
	{{"upstart", no_argument, NULL, 'u'},
	 "Upstart mode (strict header checks)"},
	{{"verbose", no_argument, NULL, 'V'},
	 "Enable debug messages"},
	{{"version", no_argument, NULL, 'v'},
	 "Report this utility version"},
	{{"wp", no_argument, NULL, 'w'},
	 "Get the current wp setting"}
};

/* Helper to print debug messages when verbose flag is specified. */
static void debug(const char *fmt, ...)
{
	va_list args;

	if (verbose_mode) {
		va_start(args, fmt);
		vprintf(fmt, args);
		va_end(args);
	}
}

/* Helpers to convert between binary and hex ascii and back. */
static char to_hexascii(uint8_t c)
{
	if (c <= 9)
		return '0' + c;
	return 'a' + c - 10;
}

static int from_hexascii(char c)
{
	/* convert to lower case. */
	c = tolower(c);

	if (c < '0' || c > 'f' || ((c > '9') && (c < 'a')))
		return -1; /* Not an ascii character. */

	if (c <= '9')
		return c - '0';

	return c - 'a' + 10;
}

/* Functions to communicate with the TPM over the trunks_send --raw channel. */

/* File handle to share between write and read sides. */
static FILE *tpm_output;
static int ts_write(const void *out, size_t len)
{
	const char *cmd_head = "PATH=\"${PATH}:/usr/sbin\" trunks_send --raw ";
	size_t head_size = strlen(cmd_head);
	char full_command[head_size + 2 * len + 1];
	size_t i;

	strcpy(full_command, cmd_head);
	/*
	 * Need to convert binary input into hex ascii output to pass to the
	 * trunks_send command.
	 */
	for (i = 0; i < len; i++) {
		uint8_t c = ((const uint8_t *)out)[i];

		full_command[head_size + 2 * i] = to_hexascii(c >> 4);
		full_command[head_size + 2 * i + 1] = to_hexascii(c & 0xf);
	}

	/* Make it a proper zero terminated string. */
	full_command[sizeof(full_command) - 1] = 0;
	debug("cmd: %s\n", full_command);
	tpm_output = popen(full_command, "r");
	if (tpm_output)
		return len;

	fprintf(stderr, "Error: failed to launch trunks_send --raw\n");
	return -1;
}

static int ts_read(void *buf, size_t max_rx_size)
{
	int i;
	int pclose_rv;
	int rv;
	char response[max_rx_size * 2];

	if (!tpm_output) {
		fprintf(stderr, "Error: attempt to read empty output\n");
		return -1;
	}

	rv = fread(response, 1, sizeof(response), tpm_output);
	if (rv > 0)
		rv -= 1; /* Discard the \n character added by trunks_send. */

	debug("response of size %d, max rx size %zd: %s\n",
	      rv, max_rx_size, response);

	pclose_rv = pclose(tpm_output);
	if (pclose_rv < 0) {
		fprintf(stderr,
			"Error: pclose failed: error %d (%s)\n",
			errno, strerror(errno));
		return -1;
	}

	tpm_output = NULL;

	if (rv & 1) {
		fprintf(stderr,
			"Error: trunks_send returned odd number of bytes: %s\n",
		response);
		return -1;
	}

	for (i = 0; i < rv/2; i++) {
		uint8_t byte;
		char c;
		int nibble;

		c = response[2 * i];
		nibble = from_hexascii(c);
		if (nibble < 0) {
			fprintf(stderr,	"Error: "
				"trunks_send returned non hex character %c\n",
				c);
			return -1;
		}
		byte = nibble << 4;

		c = response[2 * i + 1];
		nibble = from_hexascii(c);
		if (nibble < 0) {
			fprintf(stderr,	"Error: "
				"trunks_send returned non hex character %c\n",
				c);
			return -1;
		}
		byte |= nibble;

		((uint8_t *)buf)[i] = byte;
	}

	return rv/2;
}

/*
 * Prepare and transfer a block to either to /dev/tpm0 or through trunks_send
 * --raw, get a reply.
 */
static int tpm_send_pkt(struct transfer_descriptor *td, unsigned int digest,
			unsigned int addr, const void *data, int size,
			void *response, size_t *response_size,
			uint16_t subcmd)
{
	/* Used by transfer to /dev/tpm0 */
	static uint8_t outbuf[MAX_BUF_SIZE];
	struct upgrade_pkt *out = (struct upgrade_pkt *)outbuf;
	int len, done;
	int response_offset = offsetof(struct upgrade_pkt, command.data);
	void *payload;
	size_t header_size;
	uint32_t rv;
	const size_t rx_size = sizeof(outbuf);

	debug("%s: sending to %#x %d bytes\n", __func__, addr, size);

	out->tag = htobe16(0x8001);
	out->subcmd = htobe16(subcmd);

	if (subcmd <= LAST_EXTENSION_COMMAND)
		out->ordinal = htobe32(CONFIG_EXTENSION_COMMAND);
	else
		out->ordinal = htobe32(TPM_CC_VENDOR_BIT_MASK);

	if (subcmd == EXTENSION_FW_UPGRADE) {
		/* FW Upgrade PDU header includes a couple of extra fields. */
		out->upgrade.digest = digest;
		out->upgrade.address = htobe32(addr);
		header_size = offsetof(struct upgrade_pkt, upgrade.data);
	} else {
		header_size = offsetof(struct upgrade_pkt, command.data);
	}

	payload = outbuf + header_size;
	len = size + header_size;

	out->length = htobe32(len);
	memcpy(payload, data, size);

	if (verbose_mode) {
		int i;

		debug("Writing %d bytes to TPM at %x\n", len, addr);
		for (i = 0; i < MIN(len, 20); i++)
			debug("%2.2x ", outbuf[i]);
		debug("\n");
	}

	switch (td->ep_type) {
	case dev_xfer:
		done = write(td->tpm_fd, out, len);
		break;
	case ts_xfer:
		done = ts_write(out, len);
		break;
	default:
		fprintf(stderr, "Error: %s:%d: unknown transfer type %d\n",
			__func__, __LINE__, td->ep_type);
		return -1;
	}

	if (done < 0) {
		perror("Could not write to TPM");
		return -1;
	} else if (done != len) {
		fprintf(stderr, "Error: Wrote %d bytes, expected to write %d\n",
			done, len);
		return -1;
	}

	switch (td->ep_type) {
	case dev_xfer: {
		int read_count;

		len = 0;
		do {
			uint8_t *rx_buf = outbuf + len;
			size_t rx_to_go = rx_size - len;

			read_count = read(td->tpm_fd, rx_buf, rx_to_go);

			len += read_count;
		} while (read_count);
		break;
	}
	case ts_xfer:
		len = ts_read(outbuf, rx_size);
		break;
	default:
		/*
		 * This sure will never happen, type is verifed in the
		 * previous switch statement.
		 */
		len = -1;
		break;
	}

	debug("Read %d bytes from TPM\n", len);
	if (len > 0) {
		int i;

		for (i = 0; i < len; i++)
			debug("%2.2x ", outbuf[i]);
		debug("\n");
	}
	len = len - response_offset;
	if (len < 0) {
		fprintf(stderr, "Problems reading from TPM, got %d bytes\n",
			len + response_offset);
		return -1;
	}

	if (response && response_size) {
		len = MIN(len, *response_size);
		memcpy(response, outbuf + response_offset, len);
		*response_size = len;
	}

	/* Return the actual return code from the TPM response header. */
	memcpy(&rv, &((struct upgrade_pkt *)outbuf)->ordinal, sizeof(rv));
	rv = be32toh(rv);

	/* Clear out vendor command return value offset.*/
	if ((rv & VENDOR_RC_ERR) == VENDOR_RC_ERR)
		rv &= ~VENDOR_RC_ERR;

	return rv;
}

/* Release USB device and return error to the OS. */
static void shut_down(struct usb_endpoint *uep)
{
	usb_shut_down(uep);
	exit(update_error);
}

static void usage(int errs)
{
	size_t i;
	const int indent = 27; /* This is the size used by gsctool all along. */

	printf("\nUsage: %s [options] [<binary image>]\n"
	       "\n"
	       "This utility allows to update Cr50 RW firmware, configure\n"
	       "various aspects of Cr50 operation, analyze Cr50 binary\n"
	       "images, etc.\n\n"
	       "<binary image> is the file name of a full RO+RW binary image.\n"
	       "\n"
	       "Options:\n\n",
	       progname);

	for (i = 0; i < ARRAY_SIZE(cmd_line_options); i++) {
		const char *help_text = cmd_line_options[i].help_text;
		int printed_length;
		const char *separator;

		/*
		 * First print the short and long forms of the command line
		 * option.
		 */
		printed_length = printf(" -%c,--%s",
					cmd_line_options[i].opt.val,
					cmd_line_options[i].opt.name);

		/*
		 * If there is something to print immediately after the
		 * options, print it.
		 */
		separator = strchr(help_text, '%');
		if (separator) {
			char buffer[80];
			size_t extra_size;

			extra_size = separator - help_text;
			if (extra_size >= sizeof(buffer)) {
				fprintf(stderr, "misformatted help text: %s\n",
					help_text);
				exit(-1);
			}
			memcpy(buffer, help_text, extra_size);
			buffer[extra_size] = '\0';
			printed_length += printf(" %s", buffer);
			help_text = separator + 1;
		}

		/*
		 * If printed length exceeds or is too close to indent, print
		 * help text on the next line.
		 */
		if (printed_length >= (indent - 1)) {
			printf("\n");
			printed_length = 0;
		}

		while (printed_length++ < indent)
			printf(" ");
		printf("%s\n", help_text);
	}
	printf("\n");
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

struct update_pdu {
	uint32_t block_size; /* Total block size, include this field's size. */
	struct upgrade_command cmd;
	/* The actual payload goes here. */
};

static void do_xfer(struct usb_endpoint *uep, void *outbuf, int outlen,
		    void *inbuf, int inlen, int allow_less, size_t *rxed_count)
{
	if (usb_trx(uep, outbuf, outlen, inbuf, inlen, allow_less, rxed_count))
		shut_down(uep);
}

static int transfer_block(struct usb_endpoint *uep, struct update_pdu *updu,
			  uint8_t *transfer_data_ptr, size_t payload_size)
{
	size_t transfer_size;
	uint32_t reply;
	int actual;
	int r;

	/* First send the header. */
	do_xfer(uep, updu, sizeof(*updu), NULL, 0, 0, NULL);

	/* Now send the block, chunk by chunk. */
	for (transfer_size = 0; transfer_size < payload_size;) {
		int chunk_size;

		chunk_size = MIN(uep->chunk_len, payload_size - transfer_size);
		do_xfer(uep, transfer_data_ptr, chunk_size, NULL, 0, 0, NULL);
		transfer_data_ptr += chunk_size;
		transfer_size += chunk_size;
	}

	/* Now get the reply. */
	r = libusb_bulk_transfer(uep->devh, uep->ep_num | 0x80,
				 (void *) &reply, sizeof(reply),
				 &actual, 1000);
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

	/*
	 * Make sure total size is 4 bytes aligned, this is required for
	 * successful flashing.
	 */
	data_len = (data_len + 3) &  ~3;

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

			block_addr = section_addr;

			/*
			 * A single byte response is expected, but let's give
			 * the driver a few extra bytes to catch cases when a
			 * different amount of data is transferred (which
			 * would indicate a synchronization problem).
			 */
			if (tpm_send_pkt(td,
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

	for (i = 0; i < ARRAY_SIZE(fields[0]); i++) {
		uint32_t a_value;
		uint32_t b_value;

		a_value = fields[0][i];
		b_value = fields[1][i];

		/*
		 * Let's filter out images where the section is not
		 * initialized and the version field value is set to all ones.
		 */
		if (a_value == 0xffffffff)
			a_value = 0;

		if (b_value == 0xffffffff)
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

	if (td->ep_type == usb_xfer) {
		struct update_pdu updu;

		memset(&updu, 0, sizeof(updu));
		updu.block_size = htobe32(sizeof(updu));
		do_xfer(&td->uep, &updu, sizeof(updu), &start_resp,
			sizeof(start_resp), 1, &rxed_size);
	} else {
		rxed_size = sizeof(start_resp);
		if (tpm_send_pkt(td, 0, 0, NULL, 0,
				 &start_resp, &rxed_size,
				 EXTENSION_FW_UPGRADE) < 0) {
			fprintf(stderr, "Failed to start transfer\n");
			exit(update_error);
		}
	}

	/* We got something. Check for errors in response */
	if (rxed_size < 8) {
		fprintf(stderr, "Unexpected response size %zd: ", rxed_size);
		for (i = 0; i < rxed_size; i++)
			fprintf(stderr, " %02x", ((uint8_t *)&start_resp)[i]);
		fprintf(stderr, "\n");
		exit(update_error);
	}

	protocol_version = be32toh(start_resp.rpdu.protocol_version);
	if (protocol_version < 5) {
		fprintf(stderr, "Unsupported protocol version %d\n",
			protocol_version);
		exit(update_error);
	}

	printf("target running protocol version %d\n", protocol_version);

	error_code = be32toh(start_resp.rpdu.return_value);

	if (error_code) {
		fprintf(stderr, "Target reporting error %d\n", error_code);
		if (td->ep_type == usb_xfer)
			shut_down(&td->uep);
		exit(update_error);
	}

	td->rw_offset = be32toh(start_resp.rpdu.backup_rw_offset);
	td->ro_offset = be32toh(start_resp.rpdu.backup_ro_offset);

	/* Running header versions. */
	for (i = 0; i < ARRAY_SIZE(targ.shv); i++) {
		targ.shv[i].minor = be32toh(start_resp.rpdu.shv[i].minor);
		targ.shv[i].major = be32toh(start_resp.rpdu.shv[i].major);
		targ.shv[i].epoch = be32toh(start_resp.rpdu.shv[i].epoch);
	}

	for (i = 0; i < ARRAY_SIZE(targ.keyid); i++)
		targ.keyid[i] = be32toh(start_resp.rpdu.keyid[i]);

	printf("keyids: RO 0x%08x, RW 0x%08x\n", targ.keyid[0], targ.keyid[1]);
	printf("offsets: backup RO at %#x, backup RW at %#x\n",
	       td->ro_offset, td->rw_offset);

	pick_sections(td);
}

/*
 * Channel TPM extension/vendor command over USB. The payload of the USB frame
 * in this case consists of the 2 byte subcommand code concatenated with the
 * command body. The caller needs to indicate if a response is expected, and
 * if it is - of what maximum size.
 */
static int ext_cmd_over_usb(struct usb_endpoint *uep, uint16_t subcommand,
			    const void *cmd_body, size_t body_size,
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
		fprintf(stderr, "%s: failed to allocate %zd bytes\n",
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

	do_xfer(uep, ufh, usb_msg_size, resp,
		resp_size ? *resp_size : 0, 1, resp_size);

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
	do_xfer(uep, &out, sizeof(out), &out, 1, 0, NULL);
}

/* Returns number of successfully transmitted image sections. */
static int transfer_image(struct transfer_descriptor *td,
			       uint8_t *data, size_t data_len)
{
	size_t j;
	int num_txed_sections = 0;

	/*
	 * In case both RO and RW updates are required, make sure the RW
	 * section is updated before the RO. The array below keeps sections
	 * offsets in the required order.
	 */
	const size_t update_order[] = {CONFIG_RW_MEM_OFF,
				       CONFIG_RW_B_MEM_OFF,
				       CONFIG_RO_MEM_OFF,
				       CHIP_RO_B_MEM_OFF};

	for (j = 0; j < ARRAY_SIZE(update_order); j++) {
		size_t i;

		for (i = 0; i < ARRAY_SIZE(sections); i++) {
			if (sections[i].offset != update_order[j])
				continue;

			if (sections[i].ustatus != needed)
				break;

			transfer_section(td,
					 data + sections[i].offset,
					 sections[i].offset,
					 sections[i].size);
			num_txed_sections++;
		}
	}

	if (!num_txed_sections)
		printf("nothing to do\n");
	else
		printf("-------\nupdate complete\n");
	return num_txed_sections;
}

uint32_t send_vendor_command(struct transfer_descriptor *td,
			     uint16_t subcommand,
			     const void *command_body,
			     size_t command_body_size,
			     void *response,
			     size_t *response_size)
{
	int32_t rv;

	if (td->ep_type == usb_xfer) {
		/*
		 * When communicating over USB the response is always supposed
		 * to have the result code in the first byte of the response,
		 * to be stripped from the actual response body by this
		 * function.
		 */
		uint8_t temp_response[MAX_BUF_SIZE];
		size_t max_response_size;

		if (!response_size) {
			max_response_size = 1;
		} else if (*response_size < (sizeof(temp_response))) {
			max_response_size = *response_size + 1;
		} else {
			fprintf(stderr,
				"Error: Expected response too large (%zd)\n",
				*response_size);
			/* Should happen only when debugging. */
			exit(update_error);
		}

		ext_cmd_over_usb(&td->uep, subcommand,
				 command_body, command_body_size,
				 temp_response, &max_response_size);
		if (!max_response_size) {
			/*
			 * we must be talking to an older Cr50 firmware, which
			 * does not return the result code in the first byte
			 * on success, nothing to do.
			 */
			if (response_size)
				*response_size = 0;
			rv = 0;
		} else {
			rv = temp_response[0];
			if (response_size) {
				*response_size = max_response_size - 1;
				memcpy(response,
				       temp_response + 1, *response_size);
			}
		}
	} else {

		rv = tpm_send_pkt(td, 0, 0,
				  command_body, command_body_size,
				  response, response_size, subcommand);

		if (rv == -1) {
			fprintf(stderr,
				"Error: Failed to send vendor command %d\n",
				subcommand);
			exit(update_error);
		}
	}

	return rv; /* This will be converted into uint32_t */
}

/*
 * Corrupt the header of the inactive rw image to make sure the system can't
 * rollback
 */
static void invalidate_inactive_rw(struct transfer_descriptor *td)
{
	/* Corrupt the rw image that is not running. */
	uint32_t rv;

	rv = send_vendor_command(td, VENDOR_CC_INVALIDATE_INACTIVE_RW,
				 NULL, 0, NULL, NULL);
	if (!rv) {
		printf("Inactive header invalidated\n");
		return;
	}

	fprintf(stderr, "*%s: Error %#x\n", __func__, rv);
	exit(update_error);
}

static struct signed_header_version ver19 = {
	.epoch = 0,
	.major = 0,
	.minor = 19,
};

static void generate_reset_request(struct transfer_descriptor *td)
{
	size_t response_size;
	uint8_t response;
	uint16_t subcommand;
	uint8_t command_body[2]; /* Max command body size. */
	size_t command_body_size;
	uint32_t background_update_supported;
	const char *reset_type;
	int rv;

	if (protocol_version < 6) {
		if (td->ep_type == usb_xfer) {
			/*
			 * Send a second stop request, which should reboot
			 * without replying.
			 */
			send_done(&td->uep);
		}
		/* Nothing we can do over /dev/tpm0 running versions below 6. */
		return;
	}

	/* RW version 0.0.19 and above has support for background updates. */
	background_update_supported = td->background_update_supported ||
				!a_newer_than_b(&ver19, &targ.shv[1]);

	/*
	 * If this is an upstart request and there is support for background
	 * updates, don't post a request now. The target should handle it on
	 * the next reboot.
	 */
	if (td->upstart_mode && background_update_supported)
		return;

	/*
	 * If the user explicitly wants it or a reset is needed because h1
	 * does not support background updates, request post reset instead of
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
	if (td->post_reset || td->upstart_mode) {
		subcommand = EXTENSION_POST_RESET;
		reset_type = "posted";
	} else if (background_update_supported) {
		subcommand = VENDOR_CC_TURN_UPDATE_ON;
		command_body_size = sizeof(command_body);
		command_body[0] = 0;
		command_body[1] = 100;  /* Reset in 100 ms. */
		reset_type = "requested";
	} else {
		response_size = 0;
		subcommand = VENDOR_CC_IMMEDIATE_RESET;
		reset_type = "triggered";
	}

	rv = send_vendor_command(td, subcommand, command_body,
				 command_body_size, &response, &response_size);

	if (rv) {
		fprintf(stderr, "*%s: Error %#x\n", __func__, rv);
		exit(update_error);
	}
	printf("reboot %s\n", reset_type);
}

/*
 * Machine output is formatted as "key=value", one key-value pair per line, and
 * parsed by other programs (e.g., debugd). The value part should be specified
 * in the printf-like way. For example:
 *
 *           print_machine_output("date", "%d/%d/%d", 2018, 1, 1),
 *
 * which outputs this line in console:
 *
 *           date=2018/1/1
 *
 * The key part should not contain '=' or newline. The value part may contain
 * special characters like spaces, quotes, brackets, but not newlines. The
 * newline character means end of value.
 *
 * Any output format change in this function may require similar changes on the
 * programs that are using this gsctool.
 */
__attribute__((__format__(__printf__, 2, 3)))
static void print_machine_output(const char *key, const char *format, ...)
{
	va_list args;

	if (strchr(key, '=') != NULL || strchr(key, '\n') != NULL) {
		fprintf(stderr,
			"Error: key %s contains '=' or a newline character.\n",
			key);
		return;
	}

	if (strchr(format, '\n') != NULL) {
		fprintf(stderr,
			"Error: value format %s contains a newline character. "
			"\n",
			format);
		return;
	}

	va_start(args, format);

	printf("%s=", key);
	vprintf(format, args);
	printf("\n");

	va_end(args);
}

/*
 * Prints out the header, including FW versions and board IDs, of the given
 * image. Output in a machine-friendly format if show_machine_output is true.
 */
static int show_headers_versions(const void *image, bool show_machine_output)
{
	/*
	 * There are 2 FW slots in an image, and each slot has 2 sections, RO
	 * and RW. The 2 slots should have identical FW versions and board
	 * IDs.
	 */
	const struct {
		const char *name;
		uint32_t offset;
	} sections[] = {
		/* Slot A. */
		{"RO", CONFIG_RO_MEM_OFF},
		{"RW", CONFIG_RW_MEM_OFF},
		/* Slot B. */
		{"RO", CHIP_RO_B_MEM_OFF},
		{"RW", CONFIG_RW_B_MEM_OFF}
	};
	const size_t kNumSlots = 2;
	const size_t kNumSectionsPerSlot = 2;

	/*
	 * String representation of FW version (<epoch>:<major>:<minor>), one
	 * string for each FW section.
	 */
	char ro_fw_ver[kNumSlots][MAX_FW_VER_LENGTH];
	char rw_fw_ver[kNumSlots][MAX_FW_VER_LENGTH];

	uint32_t dev_id0_[kNumSlots];
	uint32_t dev_id1_[kNumSlots];
	uint32_t print_devid = 0;

	struct board_id {
		uint32_t id;
		uint32_t mask;
		uint32_t flags;
	} bid[kNumSlots];

	char bid_string[kNumSlots][MAX_BOARD_ID_LENGTH];

	size_t i;

	for (i = 0; i < ARRAY_SIZE(sections); i++) {
		const struct SignedHeader *h =
			(const struct SignedHeader *)
				((uintptr_t)image + sections[i].offset);
		const size_t slot_idx = i / kNumSectionsPerSlot;

		uint32_t cur_bid;
		size_t j;

		if (sections[i].name[1] == 'O') {
			/* RO. */
			snprintf(ro_fw_ver[slot_idx], MAX_FW_VER_LENGTH,
				 "%d.%d.%d", h->epoch_, h->major_, h->minor_);
			/* No need to read board ID in an RO section. */
			continue;
		} else {
			/* RW. */
			snprintf(rw_fw_ver[slot_idx], MAX_FW_VER_LENGTH,
				 "%d.%d.%d", h->epoch_, h->major_, h->minor_);
		}

		/*
		 * For RW sections, retrieves the board ID fields' contents,
		 * which are stored XORed with a padding value.
		 */
		bid[slot_idx].id = h->board_id_type ^ SIGNED_HEADER_PADDING;
		bid[slot_idx].mask =
			h->board_id_type_mask ^ SIGNED_HEADER_PADDING;
		bid[slot_idx].flags = h->board_id_flags ^ SIGNED_HEADER_PADDING;

		dev_id0_[slot_idx] = h->dev_id0_;
		dev_id1_[slot_idx] = h->dev_id1_;
		/* Print the devid if any slot has a non-zero devid. */
		print_devid |= h->dev_id0_ | h->dev_id1_;

		/*
		 * If board ID is a 4-uppercase-letter string (as it ought to
		 * be), print it as 4 letters, otherwise print it as an 8-digit
		 * hex.
		 */
		cur_bid = bid[slot_idx].id;
		for (j = 0; j < sizeof(cur_bid); ++j)
			if (!isupper(((const char *)&cur_bid)[j]))
				break;

		if (j == sizeof(cur_bid)) {
			cur_bid = be32toh(cur_bid);
			snprintf(bid_string[slot_idx], MAX_BOARD_ID_LENGTH,
				 "%.4s", (const char *)&cur_bid);
		} else {
			snprintf(bid_string[slot_idx], MAX_BOARD_ID_LENGTH,
				 "%08x", cur_bid);
		}
	}

	if (show_machine_output) {
		print_machine_output("IMAGE_RO_FW_VER", "%s", ro_fw_ver[0]);
		print_machine_output("IMAGE_RW_FW_VER", "%s", rw_fw_ver[0]);
		print_machine_output("IMAGE_BID_STRING", "%s", bid_string[0]);
		print_machine_output("IMAGE_BID_MASK", "%08x", bid[0].mask);
		print_machine_output("IMAGE_BID_FLAGS", "%08x", bid[0].flags);
	} else {
		printf("RO_A:%s RW_A:%s[%s:%08x:%08x] ",
		       ro_fw_ver[0], rw_fw_ver[0],
		       bid_string[0], bid[0].mask, bid[0].flags);
		printf("RO_B:%s RW_B:%s[%s:%08x:%08x]\n",
		       ro_fw_ver[1], rw_fw_ver[1],
		       bid_string[1], bid[1].mask, bid[1].flags);

		if (print_devid) {
			printf("DEVID: 0x%08x 0x%08x", dev_id0_[0],
			       dev_id1_[0]);
			/*
			 * Only print the second devid if it's different.
			 * Separate the devids with tabs, so it's easier to
			 * read.
			 */
			if (dev_id0_[0] != dev_id0_[1] ||
			    dev_id1_[0] != dev_id1_[1])
				printf("\t\t\t\tDEVID_B: 0x%08x 0x%08x",
				       dev_id0_[1], dev_id1_[1]);
			printf("\n");
		}
	}

	return 0;
}

/*
 * The default flag value will allow to run images built for any hardware
 * generation of a particular board ID.
 */
#define DEFAULT_BOARD_ID_FLAG 0xff00
static int parse_bid(const char *opt,
		     struct board_id *bid,
		     enum board_id_action *bid_action)
{
	char *e;
	const char *param2;
	size_t param1_length;

	if (!opt) {
		*bid_action = bid_get;
		return 1;
	}

	/* Set it here to make bailing out easier later. */
	bid->flags = DEFAULT_BOARD_ID_FLAG;

	*bid_action = bid_set;  /* Ignored by caller on errors. */

	/*
	 * Pointer to the optional second component of the command line
	 * parameter, when present - separated by a colon.
	 */
	param2 = strchr(opt, ':');
	if (param2) {
		param1_length = param2 - opt;
		param2++;
		if (!*param2)
			return 0; /* Empty second parameter. */
	} else {
		param1_length = strlen(opt);
	}

	if (!param1_length)
		return 0; /* Colon is the first character of the string? */

	if (param1_length <= 4) {
		unsigned i;

		/* Input must be a symbolic board name. */
		bid->type = 0;
		for (i = 0; i < param1_length; i++)
			bid->type = (bid->type << 8) | opt[i];
	} else {
		bid->type = (uint32_t)strtoul(opt, &e, 0);
		if ((param2 && (*e != ':')) || (!param2 && *e))
			return 0;
	}

	if (param2) {
		bid->flags = (uint32_t)strtoul(param2, &e, 0);
		if (*e)
			return 0;
	}

	return 1;
}

/*
 * Reads a two-character hexadecimal byte from a string. If the string is
 * ill-formed, returns 0. Otherwise, |byte| contains the byte value and the
 * return value is non-zero.
 */
static int read_hex_byte(const char* s, uint8_t* byte) {
	uint8_t b = 0;
	for (const char* end = s + 2; s < end; ++s) {
		if (*s >= '0' && *s <= '9')
			b = b * 16 + *s - '0';
		else if (*s >= 'A' && *s <= 'F')
			b = b * 16 + 10 + *s - 'A';
		else if (*s >= 'a' && *s <= 'f')
			b = b * 16 + 10 + *s - 'a';
		else
			return 0;
	}
	*byte = b;
	return 1;
}

static int parse_sn_bits(const char *opt, uint8_t *sn_bits)
{
	size_t len = strlen(opt);

	if (!strncmp(opt, "0x", 2)) {
		opt += 2;
		len -= 2;
	}
	if (len != SN_BITS_SIZE * 2) return 0;

	for (int i = 0; i < SN_BITS_SIZE; ++i, opt +=2)
		if (!read_hex_byte(opt, sn_bits++)) return 0;

	return 1;
}

static int parse_sn_inc_rma(const char *opt, uint8_t *arg)
{
	uint32_t inc;
	char *e;

	inc = (uint32_t)strtoul(opt, &e, 0);

	if (opt == e || *e != '\0' || inc > 7)
		return 0;

	*arg = inc;
	return 1;
}

static uint32_t common_process_password(struct transfer_descriptor *td,
					enum ccd_vendor_subcommands subcmd)
{
	size_t response_size;
	uint8_t response;
	uint32_t rv;
	char *password = NULL;
	char *password_copy = NULL;
	size_t copy_len = 0;
	size_t len = 0;
	struct termios oldattr, newattr;

	/* Suppress command line echo while password is being entered. */
	tcgetattr(STDIN_FILENO, &oldattr);
	newattr = oldattr;
	newattr.c_lflag &= ~ECHO;
	newattr.c_lflag |= (ICANON | ECHONL);
	tcsetattr(STDIN_FILENO, TCSANOW, &newattr);

	/* With command line echo suppressed request password entry twice. */
	printf("Enter password:");
	len = getline(&password, &len, stdin);
	printf("Re-enter password:");
	getline(&password_copy, &copy_len, stdin);

	/* Restore command line echo. */
	tcsetattr(STDIN_FILENO, TCSANOW, &oldattr);

	/* Empty password will still have the newline. */
	if ((len <= 1) || !password_copy) {
		if (password)
			free(password);
		if (password_copy)
			free(password_copy);
		fprintf(stderr, "Error reading password\n");
		exit(update_error);
	}

	/* Compare the two inputs. */
	if (strcmp(password, password_copy)) {
		fprintf(stderr, "Entered passwords don't match\n");
		free(password);
		free(password_copy);
		exit(update_error);
	}

	/*
	 * Ok, we have a password, let's move it in the buffer to overwrite
	 * the newline and free a byte to prepend the subcommand code.
	 */
	memmove(password + 1, password, len  - 1);
	password[0] = subcmd;
	response_size = sizeof(response);
	rv = send_vendor_command(td, VENDOR_CC_CCD,
				 password, len,
				 &response, &response_size);
	free(password);
	free(password_copy);

	if ((rv != VENDOR_RC_SUCCESS) && (rv != VENDOR_RC_IN_PROGRESS))
		fprintf(stderr, "Error sending password: rv %d, response %d\n",
			rv, response_size ? response : 0);

	return rv;
}

static void process_password(struct transfer_descriptor *td)
{
	if (common_process_password(td, CCDV_PASSWORD) == VENDOR_RC_SUCCESS)
		return;

	exit(update_error);
}

void poll_for_pp(struct transfer_descriptor *td,
		 uint16_t command,
		 uint8_t poll_type)
{
	uint8_t response;
	uint8_t prev_response;
	size_t response_size;
	int rv;

	prev_response = ~0; /* Guaranteed invalid value. */

	while (1) {
		response_size = sizeof(response);
		rv = send_vendor_command(td, command,
					 &poll_type, sizeof(poll_type),
					 &response, &response_size);

		if (((rv != VENDOR_RC_SUCCESS) && (rv != VENDOR_RC_IN_PROGRESS))
		    || (response_size != 1)) {
			fprintf(stderr, "Error: rv %d, response %d\n",
				rv, response_size ? response : 0);
			exit(update_error);
		}

		if (response == CCD_PP_DONE) {
			printf("PP Done!\n");
			return;
		}

		if (response == CCD_PP_CLOSED) {
			fprintf(stderr,
				"Error: Physical presence check timeout!\n");
			exit(update_error);
		}


		if (response == CCD_PP_AWAITING_PRESS) {
			printf("Press PP button now!\n");
		} else if (response == CCD_PP_BETWEEN_PRESSES) {
			if (prev_response != response)
				printf("Another press will be required!\n");
		} else {
			fprintf(stderr, "Error: unknown poll result %d\n",
				response);
			exit(update_error);
		}
		prev_response = response;

		usleep(500 * 1000); /* Poll every half a second. */
	}

}

static void print_ccd_info(void *response, size_t response_size)
{
	struct ccd_info_response ccd_info;
	size_t i;
	const struct ccd_capability_info cap_info[] = CAP_INFO_DATA;
	const char *state_names[] = CCD_STATE_NAMES;
	const char *cap_state_names[] = CCD_CAP_STATE_NAMES;
	uint32_t caps_bitmap = 0;

	if (response_size != sizeof(ccd_info)) {
		fprintf(stderr, "Unexpected CCD info response size %zd\n",
			response_size);
		exit(update_error);
	}

	memcpy(&ccd_info, response, sizeof(ccd_info));

	/* Convert it back to host endian format. */
	ccd_info.ccd_flags = be32toh(ccd_info.ccd_flags);
	for (i = 0; i < ARRAY_SIZE(ccd_info.ccd_caps_current); i++) {
		ccd_info.ccd_caps_current[i] =
			be32toh(ccd_info.ccd_caps_current[i]);
		ccd_info.ccd_caps_defaults[i] =
			be32toh(ccd_info.ccd_caps_defaults[i]);
	}

	/* Now report CCD state on the console. */
	printf("State: %s\n", ccd_info.ccd_state > ARRAY_SIZE(state_names) ?
	       "Error" : state_names[ccd_info.ccd_state]);
	printf("Password: %s\n", (ccd_info.ccd_indicator_bitmap &
		      CCD_INDICATOR_BIT_HAS_PASSWORD) ? "Set" : "None");
	printf("Flags: %#06x\n", ccd_info.ccd_flags);
	printf("Capabilities, current and default:\n");
	for (i = 0; i < CCD_CAP_COUNT; i++) {
		int is_enabled;
		int index;
		int shift;
		int cap_current;
		int cap_default;

		index = i / (32 / CCD_CAP_BITS);
		shift = (i % (32 / CCD_CAP_BITS)) * CCD_CAP_BITS;

		cap_current = (ccd_info.ccd_caps_current[index] >> shift)
							 & CCD_CAP_BITMASK;
		cap_default = (ccd_info.ccd_caps_defaults[index] >> shift)
							 & CCD_CAP_BITMASK;

		if (ccd_info.ccd_force_disabled) {
			is_enabled = 0;
		} else {
			switch (cap_current) {
			case CCD_CAP_STATE_ALWAYS:
				is_enabled = 1;
				break;
			case CCD_CAP_STATE_UNLESS_LOCKED:
				is_enabled = (ccd_info.ccd_state !=
					      CCD_STATE_LOCKED);
				break;
			default:
				is_enabled = (ccd_info.ccd_state ==
					      CCD_STATE_OPENED);
				break;
			}
		}

		printf("  %-15s %c %s",
		       cap_info[i].name,
		       is_enabled ? 'Y' : '-',
		       cap_state_names[cap_current]);

		if (cap_current != cap_default)
			printf("  (%s)", cap_state_names[cap_default]);

		printf("\n");

		if (is_enabled)
			caps_bitmap |= (1 << i);
	}
	printf("CCD caps bitmap: %#x\n", caps_bitmap);
	printf("Capabilities are %s.\n", (ccd_info.ccd_indicator_bitmap &
		 CCD_INDICATOR_BIT_ALL_CAPS_DEFAULT) ? "default" : "modified");
}

static void process_ccd_state(struct transfer_descriptor *td, int ccd_unlock,
			      int ccd_open, int ccd_lock, int ccd_info)
{
	uint8_t payload;
	 /* Max possible response size is when ccd_info is requested. */
	uint8_t response[sizeof(struct ccd_info_response)];
	size_t response_size;
	int rv;

	if (ccd_unlock)
		payload = CCDV_UNLOCK;
	else if (ccd_open)
		payload = CCDV_OPEN;
	else if (ccd_lock)
		payload = CCDV_LOCK;
	else
		payload = CCDV_GET_INFO;

	response_size = sizeof(response);
	rv = send_vendor_command(td, VENDOR_CC_CCD,
				 &payload, sizeof(payload),
				 &response, &response_size);

	/*
	 * If password is required - try sending the same subcommand
	 * accompanied by user password.
	 */
	if (rv == VENDOR_RC_PASSWORD_REQUIRED)
		rv = common_process_password(td, payload);

	if (rv == VENDOR_RC_SUCCESS) {
		if (ccd_info)
			print_ccd_info(response, response_size);
		return;
	}

	if (rv != VENDOR_RC_IN_PROGRESS) {
		fprintf(stderr, "Error: rv %d, response %d\n",
			rv, response_size ? response[0] : 0);
		exit(update_error);
	}

	/*
	 * Physical presence process started, poll for the state the user
	 * asked for. Only two subcommands would return 'IN_PROGRESS'.
	 */
	if (ccd_unlock)
		poll_for_pp(td, VENDOR_CC_CCD, CCDV_PP_POLL_UNLOCK);
	else
		poll_for_pp(td, VENDOR_CC_CCD, CCDV_PP_POLL_OPEN);
}

static void process_wp(struct transfer_descriptor *td)
{
	size_t response_size;
	uint8_t response;
	int rv = 0;

	response_size = sizeof(response);

	printf("Getting WP\n");

	rv = send_vendor_command(td, VENDOR_CC_WP, NULL, 0,
				 &response, &response_size);
	if (rv != VENDOR_RC_SUCCESS) {
		fprintf(stderr, "Error %d getting write protect\n", rv);
		exit(update_error);
	}
	if (response_size != sizeof(response)) {
		fprintf(stderr, "Unexpected response size %zd while getting "
			"write protect\n",
			response_size);
		exit(update_error);
	}

	printf("WP: %08x\n", response);
	printf("Flash WP: %s%s\n",
		response & WPV_FORCE ? "forced " : "",
		response & WPV_ENABLE ? "enabled" : "disabled");
	printf(" at boot: %s\n",
		!(response & WPV_ATBOOT_SET) ? "follow_batt_pres" :
		response & WPV_ATBOOT_ENABLE ? "forced enabled" :
		"forced disabled");
}


void process_bid(struct transfer_descriptor *td,
		 enum board_id_action bid_action,
		 struct board_id *bid,
		 bool show_machine_output)
{
	size_t response_size;

	if (bid_action == bid_get) {

		response_size = sizeof(*bid);
		send_vendor_command(td, VENDOR_CC_GET_BOARD_ID,
				    bid, sizeof(*bid),
				    bid, &response_size);

		if (response_size != sizeof(*bid)) {
			fprintf(stderr,
				"Error reading board ID: response size %zd, "
				"first byte %#02x\n",
				response_size,
				response_size ? *(uint8_t *)&bid : -1);
			exit(update_error);
		}

		if (show_machine_output) {
			print_machine_output(
				"BID_TYPE", "%08x", be32toh(bid->type));
			print_machine_output(
				"BID_TYPE_INV", "%08x", be32toh(bid->type_inv));
			print_machine_output(
				"BID_FLAGS", "%08x", be32toh(bid->flags));

			for (int i = 0; i < 4; i++) {
				if (!isupper(((const char *)bid)[i])) {
					print_machine_output(
						"BID_RLZ", "%s", "????");
					return;
				}
			}

			print_machine_output(
				"BID_RLZ", "%c%c%c%c",
				((const char *)bid)[0],
				((const char *)bid)[1],
				((const char *)bid)[2],
				((const char *)bid)[3]);
		} else {
			printf("Board ID space: %08x:%08x:%08x\n",
			       be32toh(bid->type),
			       be32toh(bid->type_inv),
			       be32toh(bid->flags));

		}

		return;
	}

	if (bid_action == bid_set) {
		/* Sending just two fields: type and flags. */
		uint32_t command_body[2];
		uint8_t response;

		command_body[0] = htobe32(bid->type);
		command_body[1] = htobe32(bid->flags);

		response_size = sizeof(command_body);
		send_vendor_command(td, VENDOR_CC_SET_BOARD_ID,
				    command_body, sizeof(command_body),
				    command_body, &response_size);

		/*
		 * Speculative assignment: the response is expected to be one
		 * byte in size and be placed in the first byte of the buffer.
		 */
		response = *((uint8_t *)command_body);

		if (response_size == 1) {
			if (!response)
				return; /* Success! */

			fprintf(stderr, "Error %d while setting board id\n",
				response);
		} else {
			fprintf(stderr, "Unexpected response size %zd"
				" while setting board id\n",
				response_size);
		}
		exit(update_error);
	}
}

static void process_sn_bits(struct transfer_descriptor *td,
			    uint8_t *sn_bits)
{
	int rv;
	uint8_t response_code;
	size_t response_size = sizeof(response_code);

	rv = send_vendor_command(td, VENDOR_CC_SN_SET_HASH,
				 sn_bits, SN_BITS_SIZE,
				 &response_code, &response_size);

	if (rv) {
		fprintf(stderr, "Error %d while sending vendor command\n", rv);
		exit(update_error);
	}

	if (response_size != 1) {
		fprintf(stderr,
			"Unexpected response size while setting sn bits\n");
		exit(update_error);
	}

	if (response_code != 0) {
		fprintf(stderr, "Error %d while setting sn bits\n",
			response_code);
		exit(update_error);
	}
}

static void process_sn_inc_rma(struct transfer_descriptor *td,
			       uint8_t arg)
{
	int rv;
	uint8_t response_code;
	size_t response_size = sizeof(response_code);

	rv = send_vendor_command(td, VENDOR_CC_SN_INC_RMA,
				 &arg, sizeof(arg),
				 &response_code, &response_size);
	if (rv) {
		fprintf(stderr, "Error %d while sending vendor command\n", rv);
		exit(update_error);
	}

	if (response_size != 1) {
		fprintf(stderr,
			"Unexpected response size while "
			"incrementing sn rma count\n");
		exit(update_error);
	}

	if (response_code != 0) {
		fprintf(stderr, "Error %d while incrementing rma count\n",
			response_code);
		exit(update_error);
	}
}

/* Get/Set the primary seed of the info1 manufacture state. */
static int process_endorsement_seed(struct transfer_descriptor *td,
				    const char *endorsement_seed_str)
{
	uint8_t endorsement_seed[32];
	uint8_t response_seed[32];
	size_t seed_size = sizeof(endorsement_seed);
	size_t response_size = sizeof(response_seed);
	size_t i;
	int rv;

	if (!endorsement_seed_str) {
		rv = send_vendor_command(td, VENDOR_CC_ENDORSEMENT_SEED, NULL,
					 0, response_seed, &response_size);
		if (rv) {
			fprintf(stderr, "Error sending vendor command %d\n",
				rv);
			return update_error;
		}
		printf("Endorsement key seed: ");
		for (i = 0; i < response_size; i++)
			printf("%02x", response_seed[i]);
		printf("\n");
		return 0;
	}
	if (seed_size * 2 != strlen(endorsement_seed_str)) {
		printf("Invalid seed %s\n", endorsement_seed_str);
		return update_error;
	}

	for (i = 0; i < seed_size; i++) {
		int nibble;
		char c;

		c = endorsement_seed_str[2 * i];
		nibble = from_hexascii(c);
		if (nibble < 0) {
			fprintf(stderr,	"Error: Non hex character in seed %c\n",
				c);
			return update_error;
		}
		endorsement_seed[i] = nibble << 4;

		c = endorsement_seed_str[2 * i + 1];
		nibble = from_hexascii(c);
		if (nibble < 0) {
			fprintf(stderr,	"Error: Non hex character in seed %c\n",
				c);
			return update_error;
		}
		endorsement_seed[i] |= nibble;
	}

	printf("Setting seed: %s\n", endorsement_seed_str);
	rv = send_vendor_command(td, VENDOR_CC_ENDORSEMENT_SEED,
				 endorsement_seed, seed_size,
				 response_seed, &response_size);
	if (rv == VENDOR_RC_NOT_ALLOWED) {
		fprintf(stderr, "Seed already set\n");
		return update_error;
	}
	if (rv) {
		fprintf(stderr, "Error sending vendor command %d\n", rv);
		return update_error;
	}
	printf("Updated endorsement key seed.\n");
	return 0;
}

/*
 * Retrieve the RMA authentication challenge from the Cr50, print out the
 * challenge on the console, then prompt the user for the authentication code,
 * and send the code back to Cr50. The Cr50 would report if the code matched
 * its expectations or not.
 */
static void process_rma(struct transfer_descriptor *td, const char *authcode)
{
	char rma_response[81];
	size_t response_size = sizeof(rma_response);
	size_t i;
	size_t auth_size = 0;

	if (!authcode) {
		send_vendor_command(td, VENDOR_CC_RMA_CHALLENGE_RESPONSE,
				    NULL, 0, rma_response, &response_size);

		if (response_size == 1) {
			fprintf(stderr, "error %d\n", rma_response[0]);
			if (td->ep_type == usb_xfer)
				shut_down(&td->uep);
			exit(update_error);
		}

		printf("Challenge:");
		for (i = 0; i < response_size; i++) {
			if (!(i % 5)) {
				if (!(i % 40))
					printf("\n");
				printf(" ");
			}
			printf("%c", rma_response[i]);
		}
		printf("\n");
		return;
	}

	if (!*authcode) {
		printf("Empty response.\n");
		exit(update_error);
		return;
	}

	if (!strcmp(authcode, "disable")) {
		printf("Invalid arg. Try using 'gsctool -F disable'\n");
		exit(update_error);
		return;
	}

	printf("Processing response...\n");
	auth_size = strlen(authcode);
	response_size = sizeof(rma_response);

	send_vendor_command(td, VENDOR_CC_RMA_CHALLENGE_RESPONSE,
			    authcode, auth_size,
			    rma_response, &response_size);

	if (response_size == 1) {
		fprintf(stderr, "\nrma unlock failed, code %d ",
			*rma_response);
		switch (*rma_response) {
		case VENDOR_RC_BOGUS_ARGS:
			fprintf(stderr, "(wrong authcode size)\n");
			break;
		case VENDOR_RC_INTERNAL_ERROR:
			fprintf(stderr, "(authcode mismatch)\n");
			break;
		default:
			fprintf(stderr, "(unknown error)\n");
			break;
		}
		if (td->ep_type == usb_xfer)
			shut_down(&td->uep);
		exit(update_error);
	}
	printf("RMA unlock succeeded.\n");
}

/*
 * Enable or disable factory mode. Factory mode will only be enabled if HW
 * write protect is removed.
 */
static void process_factory_mode(struct transfer_descriptor *td,
				 const char *arg)
{
	uint8_t rma_response;
	size_t response_size = sizeof(rma_response);
	char *cmd_str;
	int rv;
	uint16_t subcommand;

	if (!strcasecmp(arg, "disable")) {
		subcommand = VENDOR_CC_DISABLE_FACTORY;
		cmd_str = "dis";
	} else if (!strcasecmp(arg, "enable")) {
		subcommand = VENDOR_CC_RESET_FACTORY;
		cmd_str = "en";

	} else {
		fprintf(stderr, "Invalid factory mode arg %s", arg);
		exit(update_error);
	}

	printf("%sabling factory mode\n", cmd_str);
	rv = send_vendor_command(td, subcommand, NULL, 0, &rma_response,
		&response_size);
	if (rv) {
		fprintf(stderr, "Failed %sabling factory mode\nvc error "
			"%d\n", cmd_str, rv);
		if (response_size == 1)
			fprintf(stderr, "ec error %d\n", rma_response);
		exit(update_error);
	}
	printf("Factory %sable succeeded.\n", cmd_str);
}

static void report_version(void)
{
	/* Get version from the generated file, ignore the underscore prefix. */
	const char *v = strchr(VERSION, '_');

	printf("Version: %s, built on %s by %s\n", v ? v + 1 : "?",
	       DATE, BUILDER);
	exit(0);
}

/*
 * Either change or query TPM mode value.
 */
static int process_tpm_mode(struct transfer_descriptor *td,
				const char *arg)
{
	int rv;
	size_t command_size;
	size_t response_size;
	uint8_t response;
	uint8_t command_body;

	response_size = sizeof(response);
	if (!arg) {
		command_size = 0;
	} else if (!strcasecmp(arg, "disable")) {
		command_size = sizeof(command_body);
		command_body = (uint8_t) TPM_MODE_DISABLED;
	} else if (!strcasecmp(arg, "enable")) {
		command_size = sizeof(command_body);
		command_body = (uint8_t) TPM_MODE_ENABLED;
	} else {
		fprintf(stderr, "Invalid tpm mode arg: %s.\n", arg);
		return update_error;
	}

	rv = send_vendor_command(td, VENDOR_CC_TPM_MODE,
				&command_body, command_size,
				&response, &response_size);
	if (rv) {
		fprintf(stderr, "Error %d in setting TPM mode.\n", rv);
		return update_error;
	}
	if (response_size != sizeof(response)) {
		fprintf(stderr, "Error in the size of response,"
						" %zu.\n", response_size);
		return update_error;
	}
	if (response >= TPM_MODE_MAX) {
		fprintf(stderr, "Error in the value of response,"
						" %d.\n", response);
		return update_error;
	}

	printf("TPM Mode: %s (%d)\n", (response == TPM_MODE_DISABLED) ?
				"disabled" : "enabled", response);

	return rv;
}

/*
 * Retrieve from H1 flash log entries which are newer than the passed in
 * timestamp.
 *
 * On error retry a few times just in case flash log is locked by a concurrent
 * access.
 */
static int process_get_flog(struct transfer_descriptor *td, uint32_t prev_stamp)
{
	int rv;
	const int max_retries = 3;
	int retries = max_retries;

	while (retries--) {
		union entry_u entry;
		size_t resp_size;
		size_t i;

		resp_size = sizeof(entry);
		rv = send_vendor_command(td, VENDOR_CC_POP_LOG_ENTRY,
					 &prev_stamp, sizeof(prev_stamp),
					 &entry, &resp_size);

		if (rv) {
			/*
			 * Flash log could be momentarily locked by a
			 * concurrent access, let it settle and try again, 10
			 * ms should be enough.
			 */
			usleep(10 * 1000);
			continue;
		}

		if (resp_size == 0)
			/* No more entries. */
			return 0;

		memcpy(&prev_stamp, &entry.r.timestamp, sizeof(prev_stamp));
		printf("%10u:%02x", prev_stamp, entry.r.type);
		for (i = 0; i < FLASH_LOG_PAYLOAD_SIZE(entry.r.size); i++)
			printf(" %02x", entry.r.payload[i]);
		printf("\n");
		retries = max_retries;
	}

	fprintf(stderr, "%s: error %d\n", __func__, rv);

	return rv;
}

static int process_tstamp(struct transfer_descriptor *td,
			  const char *tstamp_ascii)
{
	char *e;
	size_t expected_response_size;
	size_t message_size;
	size_t response_size;
	uint32_t rv;
	uint32_t tstamp = 0;
	uint8_t max_response[sizeof(uint32_t)];

	if (tstamp_ascii) {
		tstamp = strtoul(tstamp_ascii, &e, 10);
		if (*e) {
			fprintf(stderr, "invalid base timestamp value \"%s\"\n",
				tstamp_ascii);
			return -1;
		}
		tstamp = htobe32(tstamp);
		expected_response_size = 0;
		message_size = sizeof(tstamp);
	} else {
		expected_response_size = 4;
		message_size = 0;
	}

	response_size = sizeof(max_response);
	rv = send_vendor_command(td, VENDOR_CC_FLOG_TIMESTAMP, &tstamp,
				 message_size, max_response, &response_size);

	if (rv) {
		fprintf(stderr, "error: return value %d\n", rv);
		return rv;
	}
	if (response_size != expected_response_size) {
		fprintf(stderr, "error: got %zd bytes, expected %zd\n",
			response_size, expected_response_size);
		return -1; /* Should never happen. */
	}

	if (response_size) {
		memcpy(&tstamp, max_response, sizeof(tstamp));
		printf("Current H1 time is %d\n", be32toh(tstamp));
	}
	return 0;
}

/*
 * Search the passed in zero terminated array of options_map structures for
 * option 'option'.
 *
 * If found - set the corresponding integer to 1 and return 1. If not found -
 * return 0.
 */
static int check_boolean(const struct options_map *omap, char option)
{
	do {
		if (omap->opt != option)
			continue;

		*omap->flag = 1;
		return 1;
	} while ((++omap)->opt);

	return 0;
}

/*
 * Set the long_opts table and short_opts string.
 *
 * This function allows to avoid maintaining two command line option
 * descriptions, for short and long forms.
 *
 * The long_opts table is built based on the cmd_line_options table contents,
 * and the short form is built based on the long_opts table contents.
 *
 * The 'required_argument' short options are followed by ':'.
 *
 * The passed in long_opts array and short_opts string are guaranteed to
 * accommodate all necessary objects/characters.
 */
static void set_opt_descriptors(struct option *long_opts, char *short_opts)
{
	size_t i;
	int j;

	for (i = j = 0; i < ARRAY_SIZE(cmd_line_options); i++) {
		long_opts[i] = cmd_line_options[i].opt;
		short_opts[j++] = long_opts[i].val;
		if (long_opts[i].has_arg == required_argument)
			short_opts[j++] = ':';
	}
}

/*
 * Find the long_opts table index where .val field is set to the passed in
 * short option value.
 */
static int get_longindex(int short_opt, const struct option *long_opts)
{
	int i;

	for (i = 0; long_opts[i].name; i++)
		if (long_opts[i].val == short_opt)
			return i;

	/*
	 * We could never come here as the short options list is compiled
	 * based on long options table.
	 */
	fprintf(stderr, "could not find long opt table index for %d\n",
		short_opt);
	exit(1);

	return -1; /* Not reached. */
}

/*
 * Combine searching for command line parameters and optional arguments.
 *
 * The canonical short options description string does not allow to specify
 * that a command line argument expects an optional parameter. but gsctool
 * users expect to be able to use the following styles for optional
 * parameters:
 *
 * a)   -x <param value>
 * b)  --x_long <param_value>
 * c)  --x_long=<param_value>
 *
 * Styles a) and b) are not supported standard getopt_long(), this function
 * adds ability to handle cases a) and b).
 */
static int getopt_all(int argc, char *argv[])
{
	int longindex = -1;
	static char short_opts[2 * ARRAY_SIZE(cmd_line_options)] = {};
	static struct option long_opts[ARRAY_SIZE(cmd_line_options) + 1] = {};
	int i;

	if (!short_opts[0])
		set_opt_descriptors(long_opts, short_opts);

	i = getopt_long(argc, argv, short_opts, long_opts, &longindex);
	if (i != -1) {

		if (longindex < 0) {
			/*
			 * longindex is not set, this must have been the short
			 * option case, Find the long_opts table index based
			 * on the short option value.
			 */
			longindex = get_longindex(i, long_opts);
		}

		if (long_opts[longindex].has_arg == optional_argument) {
			/*
			 * This command line option may include an argument,
			 * let's check if it is there as the next token in the
			 * command line.
			 */
			if (!optarg && argv[optind] && argv[optind][0] != '-')
				/* Yes, it is. */
				optarg = argv[optind++];
		}
	}

	return i;
}

int main(int argc, char *argv[])
{
	struct transfer_descriptor td;
	int errorcnt;
	uint8_t *data = 0;
	size_t data_len = 0;
	uint16_t vid = 0;
	uint16_t pid = 0;
	int i;
	size_t j;
	int transferred_sections = 0;
	int binary_vers = 0;
	int show_fw_ver = 0;
	int rma = 0;
	const char *rma_auth_code;
	int get_endorsement_seed = 0;
	const char *endorsement_seed_str;
	int corrupt_inactive_rw = 0;
	struct board_id bid;
	enum board_id_action bid_action;
	int password = 0;
	int ccd_open = 0;
	int ccd_unlock = 0;
	int ccd_lock = 0;
	int ccd_info = 0;
	int get_flog = 0;
	uint32_t prev_log_entry = 0;
	int wp = 0;
	int try_all_transfer = 0;
	int tpm_mode = 0;
	bool show_machine_output = false;
	int tstamp = 0;
	const char *tstamp_arg = NULL;

	const char *exclusive_opt_error =
		"Options -a, -s and -t are mutually exclusive\n";
	const char *openbox_desc_file = NULL;
	int factory_mode = 0;
	char *factory_mode_arg;
	char *tpm_mode_arg = NULL;
	char *serial = NULL;
	int sn_bits = 0;
	uint8_t sn_bits_arg[SN_BITS_SIZE];
	int sn_inc_rma = 0;
	uint8_t sn_inc_rma_arg;

	/*
	 * All options which result in setting a Boolean flag to True, along
	 * with addresses of the flags. Terminated by a zeroed entry.
	 */
	const struct options_map omap[] = {
		{ 'B', &td.background_update_supported},
		{ 'b', &binary_vers },
		{ 'c', &corrupt_inactive_rw },
		{ 'f', &show_fw_ver },
		{ 'I', &ccd_info },
		{ 'k', &ccd_lock },
		{ 'o', &ccd_open },
		{ 'P', &password },
		{ 'p', &td.post_reset },
		{ 'U', &ccd_unlock },
		{ 'u', &td.upstart_mode },
		{ 'V', &verbose_mode },
		{ 'w', &wp },
		{},
	};

	/*
	 * Explicitly sets buffering type to line buffered so that output
	 * lines can be written to pipe instantly. This is needed when the
	 * cr50-verify-ro.sh execution in verify_ro is moved from crosh to
	 * debugd.
	 */
	setlinebuf(stdout);

	progname = strrchr(argv[0], '/');
	if (progname)
		progname++;
	else
		progname = argv[0];

	/* Usb transfer - default mode. */
	memset(&td, 0, sizeof(td));
	td.ep_type = usb_xfer;

	bid_action = bid_none;
	errorcnt = 0;
	opterr = 0;				/* quiet, you */

	while ((i = getopt_all(argc, argv)) != -1) {
		if (check_boolean(omap, i))
			continue;
		switch (i) {
		case 'a':
			if (td.ep_type) {
				errorcnt++;
				fprintf(stderr, "%s", exclusive_opt_error);
				break;
			}
			try_all_transfer = 1;
			/* Try dev_xfer first. */
			td.ep_type = dev_xfer;
			break;
		case 'd':
			if (!parse_vidpid(optarg, &vid, &pid)) {
				fprintf(stderr,
					"Invalid device argument: \"%s\"\n",
					optarg);
				errorcnt++;
			}
			break;
		case 'e':
			get_endorsement_seed = 1;
			endorsement_seed_str = optarg;
			break;
		case 'F':
			factory_mode = 1;
			factory_mode_arg = optarg;
			break;
		case 'h':
			usage(errorcnt);
			break;
		case 'i':
			if (!parse_bid(optarg, &bid, &bid_action)) {
				fprintf(stderr,
					"Invalid board id argument: \"%s\"\n",
					optarg);
				errorcnt++;
			}
			break;
		case 'L':
			get_flog = 1;
			if (optarg)
				prev_log_entry = strtoul(optarg, NULL, 0);
			break;
		case 'M':
			show_machine_output = true;
			break;
		case 'm':
			tpm_mode = 1;
			tpm_mode_arg = optarg;
			break;
		case 'n':
			serial = optarg;
			break;
		case 'O':
			openbox_desc_file = optarg;
			break;
		case 'r':
			rma = 1;
			rma_auth_code = optarg;
			break;
		case 'R':
			sn_inc_rma = 1;
			if (!parse_sn_inc_rma(optarg, &sn_inc_rma_arg)) {
				fprintf(stderr,
					"Invalid sn_rma_inc argument: \"%s\"\n",
					optarg);
				errorcnt++;
			}

			break;
		case 's':
			if (td.ep_type || try_all_transfer) {
				errorcnt++;
				fprintf(stderr, "%s", exclusive_opt_error);
				break;
			}
			td.ep_type = dev_xfer;
			break;
		case 'S':
			sn_bits = 1;
			if (!parse_sn_bits(optarg, sn_bits_arg)) {
				fprintf(stderr,
					"Invalid sn_bits argument: \"%s\"\n",
					optarg);
				errorcnt++;
			}

			break;
		case 't':
			if (td.ep_type || try_all_transfer) {
				errorcnt++;
				fprintf(stderr, "%s", exclusive_opt_error);
				break;
			}
			td.ep_type = ts_xfer;
			break;
		case 'T':
			tstamp = 1;
			tstamp_arg = optarg;
			break;
		case 'v':
			report_version();  /* This will call exit(). */
			break;
		case 0:				/* auto-handled option */
			break;
		case '?':
			if (optopt)
				fprintf(stderr, "Unrecognized option: -%c\n",
					optopt);
			else
				fprintf(stderr, "Unrecognized option: %s\n",
					argv[optind - 1]);
			errorcnt++;
			break;
		case ':':
			fprintf(stderr, "Missing argument to %s\n",
				argv[optind - 1]);
			errorcnt++;
			break;
		default:
			fprintf(stderr, "Internal error at %s:%d\n",
				__FILE__, __LINE__);
			exit(update_error);
		}
	}

	if (errorcnt)
		usage(errorcnt);

	/*
	 * If no usb device information was given, default to the using cr50
	 * vendor and product id to find the usb device.
	 */
	if (!serial && !vid && !pid) {
		vid = VID;
		pid = PID;
	}

	if ((bid_action == bid_none) &&
	    !ccd_info &&
	    !ccd_lock &&
	    !ccd_open &&
	    !ccd_unlock &&
	    !corrupt_inactive_rw &&
	    !get_flog &&
	    !get_endorsement_seed &&
	    !factory_mode &&
	    !password &&
	    !rma &&
	    !show_fw_ver &&
	    !sn_bits &&
	    !sn_inc_rma &&
	    !openbox_desc_file &&
	    !tstamp &&
	    !tpm_mode &&
	    !wp) {
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
			exit(show_headers_versions(data, show_machine_output));
	} else {
		if (optind < argc)
			printf("Ignoring binary image %s\n", argv[optind]);
	}


	if (((bid_action != bid_none) + !!rma + !!password + !!ccd_open +
	     !!ccd_unlock + !!ccd_lock + !!ccd_info + !!get_flog +
	     !!openbox_desc_file + !!factory_mode + !!wp +
	     !!get_endorsement_seed) > 1) {
		fprintf(stderr,
			"ERROR: options"
			"-e, -F, -I, -i, -k, -L, -O, -o, -P, -r, -U and -w "
			"are mutually exclusive\n");
		exit(update_error);
	}

	if (td.ep_type == usb_xfer) {
		if (usb_findit(serial, vid, pid, USB_SUBCLASS_GOOGLE_CR50,
			       USB_PROTOCOL_GOOGLE_CR50_NON_HC_FW_UPDATE,
			       &td.uep))
			exit(update_error);
	} else if (td.ep_type == dev_xfer) {
		td.tpm_fd = open("/dev/tpm0", O_RDWR);
		if (td.tpm_fd < 0) {
			if (!try_all_transfer) {
				perror("Could not open TPM");
				exit(update_error);
			}
			td.ep_type = ts_xfer;
		}
	}

	if (openbox_desc_file)
		return verify_ro(&td, openbox_desc_file, show_machine_output);

	if (ccd_unlock || ccd_open || ccd_lock || ccd_info)
		process_ccd_state(&td, ccd_unlock, ccd_open,
				  ccd_lock, ccd_info);

	if (password)
		process_password(&td);

	if (bid_action != bid_none)
		process_bid(&td, bid_action, &bid, show_machine_output);

	if (get_endorsement_seed)
		exit(process_endorsement_seed(&td, endorsement_seed_str));

	if (rma)
		process_rma(&td, rma_auth_code);

	if (factory_mode)
		process_factory_mode(&td, factory_mode_arg);
	if (wp)
		process_wp(&td);

	if (corrupt_inactive_rw)
		invalidate_inactive_rw(&td);

	if (tpm_mode) {
		int rv = process_tpm_mode(&td, tpm_mode_arg);

		exit(rv);
	}

	if (tstamp)
		return process_tstamp(&td, tstamp_arg);

	if (sn_bits)
		process_sn_bits(&td, sn_bits_arg);

	if (sn_inc_rma)
		process_sn_inc_rma(&td, sn_inc_rma_arg);

	if (get_flog)
		process_get_flog(&td, prev_log_entry);

	if (data || show_fw_ver) {

		setup_connection(&td);

		if (data) {
			transferred_sections = transfer_image(&td,
							      data, data_len);
			free(data);
		}

		/*
		 * Move USB updater sate machine to idle state so that vendor
		 * commands can be processed later, if any.
		 */
		if (td.ep_type == usb_xfer)
			send_done(&td.uep);

		if (transferred_sections)
			generate_reset_request(&td);

		if (show_fw_ver) {
			if (show_machine_output) {
				print_machine_output("RO_FW_VER", "%d.%d.%d",
						     targ.shv[0].epoch,
						     targ.shv[0].major,
						     targ.shv[0].minor);
				print_machine_output("RW_FW_VER", "%d.%d.%d",
						     targ.shv[1].epoch,
						     targ.shv[1].major,
						     targ.shv[1].minor);
			} else {
				printf("Current versions:\n");
				printf("RO %d.%d.%d\n", targ.shv[0].epoch,
				       targ.shv[0].major, targ.shv[0].minor);
				printf("RW %d.%d.%d\n", targ.shv[1].epoch,
				       targ.shv[1].major, targ.shv[1].minor);
			}
		}
	}

	if (td.ep_type == usb_xfer) {
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
