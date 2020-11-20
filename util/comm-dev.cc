/* Copyright 2013 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "cros_ec_dev.h"
#include "comm-host.h"
#include "ec_commands.h"
#include "misc_util.h"

static int fd = -1;

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(t) (sizeof(t) / sizeof(t[0]))
#endif

static const char *const meanings[] = {
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
	"INVALID_HEADER_VERSION",
	"INVALID_HEADER_CRC",
	"INVALID_DATA_CRC",
	"DUP_UNAVAILABLE",
};

static const char *strresult(int i)
{
	if (i < 0 || i >= ARRAY_SIZE(meanings))
		return "<unknown>";
	return meanings[i];
}

/* Old ioctl format, used by Chrome OS 3.18 and older */

static int ec_command_dev(int command, int version, const void *outdata,
			  int outsize, void *indata, int insize)
{
	struct cros_ec_command s_cmd;
	int r;

	s_cmd.command = command;
	s_cmd.version = version;
	s_cmd.result = 0xff;
	s_cmd.outsize = outsize;
	s_cmd.outdata = (uint8_t *)outdata;
	s_cmd.insize = insize;
	s_cmd.indata = (uint8_t *)(indata);

	r = ioctl(fd, CROS_EC_DEV_IOCXCMD, &s_cmd);
	if (r < 0) {
		fprintf(stderr, "ioctl %d, errno %d (%s), EC result %d (%s)\n",
			r, errno, strerror(errno), s_cmd.result,
			strresult(s_cmd.result));
		if (errno == EAGAIN && s_cmd.result == EC_RES_IN_PROGRESS) {
			s_cmd.command = EC_CMD_RESEND_RESPONSE;
			r = ioctl(fd, CROS_EC_DEV_IOCXCMD, &s_cmd);
			if (r < 0) {
				fprintf(stderr,
					"ioctl %d, errno %d (%s), EC result %d (%s)\n",
					r, errno, strerror(errno), s_cmd.result,
					strresult(s_cmd.result));
			}
		}
	}
	if (r >= 0 && s_cmd.result != EC_RES_SUCCESS) {
		fprintf(stderr, "EC result %d (%s)\n", s_cmd.result,
			strresult(s_cmd.result));
		return -EECRESULT - s_cmd.result;
	}

	return r;
}

static int ec_readmem_dev(int offset, int bytes, void *dest)
{
	struct cros_ec_readmem s_mem;
	struct ec_params_read_memmap r_mem;
	int r;
	static int fake_it;

	if (!fake_it) {
		s_mem.offset = offset;
		s_mem.bytes = bytes;
		s_mem.buffer = (char *)(dest);
		r = ioctl(fd, CROS_EC_DEV_IOCRDMEM, &s_mem);
		if (r < 0 && errno == ENOTTY)
			fake_it = 1;
		else
			return r;
	}

	r_mem.offset = offset;
	r_mem.size = bytes;
	return ec_command_dev(EC_CMD_READ_MEMMAP, 0, &r_mem, sizeof(r_mem),
			      dest, bytes);
}

/* New ioctl format, used by Chrome OS 4.4 and later as well as upstream 4.0+ */

static int ec_command_dev_v2(int command, int version, const void *outdata,
			     int outsize, void *indata, int insize)
{
	struct cros_ec_command_v2 *s_cmd;
	int r;

	assert(outsize == 0 || outdata != NULL);
	assert(insize == 0 || indata != NULL);

	s_cmd = (struct cros_ec_command_v2 *)(malloc(
		sizeof(struct cros_ec_command_v2) + MAX(outsize, insize)));
	if (s_cmd == NULL)
		return -EC_RES_ERROR;

	s_cmd->command = command;
	s_cmd->version = version;
	s_cmd->result = 0xff;
	s_cmd->outsize = outsize;
	s_cmd->insize = insize;
	memcpy(s_cmd->data, outdata, outsize);

	r = ioctl(fd, CROS_EC_DEV_IOCXCMD_V2, s_cmd);
	if (r < 0) {
		fprintf(stderr, "ioctl %d, errno %d (%s), EC result %d (%s)\n",
			r, errno, strerror(errno), s_cmd->result,
			strresult(s_cmd->result));
		if (errno == EAGAIN && s_cmd->result == EC_RES_IN_PROGRESS) {
			s_cmd->command = EC_CMD_RESEND_RESPONSE;
			r = ioctl(fd, CROS_EC_DEV_IOCXCMD_V2, s_cmd);
			if (r < 0) {
				fprintf(stderr,
					"ioctl %d, errno %d (%s), EC result %d (%s)\n",
					r, errno, strerror(errno),
					s_cmd->result,
					strresult(s_cmd->result));
			}
		}
	}
	if (r >= 0) {
		memcpy(indata, s_cmd->data, MIN(r, insize));
		if (s_cmd->result != EC_RES_SUCCESS) {
			fprintf(stderr, "EC result %d (%s)\n", s_cmd->result,
				strresult(s_cmd->result));
			r = -EECRESULT - s_cmd->result;
		}
	}
	free(s_cmd);

	return r;
}

