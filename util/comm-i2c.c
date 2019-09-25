/* Copyright 2012 The Chromium OS Authors. All rights reserved.
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
#include <unistd.h>

#include "comm-host.h"
#include "i2c.h"

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

static int sum_bytes(const void *data, int length)
{
	const uint8_t *bytes = (const uint8_t *)data;
	int sum = 0;
	int i;

	for (i = 0; i < length; i++)
		sum += bytes[i];
	return sum;
}

static void dump_buffer(const uint8_t *data, int length)
{
	int i;

	for (i = 0; i < length; i++)
		fprintf(stderr, "%02X ", data[i]);
	fprintf(stderr, "\n");
}

/*
 * Sends a command to the EC (protocol v3). Returns the command status code
 * (>= 0), or a negative EC_RES_* value on error.
 */
static int ec_command_i2c_3(int command, int version,
			    const void *outdata, int outsize,
			    void *indata, int insize)
{
	int ret = -EC_RES_ERROR;
	int error;
	int req_len, resp_len;
	uint8_t *req_buf = NULL;
	uint8_t *resp_buf = NULL;
	struct ec_host_request *req;
	struct ec_host_response *resp;
	uint8_t command_return_code;
	struct i2c_msg i2c_msg;
	struct i2c_rdwr_ioctl_data data;

	if (outsize > ec_max_outsize) {
		fprintf(stderr, "Request is too large (%d > %d).\n", outsize,
			ec_max_outsize);
		return -EC_RES_ERROR;
	}
	if (insize > ec_max_insize) {
		fprintf(stderr, "Response would be too large (%d > %d).\n",
			insize, ec_max_insize);
		return -EC_RES_ERROR;
	}
	req_len = I2C_REQUEST_HEADER_SIZE + sizeof(struct ec_host_request)
		+ outsize;
	req_buf = calloc(1, req_len);
	if (!req_buf)
		goto done;

	req_buf[0] = EC_COMMAND_PROTOCOL_3;
	req = (struct ec_host_request *)&req_buf[1];
	req->struct_version = EC_HOST_REQUEST_VERSION;
	req->checksum = 0;
	req->command = command;
	req->command_version = version;
	req->reserved = 0;
	req->data_len = outsize;

	memcpy(&req_buf[I2C_REQUEST_HEADER_SIZE
			+ sizeof(struct ec_host_request)],
	       outdata, outsize);

	req->checksum =
		(uint8_t)(-sum_bytes(&req_buf[I2C_REQUEST_HEADER_SIZE],
				     req_len - I2C_REQUEST_HEADER_SIZE));

	i2c_msg.addr = EC_I2C_ADDR;
	i2c_msg.flags = 0;
	i2c_msg.len = req_len;
	i2c_msg.buf = (char *)req_buf;

	resp_len = I2C_RESPONSE_HEADER_SIZE + sizeof(struct ec_host_response)
		+ insize;
	resp_buf = calloc(1, resp_len);
	if (!resp_buf)
		goto done;
	memset(resp_buf, 0, resp_len);

	if (IS_ENABLED(DEBUG)) {
		fprintf(stderr, "Sending: 0x");
		dump_buffer(req_buf, req_len);
	}

	/*
	 * Combining these two ioctls makes the write-read interval too short
	 * for some chips (such as the MAX32660) to handle.
	 */
	data.msgs = &i2c_msg;
	data.nmsgs = 1;
	error = ioctl(i2c_fd, I2C_RDWR, &data);
	if (error < 0) {
		fprintf(stderr, "I2C write failed: %d (err: %d, %s)\n",
			error, errno, strerror(errno));
		goto done;
	}

	i2c_msg.addr = EC_I2C_ADDR;
	i2c_msg.flags = I2C_M_RD;
	i2c_msg.len = resp_len;
	i2c_msg.buf = (char *)resp_buf;
	error = ioctl(i2c_fd, I2C_RDWR, &data);
	if (error < 0) {
		fprintf(stderr, "I2C read failed: %d (err: %d, %s)\n",
			error, errno, strerror(errno));
		goto done;
	}

	if (IS_ENABLED(DEBUG)) {
		fprintf(stderr, "Received: 0x");
		dump_buffer(resp_buf, resp_len);
	}

	command_return_code = resp_buf[0];
	if (command_return_code != EC_RES_SUCCESS) {
		debug("command 0x%02x returned an error %d\n", command,
		      command_return_code);
		ret = -EECRESULT - command_return_code;
		goto done;
	}

	if (resp_buf[1] > sizeof(struct ec_host_response) + insize) {
		debug("EC returned too much data.\n");
		ret = -EC_RES_RESPONSE_TOO_BIG;
		goto done;
	}

	resp = (struct ec_host_response *)(&resp_buf[2]);
	if (resp->struct_version != EC_HOST_RESPONSE_VERSION) {
		debug("EC response version mismatch.\n");
		ret = -EC_RES_INVALID_RESPONSE;
		goto done;
	}

	if ((uint8_t)sum_bytes(&resp_buf[I2C_RESPONSE_HEADER_SIZE], resp_buf[1])
			!= 0) {
		debug("Bad checksum on EC response.\n");
		ret = -EC_RES_INVALID_CHECKSUM;
		goto done;
	}

	memcpy(indata, &resp_buf[I2C_RESPONSE_HEADER_SIZE
				 + sizeof(struct ec_host_response)],
	       insize);

	ret = resp->data_len;
done:
	if (req_buf)
		free(req_buf);
	if (resp_buf)
		free(resp_buf);

	return ret;
}

int comm_init_i2c(int i2c_bus)
{
	char *file_path;
	char buffer[64];
	int i;

	if (i2c_bus != -1) {
		i = i2c_bus;

		if (i >= I2C_MAX_ADAPTER) {
			fprintf(stderr, "Invalid I2C bus number %d. (The highest possible bus number is %d.)\n",
				i, I2C_MAX_ADAPTER);
			return -1;
		}
	} else {
		/* find the device number based on the adapter name */
		for (i = 0; i < I2C_MAX_ADAPTER; i++) {
			FILE *f;

			if (asprintf(&file_path, I2C_ADAPTER_NODE,
				     i, i, EC_I2C_ADDR) < 0)
				return -1;
			f = fopen(file_path, "r");
			if (f) {
				if (fgets(buffer, sizeof(buffer), f) &&
				    !strncmp(buffer, I2C_ADAPTER_NAME, 6)) {
					free(file_path);
					fclose(f);
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
	}

	if (asprintf(&file_path, I2C_NODE, i) < 0)
		return -1;
	debug("using I2C adapter %s\n", file_path);
	i2c_fd = open(file_path, O_RDWR);
	if (i2c_fd < 0)
		fprintf(stderr, "Cannot open %s : %d\n", file_path, errno);

	free(file_path);

	ec_command_proto = ec_command_i2c_3;
	ec_max_outsize = I2C_MAX_HOST_PACKET_SIZE - I2C_REQUEST_HEADER_SIZE
		- sizeof(struct ec_host_request);
	ec_max_insize = I2C_MAX_HOST_PACKET_SIZE - I2C_RESPONSE_HEADER_SIZE
		- sizeof(struct ec_host_response);

	return 0;
}
