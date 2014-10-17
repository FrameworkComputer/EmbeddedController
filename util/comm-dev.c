/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "cros_ec_dev.h"
#include "comm-host.h"
#include "ec_commands.h"

static int fd = -1;

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(t) (sizeof(t) / sizeof(t[0]))
#endif

static const char const *meanings[] = {
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
	"BUS_ERROR"
};

static const char *strresult(int i)
{
	if (i < 0 || i >= ARRAY_SIZE(meanings))
		return "<unknown>";
	return meanings[i];
}


static int ec_command_dev(int command, int version,
			  const void *outdata, int outsize,
			  void *indata, int insize)
{
	struct cros_ec_command s_cmd;
	int r;

	s_cmd.command = command;
	s_cmd.version = version;
	s_cmd.result = 0xff;
	s_cmd.outsize = outsize;
	s_cmd.outdata = (uint8_t *)outdata;
	s_cmd.insize = insize;
	s_cmd.indata = indata;

	r = ioctl(fd, CROS_EC_DEV_IOCXCMD, &s_cmd);
	if (r < 0) {
		fprintf(stderr, "ioctl %d, errno %d (%s), EC result %d (%s)\n",
			r, errno, strerror(errno), s_cmd.result,
			strresult(s_cmd.result));
		if (errno == EAGAIN && s_cmd.result == EC_RES_IN_PROGRESS) {
			s_cmd.command = EC_CMD_RESEND_RESPONSE;
			r = ioctl(fd, CROS_EC_DEV_IOCXCMD, &s_cmd);
			fprintf(stderr,
				"ioctl %d, errno %d (%s), EC result %d (%s)\n",
				r, errno, strerror(errno), s_cmd.result,
				strresult(s_cmd.result));
		}
	} else if (s_cmd.result != EC_RES_SUCCESS) {
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
		s_mem.buffer = dest;
		r = ioctl(fd, CROS_EC_DEV_IOCRDMEM, &s_mem);
		if (r < 0 && errno == ENOTTY)
			fake_it = 1;
		else
			return r;
	}

	r_mem.offset = offset;
	r_mem.size = bytes;
	return ec_command_dev(EC_CMD_READ_MEMMAP, 0,
			      &r_mem, sizeof(r_mem),
			      dest, bytes);
}

int comm_init_dev(const char *device_name)
{
	char version[80];
	char device[80] = "/dev/";
	int r;
	char *s;

	strncat(device, (device_name ? device_name : CROS_EC_DEV_NAME), 40);
	fd = open(device, O_RDWR);
	if (fd < 0)
		return 1;

	r = read(fd, version, sizeof(version)-1);
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

	ec_command_proto = ec_command_dev;
	if (ec_readmem_dev(EC_MEMMAP_ID, 2, version) == 2 &&
	    version[0] == 'E' && version[1] == 'C')
		ec_readmem = ec_readmem_dev;

	/*
	 * TODO(crosbug.com/p/23823): Need a way to get this from the driver
	 * and EC.  For now, pick a magic lowest common denominator value. The
	 * ec_max_outsize is set to handle v3 EC protocol. The ec_max_insize
	 * needs to be set to the largest value that can be returned from the
	 * EC, EC_PROTO2_MAX_PARAM_SIZE.
	 */
	ec_max_outsize = EC_PROTO2_MAX_PARAM_SIZE - 8;
	ec_max_insize = EC_PROTO2_MAX_PARAM_SIZE;

	return 0;
}
