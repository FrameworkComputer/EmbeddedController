// SPDX-License-Identifier: GPL-2.0
/*
 * I2C adapter userspace example
 *
 * Copyright 2023 Google LLC
 *
 * This example shows how to use the i2c-pseudo module UAPI.
 * This program starts an I2C adapter and prints the I2C transfers it receives.
 * I2C reads are filled by reading from stdin.
 */

#include <errno.h>
#include <stdio.h>

#include <fcntl.h>
#include <linux/i2c-pseudo.h>
#include <linux/i2c.h>
#include <sys/ioctl.h>
#include <unistd.h>

#define MAX_MSGS_PER_XFER 6
#define MAX_DATA_PER_XFER 30

/* Start the I2C adapter. Returns 0 if successful, errno otherwise. */
static int start_adapter(int fd)
{
	int ret;
	struct i2cp_ioctl_start_arg start_arg = {
		.functionality = I2C_FUNC_I2C,
		.timeout_ms = 5000,
		.name = "example userspace I2C adapter",
	};
	if (ioctl(fd, I2CP_IOCTL_START, &start_arg) < 0) {
		ret = errno;
		perror("I2CP_IOCTL_START failed");
		return ret;
	}
	printf("adapter_num=%llu\n",
	       (unsigned long long)start_arg.output.adapter_num);
	return 0;
}

/* Fill msg->buf from stdin. Returns 0 if successful, errno otherwise. */
static int fill_read_buf(struct i2c_msg *msg)
{
	int ret;
	for (int i = 0; i < msg->len; i += ret) {
		ret = read(0, &msg->buf[i], msg->len - i);
		if (ret < 0) {
			ret = errno;
			perror("stdin read() failed");
			return ret;
		}
	}
	return 0;
}

/* Print msg to stdout. Returns 0 if successful, errno otherwise. */
static int print_msg(struct i2c_msg *msg)
{
	int ret;
	printf("addr=0x%02hx flags=0x%02hx len=%hu ", (short)msg->addr,
	       (short)msg->flags, (short)msg->len);
	if (!(msg->flags & I2C_M_RD)) {
		printf("write=[");
	} else {
		printf("read=[");
		/* stdin source may need to see the "read=" request */
		fflush(stdout);
		ret = fill_read_buf(msg);
		if (ret)
			return ret;
	}
	for (int i = 0; i < msg->len; i++) {
		if (i)
			printf(" ");
		printf("0x%02hhx", msg->buf[i]);
	}
	printf("]\n");
	return 0;
}

/* Process I2C transfers. Returns errno upon any interruption or failure. */
static int xfer_loop(int fd)
{
	int ret;
	struct i2c_msg msgs[MAX_MSGS_PER_XFER];
	__u8 data_buf[MAX_DATA_PER_XFER];
	struct i2cp_ioctl_xfer_req_arg req_arg = {
		.msgs = &msgs[0],
		.msgs_len = MAX_MSGS_PER_XFER,
		.data_buf = &data_buf[0],
		.data_buf_len = MAX_DATA_PER_XFER,
	};
	struct i2cp_ioctl_xfer_reply_arg reply_arg = {
		.msgs = &msgs[0],
	};

	for (;;) {
		if (ioctl(fd, I2CP_IOCTL_XFER_REQ, &req_arg) < 0) {
			ret = errno;
			perror("I2CP_IOCTL_XFER_REQ failed");
			switch (ret) {
			case ENOBUFS:
			case EMSGSIZE:
				break;
			default:
				return ret;
			}
			reply_arg.num_msgs = 0;
			reply_arg.error = ret;
		} else {
			reply_arg.num_msgs = 0;
			reply_arg.error = 0;
			printf("\nbegin transaction\n");
			for (int i = 0; i < req_arg.output.num_msgs; i++) {
				ret = print_msg(&msgs[i]);
				if (ret) {
					reply_arg.error = ret;
					break;
				}
				reply_arg.num_msgs++;
			}
			if (!reply_arg.error)
				printf("end transaction\n");
		}
		reply_arg.xfer_id = req_arg.output.xfer_id;
		if (ioctl(fd, I2CP_IOCTL_XFER_REPLY, &reply_arg) < 0) {
			ret = errno;
			perror("I2CP_IOCTL_XFER_REPLY failed");
			if (!reply_arg.error)
				return ret;
		}
		if (reply_arg.error)
			return reply_arg.error;
	}
}

int main(void)
{
	int fd = open("/dev/i2c-pseudo", O_RDWR);
	if (fd < 0) {
		perror("Failed to open() i2c-pseudo device file");
		return 1;
	}
	if (start_adapter(fd))
		return 2;
	if (xfer_loop(fd))
		return 3;
	if (close(fd) < 0) {
		perror("Failed to close() i2c-pseudo device file");
		return 4;
	}
	return 0;
}
