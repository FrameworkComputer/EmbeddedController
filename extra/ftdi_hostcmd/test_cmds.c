/* Copyright 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "mpsse.h"

#include "ec_commands.h"

static int opt_verbose;

/* Communication handle */
static struct mpsse_context *mpsse;

/* enum ec_status meaning */
static const char *ec_strerr(enum ec_status r)
{
	static const char * const strs[] = {
		"SUCCESS",
		"INVALID_COMMAND",
		"ERROR",
		"INVALID_PARAM",
		"ACCESS_DENIED",
		"INVALID_RESPONSE",
		"INVALID_VERSION",
		"INVALID_CHECKSUM",
		"IN_PROGRESS",
		"UNAVAILABLE",
		"TIMEOUT",
		"OVERFLOW",
		"INVALID_HEADER",
		"REQUEST_TRUNCATED",
		"RESPONSE_TOO_BIG",
		"BUS_ERROR",
		"BUSY",
	};
	if (r >= EC_RES_SUCCESS && r <= EC_RES_BUSY)
		return strs[r];

	return "<undefined result>";
};


/****************************************************************************
 * Debugging output
 */

#define LINELEN 16

static void showline(uint8_t *buf, int len)
{
	int i;
	printf("  ");
	for (i = 0; i < len; i++)
		printf(" %02x", buf[i]);
	for (i = len; i < LINELEN; i++)
		printf("   ");
	printf("    ");
	for (i = 0; i < len; i++)
		printf("%c",
		       (buf[i] >= ' ' && buf[i] <= '~') ? buf[i] : '.');
	printf("\n");
}

static void show(const char *fmt, uint8_t *buf, int len)
{
	int i, m, n;

	if (!opt_verbose)
		return;

	printf(fmt, len);

	m = len / LINELEN;
	n = len % LINELEN;

	for (i = 0; i < m; i++)
		showline(buf + i * LINELEN, LINELEN);
	if (n)
		showline(buf + m * LINELEN, n);
}

/****************************************************************************
 * Send command & receive result
 */

/*
 * With proto v3, the kernel driver asks the EC for the max param size
 * (EC_CMD_GET_PROTOCOL_INFO) at probe time, because it can vary depending on
 * the bus and/or the supported commands.
 *
 * FIXME: For now we'll just hard-code a size.
 */
static uint8_t txbuf[128];

/*
 * Load the output buffer with a proto v3 request (header, then data, with
 * checksum correct in header).
 */
static size_t prepare_request(int cmd, int version,
			      const uint8_t *data, size_t data_len)
{
	struct ec_host_request *request;
	size_t i, total_len;
	uint8_t csum = 0;

	total_len = sizeof(*request) + data_len;
	if (total_len > sizeof(txbuf)) {
		printf("Request too large (%zd > %zd)\n",
		       total_len, sizeof(txbuf));
		return -1;
	}

	/* Header first */
	request = (struct ec_host_request *)txbuf;
	request->struct_version = EC_HOST_REQUEST_VERSION;
	request->checksum = 0;
	request->command = cmd;
	request->command_version = version;
	request->reserved = 0;
	request->data_len = data_len;

	/* Then data */
	memcpy(txbuf + sizeof(*request), data, data_len);

	/* Update checksum */
	for (i = 0; i < total_len; i++)
		csum += txbuf[i];
	request->checksum = -csum;

	return total_len;
}

/*
 * Sends prepared proto v3 command using the SPI protocol
 *
 * Returns zero if command was sent, nonzero otherwise.
 */
static int send_request(uint8_t *txbuf, size_t len)
{
	uint8_t *tptr;
	size_t i;
	int ret = 0;

	show("Transfer(%d) =>\n", txbuf, len);
	tptr = Transfer(mpsse, txbuf, len);

	if (!tptr) {
		fprintf(stderr, "Transfer failed: %s\n",
			ErrorString(mpsse));
		return -1;
	}

	show("Transfer(%d) <=\n", tptr, len);

	/* Make sure the EC was listening */
	for (i = 0; i < len; i++) {
		switch (tptr[i]) {
		case EC_SPI_PAST_END:
		case EC_SPI_RX_BAD_DATA:
		case EC_SPI_NOT_READY:
			ret = tptr[i];
			/* FALLTHROUGH */
		default:
			break;
		}
		if (ret)
			break;
	}
	free(tptr);
	return ret;
}


