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
		fprintf(stderr, "ioctl %d, errno %d (%s), EC result %d\n",
			r, errno, strerror(errno), s_cmd.result);
		return r;
	}
	if (s_cmd.result != EC_RES_SUCCESS)
		fprintf(stderr, "EC result %d\n", s_cmd.result);

	return s_cmd.insize;
}

static int ec_readmem_dev(int offset, int bytes, void *dest)
{
	struct cros_ec_readmem s_mem;

	s_mem.offset = offset;
	s_mem.bytes = bytes;
	s_mem.buffer = dest;

	return ioctl(fd, CROS_EC_DEV_IOCRDMEM, &s_mem);
}

int comm_init_dev(void)
{
	char version[80];
	int r;
	char *s;

	fd = open("/dev/" CROS_EC_DEV_NAME, O_RDWR);
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

	ec_command = ec_command_dev;
	if (ec_readmem_dev(EC_MEMMAP_ID, 2, version) == 2)
		ec_readmem = ec_readmem_dev;

	return 0;
}
