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

#define EC_I2C_ADDR 0x1e

#define I2C_ADAPTER_NODE "/sys/class/i2c-adapter/i2c-%d/%d-%04x/name"
#define I2C_ADAPTER_NAME "cros-ec-i2c"
#define I2C_MAX_ADAPTER  32
#define I2C_NODE "/dev/i2c-%d"

#ifdef DEBUG
#define debug(format, arg...) printf(format, ##arg)
#else
#define debug(...)
#endif

static int i2c_fd = -1;

/*
 * Sends a command to the EC (protocol v2).  Returns the command status code, or
 * -1 if other error.
 *
 * Returns >= 0 for success, or negative if error.
 *
 */
static int ec_command_i2c(int command, int version,
			  const void *outdata, int outsize,
			  void *indata, int insize)
{
	struct i2c_rdwr_ioctl_data data;
	int ret = -1;
	int i;
	int req_len;
	uint8_t *req_buf = NULL;
	int resp_len;
	uint8_t *resp_buf = NULL;
	const uint8_t *c;
	uint8_t *d;
	uint8_t sum;
	struct i2c_msg i2c_msg[2];

	if (version > 1) {
		fprintf(stderr, "Command versions >1 unsupported.\n");
		return -EC_RES_ERROR;
	}

	if (i2c_fd < 0) {
		fprintf(stderr, "i2c_fd is negative: %d\n", i2c_fd);
		return -EC_RES_ERROR;
	}

	if (ioctl(i2c_fd, I2C_SLAVE, EC_I2C_ADDR) < 0) {
		fprintf(stderr, "Cannot set I2C slave address\n");
		return -EC_RES_ERROR;
	}

	i2c_msg[0].addr = EC_I2C_ADDR;
	i2c_msg[0].flags = 0;
	i2c_msg[1].addr = EC_I2C_ADDR;
	i2c_msg[1].flags = I2C_M_RD;
	data.msgs = i2c_msg;
	data.nmsgs = 2;

	/*
	 * allocate larger packet
	 * (version, command, size, ..., checksum)
	 */
	req_len = outsize + EC_PROTO2_REQUEST_OVERHEAD;
	req_buf = calloc(1, req_len);
	if (!req_buf)
		goto done;
	i2c_msg[0].len = req_len;
	i2c_msg[0].buf = (char *)req_buf;
	req_buf[0] = version + EC_CMD_VERSION0;
	req_buf[1] = command;
	req_buf[2] = outsize;

	debug("i2c req %02x:", command);
	sum = req_buf[0] + req_buf[1] + req_buf[2];
	/* copy message payload and compute checksum */
	for (i = 0, c = outdata; i < outsize; i++, c++) {
		req_buf[i + 3] = *c;
		sum += *c;
		debug(" %02x", *c);
	}
	debug(", sum=%02x\n", sum);
	req_buf[req_len - 1] = sum;

	/*
	 * allocate larger packet
	 * (result, size, ..., checksum)
	 */
	resp_len = insize + EC_PROTO2_RESPONSE_OVERHEAD;
	resp_buf = calloc(1, resp_len);
	if (!resp_buf)
		goto done;
	i2c_msg[1].len = resp_len;
	i2c_msg[1].buf = (char *)resp_buf;

	/* send command to EC and read answer */
	ret = ioctl(i2c_fd, I2C_RDWR, &data);
	if (ret < 0) {
		fprintf(stderr, "i2c transfer failed: %d (err: %d)\n",
			ret, errno);
		ret = -EC_RES_ERROR;
		goto done;
	}

	/* check response error code */
	ret = resp_buf[0];

	/* TODO(crosbug.com/p/23824): handle EC_RES_IN_PROGRESS case. */

	resp_len = resp_buf[1];
	if (resp_len > insize) {
		fprintf(stderr, "response size is too large %d > %d\n",
				resp_len, insize);
		ret = -EC_RES_ERROR;
		goto done;
	}

	if (ret) {
		debug("command 0x%02x returned an error %d\n",
			 command, i2c_msg[1].buf[0]);
		/* Translate ERROR to -ERROR and offset */
		ret = -EECRESULT - ret;
	} else if (insize) {
		debug("i2c resp  :");
		/* copy response packet payload and compute checksum */
		sum = resp_buf[0] + resp_buf[1];
		for (i = 0, d = indata; i < resp_len; i++, d++) {
			*d = resp_buf[i + 2];
			sum += *d;
			debug(" %02x", *d);
		}
		debug(", sum=%02x\n", sum);

		if (sum != resp_buf[resp_len + 2]) {
			fprintf(stderr, "bad packet checksum\n");
			ret = -EC_RES_ERROR;
			goto done;
		}

		/* Return output buffer size */
		ret = resp_len;
	}
done:
	if (resp_buf)
		free(resp_buf);
	if (req_buf)
		free(req_buf);
	return ret;
}

int comm_init_i2c(void)
{
	char *file_path;
	char buffer[64];
	FILE *f;
	int i;

	/* find the device number based on the adapter name */
	for (i = 0; i < I2C_MAX_ADAPTER; i++) {
		if (asprintf(&file_path, I2C_ADAPTER_NODE,
			     i, i, EC_I2C_ADDR) < 0)
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

	ec_command_proto = ec_command_i2c;
	ec_max_outsize = ec_max_insize = EC_PROTO2_MAX_PARAM_SIZE;

	return 0;
}
