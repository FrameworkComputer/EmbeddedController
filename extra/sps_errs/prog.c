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
static size_t stop_after = -1;

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


/* Timeout flag, so we don't wait forever */
static int timedout;
static void alarm_handler(int sig)
{
	timedout = 1;
}

/*
 * Send command, wait for result. Return zero if communication succeeded; check
 * response to see if the EC liked the command.
 */
static int send_cmd(int cmd, int version,
		    void *outbuf,
		    size_t outsize,
		    struct ec_host_response *hdr,
		    void *bodydest,
		    size_t bodylen)
{
	uint8_t *tptr, *hptr = 0, *bptr = 0;
	size_t len, i;
	uint8_t sum = 0;
	int lastone = 0x1111;
	int ret = 0;
	size_t bytes_left = stop_after;
	size_t bytes_sent = 0;


	/* Load up the txbuf with the stuff to send */
	len = prepare_request(cmd, version, outbuf, outsize);
	if (len < 0)
		return -1;

	if (MPSSE_OK != Start(mpsse)) {
		fprintf(stderr, "Start failed: %s\n",
			ErrorString(mpsse));
		return -1;
	}

	/* Send the command request */
	if (len > bytes_left) {
		printf("len %zd => %zd\n", len, bytes_left);
		len = bytes_left;
	}

	show("Transfer(%d) =>\n", txbuf, len);
	tptr = Transfer(mpsse, txbuf, len);
	bytes_left -= len;
	bytes_sent += len;
	if (!tptr) {
		fprintf(stderr, "Transfer failed: %s\n",
			ErrorString(mpsse));
		goto out;
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
	if (ret) {
		printf("HEY: EC no good (0x%02x)\n", ret);
		goto out;
	}

	if (!bytes_left)
		goto out;

	/* Read until we see the response come along */

	/* Give up eventually */
	timedout = 0;
	if (SIG_ERR == signal(SIGALRM, alarm_handler)) {
		perror("Problem with signal handler");
		goto out;
	}
	alarm(1);

	if (opt_verbose)
		printf("Wait:");

	/* Read a byte at a time until we see the start of the frame.
	 * This is slow, but still faster than the EC. */
	while (bytes_left) {
		uint8_t *ptr = Read(mpsse, 1);
		bytes_left--;
		bytes_sent++;
		if (!ptr) {
			fprintf(stderr, "Read failed: %s\n",
				ErrorString(mpsse));
			alarm(0);
			goto out;
		}
		if (opt_verbose && lastone != *ptr) {
			printf(" %02x", *ptr);
			lastone = *ptr;
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

	if (opt_verbose)
		printf("\n");

	if (!bytes_left)
		goto out;

	/* Now read the response header */
	len = sizeof(*hdr);
	if (len > bytes_left) {
		printf("len %zd => %zd\n", len, bytes_left);
		len = bytes_left;
	}

	hptr = Read(mpsse, len);
	bytes_left -= len;
	bytes_sent += len;
	if (!hptr) {
		fprintf(stderr, "Read failed: %s\n",
			ErrorString(mpsse));
		goto out;
	}
	show("Header(%d):\n", hptr, sizeof(*hdr));
	memcpy(hdr, hptr, sizeof(*hdr));

	/* Check the header */
	if (hdr->struct_version != EC_HOST_RESPONSE_VERSION) {
		printf("HEY: response version %d (should be %d)\n",
		       hdr->struct_version,
		       EC_HOST_RESPONSE_VERSION);
		goto out;
	}

	if (hdr->data_len > bodylen) {
		printf("HEY: response data_len %d is > %zd\n",
		       hdr->data_len,
		       bodylen);
		goto out;
	}

	if (!bytes_left)
		goto out;

	len = hdr->data_len;
	if (len > bytes_left) {
		printf("len %zd => %zd\n", len, bytes_left);
		len = bytes_left;
	}

	/* Read the data */
	if (len) {
		bptr = Read(mpsse, len);
		bytes_left -= len;
		bytes_sent += len;
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
		printf("HEY: Checksum invalid\n");

out:
	printf("sent %zd bytes\n", bytes_sent);
	if (!bytes_left)
		printf("hit byte limit\n");
	if (hptr)
		free(hptr);
	if (bptr)
		free(bptr);

	if (MPSSE_OK != Stop(mpsse)) {
		fprintf(stderr, "Stop failed: %s\n",
			ErrorString(mpsse));
		return -1;
	}

	return 0;
}


/****************************************************************************/

/**
 * Try it.
 *
 * @return  zero on success
 */
static int hello(void)
{
	struct ec_params_hello p;
	struct ec_host_response resp;
	struct ec_response_hello r;
	uint32_t expected;
	int retval;

	memset(&p, 0, sizeof(p));
	memset(&resp, 0, sizeof(resp));
	memset(&r, 0, sizeof(r));

	p.in_data = 0xa5a5a5a5;
	expected = p.in_data + 0x01020304;

	retval = send_cmd(EC_CMD_HELLO, 0,
			  &p, sizeof(p),
			  &resp,
			  &r, sizeof(r));

	if (retval) {
		printf("Transmission error\n");
		return -1;
	}

	if (EC_RES_SUCCESS != resp.result) {
		printf("EC result is %d: %s\n",
		       resp.result, ec_strerr(resp.result));
		return -1;
	}

	printf("sent %08x, expected %08x, got %08x => %s\n",
	       p.in_data, expected, r.out_data,
	       expected == r.out_data ? "yay" : "boo");

	return !(expected == r.out_data);
}

static void usage(char *progname)
{
	printf("\nUsage: %s [-v] [-c BYTES]\n\n", progname);
	printf("This sends a EC_CMD_HELLO host command. The -c option can\n");
	printf("be used to truncate the exchange early, to see how the EC\n");
	printf("deals with the interruption.\n\n");
}

int main(int argc, char *argv[])
{
	int retval = 1;
	int errorcnt = 0;
	int i;

	while ((i = getopt(argc, argv, ":vc:")) != -1) {
		switch (i) {
		case 'c':
			stop_after = atoi(optarg);
			printf("stopping after %zd bytes\n", stop_after);
			break;
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

	/* Find something to talk to */
	mpsse = MPSSE(SPI0, 2000000, 0);
	if (!mpsse) {
		printf("Can't find a device to open\n");
		return 1;
	}

	if (0 != hello())
		goto out;

	retval = 0;
out:
	Close(mpsse);
	mpsse = 0;
	return retval;
}
