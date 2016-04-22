/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "comm-host.h"
#include "ec_commands.h"
#include "misc_util.h"


int (*ec_command_proto)(int command, int version,
			const void *outdata, int outsize,
			void *indata, int insize);

int (*ec_readmem)(int offset, int bytes, void *dest);

int ec_max_outsize, ec_max_insize;
void *ec_outbuf;
void *ec_inbuf;
static int command_offset;

int comm_init_dev(const char *device_name) __attribute__((weak));
int comm_init_lpc(void) __attribute__((weak));
int comm_init_i2c(void) __attribute__((weak));

static int fake_readmem(int offset, int bytes, void *dest)
{
	struct ec_params_read_memmap p;
	int c;
	char *buf;

	p.offset = offset;

	if (bytes) {
		p.size = bytes;
		c = ec_command(EC_CMD_READ_MEMMAP, 0, &p, sizeof(p),
			       dest, p.size);
		if (c < 0)
			return c;
		return p.size;
	}

	p.size = EC_MEMMAP_TEXT_MAX;

	c = ec_command(EC_CMD_READ_MEMMAP, 0, &p, sizeof(p), dest, p.size);
	if (c < 0)
		return c;

	buf = dest;
	for (c = 0; c < EC_MEMMAP_TEXT_MAX; c++) {
		if (buf[c] == 0)
			return c;
	}

	buf[EC_MEMMAP_TEXT_MAX - 1] = 0;
	return EC_MEMMAP_TEXT_MAX - 1;
}

void set_command_offset(int offset)
{
	command_offset = offset;
}

int ec_command(int command, int version,
	       const void *outdata, int outsize,
	       void *indata, int insize)
{
	/* Offset command code to support sub-devices */
	return ec_command_proto(command_offset + command, version,
				outdata, outsize,
				indata, insize);
}

int comm_init(int interfaces, const char *device_name)
{
	struct ec_response_get_protocol_info info;
	int allow_large_buffer;

	/* Default memmap access */
	ec_readmem = fake_readmem;

	allow_large_buffer = kernel_version_ge(3, 14, 0);
	if (allow_large_buffer < 0) {
		fprintf(stderr, "Unable to check linux version\n");
		return 1;
	}

	/* Prefer new /dev method */
	if ((interfaces & COMM_DEV) && comm_init_dev &&
	    !comm_init_dev(device_name))
		goto init_ok;

	/* Fallback to direct LPC on x86 */
	if ((interfaces & COMM_LPC) && comm_init_lpc && !comm_init_lpc())
		goto init_ok;

	/* Fallback to direct i2c on ARM */
	if ((interfaces & COMM_I2C) && comm_init_i2c && !comm_init_i2c())
		goto init_ok;

	/* Give up */
	fprintf(stderr, "Unable to establish host communication\n");
	return 1;

 init_ok:
	/* Allocate shared I/O buffers */
	ec_outbuf = malloc(ec_max_outsize);
	ec_inbuf = malloc(ec_max_insize);
	if (!ec_outbuf || !ec_inbuf) {
		fprintf(stderr, "Unable to allocate buffers\n");
		return 1;
	}

	/* read max request / response size from ec for protocol v3+ */
	if (ec_command(EC_CMD_GET_PROTOCOL_INFO, 0, NULL, 0, &info,
		sizeof(info)) == sizeof(info)) {
		int outsize = info.max_request_packet_size -
			sizeof(struct ec_host_request);
		int insize = info.max_response_packet_size -
			sizeof(struct ec_host_response);
		if ((allow_large_buffer) || (outsize < ec_max_outsize))
			ec_max_outsize = outsize;
		if ((allow_large_buffer) || (insize < ec_max_insize))
			ec_max_insize = insize;

		ec_outbuf = realloc(ec_outbuf, ec_max_outsize);
		ec_inbuf = realloc(ec_inbuf, ec_max_insize);

		if (!ec_outbuf || !ec_inbuf) {
			fprintf(stderr, "Unable to reallocate buffers\n");
			return 1;
		}
	}

	return 0;

}
