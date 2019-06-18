/* Copyright 2013 The Chromium OS Authors. All rights reserved.
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
#include "timer.h"

static const uint32_t ERASE_ASYNC_TIMEOUT = 10 * SECOND;
static const uint32_t ERASE_ASYNC_WAIT = 500 * MSEC;
static const int FLASH_ERASE_BUSY_RV = -EECRESULT - EC_RES_BUSY;

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
	struct ec_params_flash_write *p =
		(struct ec_params_flash_write *)ec_outbuf;
	struct ec_response_flash_info info;
	int pdata_max_size = (int)(ec_max_outsize - sizeof(*p));
	int step;
	int rv;
	int i;

	/*
	 * Determine whether we can use version 1 of the command with more
	 * data, or only version 0.
	 */
	if (!ec_cmd_version_supported(EC_CMD_FLASH_WRITE, EC_VER_FLASH_WRITE))
		pdata_max_size = EC_FLASH_WRITE_VER0_SIZE;

	/*
	 * Determine step size.  This must be a multiple of the write block
	 * size, and must also fit into the host parameter buffer.
	 */
	rv = ec_command(EC_CMD_FLASH_INFO, 0, NULL, 0, &info, sizeof(info));
	if (rv < 0)
		return rv;

	step = (pdata_max_size / info.write_block_size) * info.write_block_size;

	if (!step) {
		fprintf(stderr, "Write block size %d > max param size %d\n",
			info.write_block_size, pdata_max_size);
		return -1;
	}

	/* Write data in chunks */
	printf("Write size %d...\n", step);

	for (i = 0; i < size; i += step) {
		p->offset = offset + i;
		p->size = MIN(size - i, step);
		memcpy(p + 1, buf + i, p->size);
		rv = ec_command(EC_CMD_FLASH_WRITE, 0, p, sizeof(*p) + p->size,
				NULL, 0);
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

int ec_flash_erase_async(int offset, int size)
{
	struct ec_params_flash_erase_v1 p = { 0 };
	uint32_t timeout = 0;
	int rv = FLASH_ERASE_BUSY_RV;

	p.cmd = FLASH_ERASE_SECTOR_ASYNC;
	p.params.offset = offset;
	p.params.size = size;

	rv = ec_command(EC_CMD_FLASH_ERASE, 1, &p, sizeof(p), NULL, 0);

	if (rv < 0)
		return rv;

	rv = FLASH_ERASE_BUSY_RV;

	while (rv < 0 && timeout < ERASE_ASYNC_TIMEOUT) {
		/*
		 * The erase is not complete until FLASH_ERASE_GET_RESULT
		 * returns success. It's important that we retry even when the
		 * underlying ioctl returns an error (not just
		 * FLASH_ERASE_BUSY_RV).
		 *
		 * See https://crrev.com/c/511805 for details.
		 */
		usleep(ERASE_ASYNC_WAIT);
		timeout += ERASE_ASYNC_WAIT;
		p.cmd = FLASH_ERASE_GET_RESULT;
		rv = ec_command(EC_CMD_FLASH_ERASE, 1, &p, sizeof(p), NULL, 0);
	}
	return rv;
}