static int ec_readmem_dev_v2(int offset, int bytes, void *dest)
{
	struct cros_ec_readmem_v2 s_mem;
	struct ec_params_read_memmap r_mem;
	int r;
	static int fake_it;

	if (!fake_it) {
		s_mem.offset = offset;
		s_mem.bytes = bytes;
		r = ioctl(fd, CROS_EC_DEV_IOCRDMEM_V2, &s_mem);
		if (r < 0 && errno == ENOTTY) {
			fake_it = 1;
		} else {
			memcpy(dest, s_mem.buffer, bytes);
			return r;
		}
	}

	r_mem.offset = offset;
	r_mem.size = bytes;
	return ec_command_dev_v2(EC_CMD_READ_MEMMAP, 0, &r_mem, sizeof(r_mem),
				 dest, bytes);
}

/*
 * Attempt to communicate with kernel using old ioctl format.
 * If it returns ENOTTY, assume that this kernel uses the new format.
 */
static int ec_dev_is_v2(void)
{
	struct ec_params_hello h_req = { .in_data = 0xa0b0c0d0 };
	struct ec_response_hello h_resp;
	struct cros_ec_command s_cmd = {};
	int r;

	s_cmd.command = EC_CMD_HELLO;
	s_cmd.result = 0xff;
	s_cmd.outsize = sizeof(h_req);
	s_cmd.outdata = (uint8_t *)&h_req;
	s_cmd.insize = sizeof(h_resp);
	s_cmd.indata = (uint8_t *)&h_resp;

	r = ioctl(fd, CROS_EC_DEV_IOCXCMD, &s_cmd);
	if (r < 0 && errno == ENOTTY)
		return 1;

	return 0;
}

static int ec_pollevent_dev(unsigned long mask, void *buffer, size_t buf_size,
			    int timeout)
{
	int rv;
	struct pollfd pf = { .fd = fd, .events = POLLIN };

	ioctl(fd, CROS_EC_DEV_IOCEVENTMASK_V2, mask);

	rv = poll(&pf, 1, timeout);
	if (rv != 1)
		return rv;

	if (pf.revents != POLLIN)
		return -pf.revents;

	return read(fd, buffer, buf_size);
}

int comm_init_dev(const char *device_name)
{
	int (*ec_cmd_readmem)(int offset, int bytes, void *dest);
	char version[80];
	char device[80] = "/dev/";
	int r;
	char *s;

	strncat(device, (device_name ? device_name : CROS_EC_DEV_NAME), 40);
	fd = open(device, O_RDWR);
	if (fd < 0)
		return 1;

	r = read(fd, version, sizeof(version) - 1);
	if (r <= 0) {
		close(fd);
		return 2;
	}
	version[r] = '\0';
	s = strchr(version, '\n');
	if (s)
		*s = '\0';
	if (strcmp(version, CROS_EC_DEV_VERSION)) {
		close(fd);
		return 3;
	}

	if (ec_dev_is_v2()) {
		ec_command_proto = ec_command_dev_v2;
		ec_cmd_readmem = ec_readmem_dev_v2;
	} else {
		ec_command_proto = ec_command_dev;
		ec_cmd_readmem = ec_readmem_dev;
	}

	if (ec_cmd_readmem(EC_MEMMAP_ID, 2, version) == 2 &&
	    version[0] == 'E' && version[1] == 'C')
		ec_readmem = ec_cmd_readmem;
	ec_pollevent = ec_pollevent_dev;

	/*
	 * Set temporary size, will be updated later.
	 */
	ec_max_outsize = EC_PROTO2_MAX_PARAM_SIZE - 8;
	ec_max_insize = EC_PROTO2_MAX_PARAM_SIZE;

	return 0;
}
