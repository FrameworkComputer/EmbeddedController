/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "comm-host.h"
#include "ec_commands.h"

int (*ec_command)(int command, int version,
		  const void *outdata, int outsize,
		  void *indata, int insize);

int (*ec_readmem)(int offset, int bytes, void *dest);

int ec_max_outsize, ec_max_insize;
void *ec_outbuf;
void *ec_inbuf;

int comm_init_dev(void) __attribute__((weak));
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

int comm_init(void)
{
	/* Default memmap access */
	ec_readmem = fake_readmem;

	/* Prefer new /dev method */
	if (comm_init_dev && !comm_init_dev())
		goto init_ok;

	/* Fallback to direct LPC on x86 */
	if (comm_init_lpc && !comm_init_lpc())
		goto init_ok;

	/* Fallback to direct i2c on ARM */
	if (comm_init_i2c && !comm_init_i2c())
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

	return 0;

}