/* Timeout flag, so we don't wait forever */
static int timedout;
static void alarm_handler(int sig)
{
	timedout = 1;
}

/*
 * Read proto v3 response from SPI bus
 *
 * The response header and data are copied into the provided locations.
 *
 * Return value:
 *   0   = response received (check hdr for EC result and body size)
 *   -1  = problems
 */
static int get_response(struct ec_host_response *hdr,
			uint8_t *bodydest, size_t bodylen)
{
	uint8_t *hptr = 0, *bptr = 0;
	uint8_t sum = 0;
	int ret = -1;
	size_t i;

	/* Give up eventually */
	timedout = 0;
	if (SIG_ERR == signal(SIGALRM, alarm_handler)) {
		perror("Problem with signal handler");
		goto out;
	}
	alarm(3);

	/* Read a byte at a time until we see the start of the frame.
	 * This is slow, but still faster than the EC. */
	while (1) {
		uint8_t *ptr = Read(mpsse, 1);
		if (!ptr) {
			fprintf(stderr, "Read failed: %s\n",
				ErrorString(mpsse));
			alarm(0);
			goto out;
		}
		if (*ptr == EC_SPI_FRAME_START) {
			free(ptr);
			break;
		}
		free(ptr);

		if (timedout) {
			fprintf(stderr, "timed out\n");
			goto out;
		}
	}
	alarm(0);

	/* Now read the response header */
	hptr = Read(mpsse, sizeof(*hdr));
	if (!hptr) {
		fprintf(stderr, "Read failed: %s\n",
			ErrorString(mpsse));
		goto out;
	}
	show("Header(%d):\n", hptr, sizeof(*hdr));
	memcpy(hdr, hptr, sizeof(*hdr));

	/* Check the header */
	if (hdr->struct_version != EC_HOST_RESPONSE_VERSION) {
		printf("response version %d (should be %d)\n",
		       hdr->struct_version,
		       EC_HOST_RESPONSE_VERSION);
		goto out;
	}

	if (hdr->data_len > bodylen) {
		printf("response data_len %d is > %zd\n",
		       hdr->data_len,
		       bodylen);
		goto out;
	}

	/* Read the data */
	if (hdr->data_len) {
		bptr = Read(mpsse, hdr->data_len);
		if (!bptr) {
			fprintf(stderr, "Read failed: %s\n",
				ErrorString(mpsse));
			goto out;
		}
		show("Body(%d):\n", bptr, hdr->data_len);
		memcpy(bodydest, bptr, hdr->data_len);
	}

	/* Verify the checksum */
	for (i = 0; i < sizeof(hdr); i++)
		sum += hptr[i];
	for (i = 0; i < hdr->data_len; i++)
		sum += bptr[i];
	if (sum)

		printf("Checksum invalid\n");
	else
		ret = 0;

out:
	if (hptr)
		free(hptr);
	if (bptr)
		free(bptr);
	return ret;
}


/*
 * Send command, wait for result. Return zero if communication succeeded; check
 * response to see if the EC liked the command.
 */
static int send_cmd(int cmd, int version,
		    void *outbuf,
		    size_t outsize,
		    struct ec_host_response *resp,
		    void *inbuf,
		    size_t insize)
{

	size_t len;
	int ret = -1;

	/* Load up the txbuf with the stuff to send */
	len = prepare_request(cmd, version, outbuf, outsize);
	if (len < 0)
		return -1;

	if (MPSSE_OK != Start(mpsse)) {
		fprintf(stderr, "Start failed: %s\n",
			ErrorString(mpsse));
		return -1;
	}

	if (0 == send_request(txbuf, len) &&
	    0 == get_response(resp, inbuf, insize))
		ret = 0;

	if (MPSSE_OK != Stop(mpsse)) {
		fprintf(stderr, "Stop failed: %s\n",
			ErrorString(mpsse));
		return -1;
	}

	return ret;
}


/****************************************************************************
 * Probe for basic protocol info
 */

/**
 * Try to talk to the attached(?) device.
 *
 * @return  zero on success
 */
