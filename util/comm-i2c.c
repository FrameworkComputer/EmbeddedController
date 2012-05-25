/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#define _GNU_SOURCE /* for asprintf */

#include <errno.h>
#include <fcntl.h>
#include <linux/i2c.h>
#include <linux/i2c-dev.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "comm-host.h"
#include "ec_commands.h"

#define EC_I2C_ADDR 0x1e

#define I2C_ADAPTER_NODE "/sys/class/i2c-adapter/i2c-%d/name"
#define I2C_ADAPTER_NAME "cros_ec_i2c"
#define I2C_MAX_ADAPTER  32
#define I2C_NODE "/dev/i2c-%d"

#ifdef DEBUG
#define debug(format, arg...) printf(format, ##arg)
#else
#define debug(...)
#endif

static int i2c_fd = -1;

int comm_init(void)
{
	char *file_path;
	char buffer[64];
	FILE *f;
	int i;

	/* find the device number based on the adapter name */
	for (i = 0; i < I2C_MAX_ADAPTER; i++) {
		if (asprintf(&file_path, I2C_ADAPTER_NODE, i) < 0)
			return -1;
		f = fopen(file_path, "r");
		if (f) {
			if (fgets(buffer, sizeof(buffer), f) &&
			    !strncmp(buffer, I2C_ADAPTER_NAME, 6)) {
				free(file_path);
				break;
			}
			fclose(f);
		}
		free(file_path);
	}
	if (i == I2C_MAX_ADAPTER) {
		fprintf(stderr, "Cannot find I2C adapter\n");
		return -1;
	}

	if (asprintf(&file_path, I2C_NODE, i) < 0)
		return -1;
	debug("using I2C adapter %s\n", file_path);
	i2c_fd = open(file_path, O_RDWR);
	if (i2c_fd < 0)
		fprintf(stderr, "Cannot open %s : %d\n", file_path, errno);

	free(file_path);
	return 0;
}


/* Sends a command to the EC.  Returns the command status code, or
 * -1 if other error. */
int ec_command(int command, const void *indata, int insize,
	       void *outdata, int outsize)
{
	struct i2c_rdwr_ioctl_data data;
	int ret = -1;
	int i;
	uint8_t res_code;
	uint8_t *req_buf = NULL;
	uint8_t *resp_buf = NULL;
	const uint8_t *c;
	uint8_t *d;
	uint8_t sum;
	struct i2c_msg i2c_msg[2];

	if (i2c_fd < 0)
		return -1;

	if (ioctl(i2c_fd, I2C_SLAVE, EC_I2C_ADDR) < 0) {
		fprintf(stderr, "Cannot set I2C slave address\n");
		return -1;
	}

	i2c_msg[0].addr = EC_I2C_ADDR;
	i2c_msg[0].flags = 0;
	i2c_msg[1].addr = EC_I2C_ADDR;
	i2c_msg[1].flags = I2C_M_RD;
	data.msgs = i2c_msg;
	data.nmsgs = 2;

	if (outsize) {
		/* allocate larger packet
		 * (one byte for checksum, one for result code)
		 */
		resp_buf = calloc(1, outsize + 2);
		if (!resp_buf)
			goto done;
		i2c_msg[1].len = outsize + 2;
		i2c_msg[1].buf = (char *)resp_buf;
	} else {
		i2c_msg[1].len = 1;
		i2c_msg[1].buf = (char *)&res_code;
	}

	if (insize) {
		/* allocate larger packet
		 * (one byte for checksum, one for command code)
		 */
		req_buf = calloc(1, insize + 2);
		if (!req_buf)
			goto done;
		i2c_msg[0].len = insize + 2;
		i2c_msg[0].buf = (char *)req_buf;
		req_buf[0] = command;

		debug("i2c req %02x:", command);
		/* copy message payload and compute checksum */
		for (i = 0, sum = 0, c = indata; i < insize; i++, c++) {
			req_buf[i + 1] = *c;
			sum += *c;
			debug(" %02x", *c);
		}
		debug(", sum=%02x\n", sum);
		req_buf[insize + 1] = sum;
	} else {
		i2c_msg[0].len = 1;
		i2c_msg[0].buf = (char *)&command; /* nasty cast */
	}

	/* send command to EC and read answer */
	ret = ioctl(i2c_fd, I2C_RDWR, &data);
	if (ret < 0) {
		fprintf(stderr, "i2c transfer failed: %d (err: %d)\n",
			ret, errno);
		goto done;
	}

	/* check response error code */
	ret = i2c_msg[1].buf[0];
	if (ret) {
		debug("command 0x%02x returned an error %d\n",
			 command, i2c_msg[1].buf[0]);
	} else if (outsize) {
		debug("i2c resp  :");
		/* copy response packet payload and compute checksum */
		for (i = 0, sum = 0, d = outdata; i < outsize; i++, d++) {
			*d = resp_buf[i + 1];
			sum += *d;
			debug(" %02x", *d);
		}
		debug(", sum=%02x\n", sum);

		if (sum != resp_buf[outsize + 1]) {
			debug("bad packet checksum\n");
			ret = -1;
			goto done;
		}
	}
done:
	if (resp_buf)
		free(resp_buf);
	if (req_buf)
		free(req_buf);
	return ret;
}


uint8_t read_mapped_mem8(uint8_t offset)
{
	/* Not implemented */
	return 0xff;
}


uint16_t read_mapped_mem16(uint8_t offset)
{
	/* Not implemented */
	return 0xffff;
}


uint32_t read_mapped_mem32(uint8_t offset)
{
	/* Not implemented */
	return 0xffffffff;
}


int read_mapped_string(uint8_t offset, char *buf)
{
	strncpy(buf, "NOT IMPLEMENTED", EC_MEMMAP_TEXT_MAX);
	return sizeof("NOT IMPLEMENTED");
}
