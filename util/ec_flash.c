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
#include "misc_util.h"

int ec_flash_read(uint8_t *buf, int offset, int size)
{
	struct ec_params_flash_read p;
	int rv;
	int i;

	/* Read data in chunks */
	for (i = 0; i < size; i += ec_max_insize) {
		p.offset = offset + i;
		p.size = MIN(size - i, ec_max_insize);
		rv = ec_command(EC_CMD_FLASH_READ, 0,
				&p, sizeof(p), ec_inbuf, p.size);
		if (rv < 0) {
			fprintf(stderr, "Read error at offset %d\n", i);
			return rv;
		}
		memcpy(buf + i, ec_inbuf, p.size);
	}

	return 0;
}

int ec_flash_verify(const uint8_t *buf, int offset, int size)
{
	uint8_t *rbuf = malloc(size);
	int rv;
	int i;

	if (!rbuf) {
		fprintf(stderr, "Unable to allocate buffer.\n");
		return -1;
	}

	rv = ec_flash_read(rbuf, offset, size);
	if (rv < 0) {
		free(rbuf);
		return rv;
	}

	for (i = 0; i < size; i++) {
		if (buf[i] != rbuf[i]) {
			fprintf(stderr, "Mismatch at offset 0x%x: "
				"want 0x%02x, got 0x%02x\n",
				i, buf[i], rbuf[i]);
			free(rbuf);
			return -1;
		}
	}

	free(rbuf);
	return 0;
}

int ec_flash_write(const uint8_t *buf, int offset, int size)
{
	struct ec_params_flash_write p;
	int rv;
	int i;

	/* Write data in chunks */
	for (i = 0; i < size; i += sizeof(p.data)) {
		p.offset = offset + i;
		p.size = MIN(size - i, sizeof(p.data));
		memcpy(p.data, buf + i, p.size);
		rv = ec_command(EC_CMD_FLASH_WRITE, 0, &p, sizeof(p), NULL, 0);
		if (rv < 0) {
			fprintf(stderr, "Write error at offset %d\n", i);
			return rv;
		}
	}

	return 0;
}

int ec_flash_erase(int offset, int size)
{
	struct ec_params_flash_erase p;

	p.offset = offset;
	p.size = size;

	return ec_command(EC_CMD_FLASH_ERASE, 0, &p, sizeof(p), NULL, 0);
}