static int probe_v3(void)
{
	struct ec_host_response resp;
	struct ec_response_get_protocol_info info;
	int i, ret;

	memset(&resp, 0, sizeof(resp));
	memset(&info, 0, sizeof(info));

	if (opt_verbose)
		printf("Trying EC_CMD_GET_PROTOCOL_INFO...\n");

	ret = send_cmd(EC_CMD_GET_PROTOCOL_INFO, 0,
		       0, 0,
		       &resp,
		       &info, sizeof(info));

	if (ret) {
		printf("EC_CMD_GET_PROTOCOL_INFO failed\n");
		return -1;
	}

	if (EC_RES_SUCCESS != resp.result) {
		printf("EC result is %d: %s\n",
		       resp.result, ec_strerr(resp.result));
		return -1;
	}

	printf("EC_CMD_GET_PROTOCOL_INFO Success!\n");
	printf("  protocol_versions:         ");
	for (i = 0; i < 32; i++)
		if (info.protocol_versions & (1 << i))
			printf(" %d", i);
	printf("\n");
	printf("  max_request_packet_size:    %d\n",
	       info.max_request_packet_size);
	printf("  max_response_packet_size:   %d\n",
	       info.max_response_packet_size);
	printf("  flags:                      0x%x\n",
	       info.flags);

	return 0;
}

/****************************************************************************
 * Pretty-print the host commands that the device admits to having
 */

struct lookup {
	uint16_t cmd;
	const char * const desc;
};

static struct lookup cmd_table[] = {
	{0x00, "EC_CMD_PROTO_VERSION"},
	{0x01, "EC_CMD_HELLO"},
	{0x02, "EC_CMD_GET_VERSION"},
	{0x03, "EC_CMD_READ_TEST"},
	{0x04, "EC_CMD_GET_BUILD_INFO"},
	{0x05, "EC_CMD_GET_CHIP_INFO"},
	{0x06, "EC_CMD_GET_BOARD_VERSION"},
	{0x07, "EC_CMD_READ_MEMMAP"},
	{0x08, "EC_CMD_GET_CMD_VERSIONS"},
	{0x09, "EC_CMD_GET_COMMS_STATUS"},
	{0x0a, "EC_CMD_TEST_PROTOCOL"},
	{0x0b, "EC_CMD_GET_PROTOCOL_INFO"},
	{0x0c, "EC_CMD_GSV_PAUSE_IN_S5"},
	{0x0d, "EC_CMD_GET_FEATURES"},
	{0x10, "EC_CMD_FLASH_INFO"},
	{0x11, "EC_CMD_FLASH_READ"},
	{0x12, "EC_CMD_FLASH_WRITE"},
	{0x13, "EC_CMD_FLASH_ERASE"},
	{0x15, "EC_CMD_FLASH_PROTECT"},
	{0x16, "EC_CMD_FLASH_REGION_INFO"},
	{0x17, "EC_CMD_VBNV_CONTEXT"},
	{0x20, "EC_CMD_PWM_GET_FAN_TARGET_RPM"},
	{0x21, "EC_CMD_PWM_SET_FAN_TARGET_RPM"},
	{0x22, "EC_CMD_PWM_GET_KEYBOARD_BACKLIGHT"},
	{0x23, "EC_CMD_PWM_SET_KEYBOARD_BACKLIGHT"},
	{0x24, "EC_CMD_PWM_SET_FAN_DUTY"},
	{0x28, "EC_CMD_LIGHTBAR_CMD"},
	{0x29, "EC_CMD_LED_CONTROL"},
	{0x2a, "EC_CMD_VBOOT_HASH"},
	{0x2b, "EC_CMD_MOTION_SENSE_CMD"},
	{0x2c, "EC_CMD_FORCE_LID_OPEN"},
	{0x30, "EC_CMD_USB_CHARGE_SET_MODE"},
	{0x40, "EC_CMD_PSTORE_INFO"},
	{0x41, "EC_CMD_PSTORE_READ"},
	{0x42, "EC_CMD_PSTORE_WRITE"},
	{0x44, "EC_CMD_RTC_GET_VALUE"},
	{0x45, "EC_CMD_RTC_GET_ALARM"},
	{0x46, "EC_CMD_RTC_SET_VALUE"},
	{0x47, "EC_CMD_RTC_SET_ALARM"},
	{0x48, "EC_CMD_PORT80_LAST_BOOT"},
	{0x48, "EC_CMD_PORT80_READ"},
	{0x50, "EC_CMD_THERMAL_SET_THRESHOLD"},
	{0x51, "EC_CMD_THERMAL_GET_THRESHOLD"},
	{0x52, "EC_CMD_THERMAL_AUTO_FAN_CTRL"},
	{0x53, "EC_CMD_TMP006_GET_CALIBRATION"},
	{0x54, "EC_CMD_TMP006_SET_CALIBRATION"},
	{0x55, "EC_CMD_TMP006_GET_RAW"},
	{0x60, "EC_CMD_MKBP_STATE"},
	{0x61, "EC_CMD_MKBP_INFO"},
	{0x62, "EC_CMD_MKBP_SIMULATE_KEY"},
	{0x64, "EC_CMD_MKBP_SET_CONFIG"},
	{0x65, "EC_CMD_MKBP_GET_CONFIG"},
	{0x66, "EC_CMD_KEYSCAN_SEQ_CTRL"},
	{0x67, "EC_CMD_GET_NEXT_EVENT"},
	{0x70, "EC_CMD_TEMP_SENSOR_GET_INFO"},
	{0x87, "EC_CMD_HOST_EVENT_GET_B"},
	{0x88, "EC_CMD_HOST_EVENT_GET_SMI_MASK"},
	{0x89, "EC_CMD_HOST_EVENT_GET_SCI_MASK"},
	{0x8d, "EC_CMD_HOST_EVENT_GET_WAKE_MASK"},
	{0x8a, "EC_CMD_HOST_EVENT_SET_SMI_MASK"},
	{0x8b, "EC_CMD_HOST_EVENT_SET_SCI_MASK"},
	{0x8c, "EC_CMD_HOST_EVENT_CLEAR"},
	{0x8e, "EC_CMD_HOST_EVENT_SET_WAKE_MASK"},
	{0x8f, "EC_CMD_HOST_EVENT_CLEAR_B"},
	{0x90, "EC_CMD_SWITCH_ENABLE_BKLIGHT"},
	{0x91, "EC_CMD_SWITCH_ENABLE_WIRELESS"},
	{0x92, "EC_CMD_GPIO_SET"},
	{0x93, "EC_CMD_GPIO_GET"},
	{0x94, "EC_CMD_I2C_READ"},
	{0x95, "EC_CMD_I2C_WRITE"},
	{0x96, "EC_CMD_CHARGE_CONTROL"},
	{0x97, "EC_CMD_CONSOLE_SNAPSHOT"},
	{0x98, "EC_CMD_CONSOLE_READ"},
	{0x99, "EC_CMD_BATTERY_CUT_OFF"},
	{0x9a, "EC_CMD_USB_MUX"},
	{0x9b, "EC_CMD_LDO_SET"},
	{0x9c, "EC_CMD_LDO_GET"},
	{0x9d, "EC_CMD_POWER_INFO"},
	{0x9e, "EC_CMD_I2C_PASSTHRU"},
	{0x9f, "EC_CMD_HANG_DETECT"},
	{0xa0, "EC_CMD_CHARGE_STATE"},
	{0xa1, "EC_CMD_CHARGE_CURRENT_LIMIT"},
	{0xa2, "EC_CMD_EXT_POWER_CURRENT_LIMIT"},
	{0xb0, "EC_CMD_SB_READ_WORD"},
	{0xb1, "EC_CMD_SB_WRITE_WORD"},
	{0xb2, "EC_CMD_SB_READ_BLOCK"},
	{0xb3, "EC_CMD_SB_WRITE_BLOCK"},
	{0xb4, "EC_CMD_BATTERY_VENDOR_PARAM"},
	{0xb5, "EC_CMD_SB_FW_UPDATE"},
	{0xd2, "EC_CMD_REBOOT_EC"},
	{0xd3, "EC_CMD_GET_PANIC_INFO"},
	{0xd1, "EC_CMD_REBOOT"},
	{0xdb, "EC_CMD_RESEND_RESPONSE"},
	{0xdc, "EC_CMD_VERSION0"},
	{0x100, "EC_CMD_PD_EXCHANGE_STATUS"},
	{0x104, "EC_CMD_PD_HOST_EVENT_STATUS"},
	{0x101, "EC_CMD_USB_PD_CONTROL"},
	{0x102, "EC_CMD_USB_PD_PORTS"},
	{0x103, "EC_CMD_USB_PD_POWER_INFO"},
	{0x110, "EC_CMD_USB_PD_FW_UPDATE"},
	{0x111, "EC_CMD_USB_PD_RW_HASH_ENTRY"},
	{0x112, "EC_CMD_USB_PD_DEV_INFO"},
	{0x113, "EC_CMD_USB_PD_DISCOVERY"},
	{0x114, "EC_CMD_PD_CHARGE_PORT_OVERRIDE"},
	{0x115, "EC_CMD_PD_GET_LOG_ENTRY"},
	{0x116, "EC_CMD_USB_PD_GET_AMODE"},
	{0x117, "EC_CMD_USB_PD_SET_AMODE"},
	{0x118, "EC_CMD_PD_WRITE_LOG_ENTRY"},
	{0x200, "EC_CMD_BLOB"},
};

#define ARRAY_SIZE(A) (sizeof(A) / sizeof(A[0]))

static void show_command(uint16_t c)
{
	unsigned int i;
	const char *desc = "<unknown>";

	for (i = 0; i < ARRAY_SIZE(cmd_table); i++)
		if (cmd_table[i].cmd == c) {
			desc = cmd_table[i].desc;
			break;
		}

	printf("  %02x  %s\n", c, desc);
}

static void scan_commands(uint16_t start, uint16_t stop)
{
	struct ec_params_get_cmd_versions_v1 q_vers;
	struct ec_response_get_cmd_versions r_vers;
	struct ec_host_response ec_resp;
	uint16_t i;

	memset(&ec_resp, 0, sizeof(ec_resp));

	printf("Supported host commands:\n");
	for (i = start; i <= stop; i++) {

		if (opt_verbose)
			printf("Querying CMD %02x\n", i);

		q_vers.cmd = i;
		if (0 != send_cmd(EC_CMD_GET_CMD_VERSIONS, 1,
				  &q_vers, sizeof(q_vers),
				  &ec_resp,
				  &r_vers, sizeof(r_vers))) {
			printf("query failed on cmd %02x - aborting\n", i);
			return;
		}

		switch (ec_resp.result) {
		case EC_RES_SUCCESS:
			if (opt_verbose)
				printf("Yes: ");
			show_command(i);
			break;
		case EC_RES_INVALID_PARAM:
			if (opt_verbose)
				printf("No\n");
			break;
		default:
			printf("lookup of cmd %02x returned %d %s\n", i,
			       ec_resp.result,
			       ec_strerr(ec_resp.result));
		}
	}
}

/****************************************************************************/

static void usage(char *progname)
{
	printf("Usage: %s [-v] [start [stop]]\n", progname);
}

int main(int argc, char *argv[])
{
	int retval = 1;
	int errorcnt = 0;
	int i;
	uint16_t start = cmd_table[0].cmd;
	uint16_t stop = cmd_table[ARRAY_SIZE(cmd_table) - 1].cmd;

	while ((i = getopt(argc, argv, ":v")) != -1) {
		switch (i) {
		case 'v':
			opt_verbose++;
			break;
		case '?':
			printf("unrecognized option: -%c\n", optopt);
			errorcnt++;
			break;
		}
	}
	if (errorcnt) {
		usage(argv[0]);
		return 1;
	}

	/* Range (no error checking) */
	if (optind < argc)
		start = (uint16_t)strtoul(argv[optind++], 0, 0);
	if (optind < argc)
		stop = (uint16_t)strtoul(argv[optind++], 0, 0);

	/* Find something to talk to */
	mpsse = MPSSE(SPI0, 1000000, 0);
	if (!mpsse) {
		printf("Can't find a device to open\n");
		return 1;
	}

	if (0 != probe_v3())
		goto out;

	scan_commands(start, stop);

	retval = 0;
out:
	Close(mpsse);
	mpsse = 0;
	return retval;
}
