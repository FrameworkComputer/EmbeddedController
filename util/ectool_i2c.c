/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <endian.h>

#include "comm-host.h"
#include "ectool.h"

int cmd_i2c_protect(int argc, char *argv[])
{
	struct ec_params_i2c_passthru_protect p;
	char *e;
	int rv;

	if (argc != 2 && (argc != 3 || strcmp(argv[2], "status"))) {
		fprintf(stderr, "Usage: %s <port> [status]\n",
			argv[0]);
		return -1;
	}

	p.port = strtol(argv[1], &e, 0);
	if (e && *e) {
		fprintf(stderr, "Bad port.\n");
		return -1;
	}

	if (argc == 3) {
		struct ec_response_i2c_passthru_protect r;

		p.subcmd = EC_CMD_I2C_PASSTHRU_PROTECT_STATUS;

		rv = ec_command(EC_CMD_I2C_PASSTHRU_PROTECT, 0, &p, sizeof(p),
				&r, sizeof(r));

		if (rv < 0)
			return rv;

		printf("I2C port %d: %s (%d)\n", p.port,
		       r.status ? "Protected" : "Unprotected", r.status);
	} else {
		p.subcmd = EC_CMD_I2C_PASSTHRU_PROTECT_ENABLE;

		rv = ec_command(EC_CMD_I2C_PASSTHRU_PROTECT, 0, &p, sizeof(p),
				NULL, 0);

		if (rv < 0)
			return rv;
	}
	return 0;
}

static int do_i2c_xfer(unsigned int port, unsigned int addr,
		       uint8_t *write_buf, int write_len,
		       uint8_t **read_buf, int read_len)
{
	struct ec_params_i2c_passthru *p =
		(struct ec_params_i2c_passthru *)ec_outbuf;
	struct ec_response_i2c_passthru *r =
		(struct ec_response_i2c_passthru *)ec_inbuf;
	struct ec_params_i2c_passthru_msg *msg = p->msg;
	uint8_t *pdata;
	int size;
	int rv;

	p->port = port;
	p->num_msgs = (read_len != 0) + (write_len != 0);

	size = sizeof(*p) + p->num_msgs * sizeof(*msg);
	if (size + write_len > ec_max_outsize) {
		fprintf(stderr, "Params too large for buffer\n");
		return -1;
	}
	if (sizeof(*r) + read_len > ec_max_insize) {
		fprintf(stderr, "Read length too big for buffer\n");
		return -1;
	}

	pdata = (uint8_t *)p + size;
	if (write_len) {
		msg->addr_flags = addr;
		msg->len = write_len;

		memcpy(pdata, write_buf, write_len);
		msg++;
	}

	if (read_len) {
		msg->addr_flags = addr | EC_I2C_FLAG_READ;
		msg->len = read_len;
	}

	rv = ec_command(EC_CMD_I2C_PASSTHRU, 0, p, size + write_len,
			r, sizeof(*r) + read_len);
	if (rv < 0)
		return rv;

	/* Parse response */
	if (r->i2c_status & (EC_I2C_STATUS_NAK | EC_I2C_STATUS_TIMEOUT)) {
		fprintf(stderr, "Transfer failed with status=0x%x\n",
			r->i2c_status);
		return -1;
	}

	if (rv < sizeof(*r) + read_len) {
		fprintf(stderr, "Truncated read response\n");
		return -1;
	}

	if (read_len)
		*read_buf = r->data;

	return 0;
}

static void cmd_i2c_help(void)
{
	fprintf(stderr,
		"  Usage: i2cread <8 | 16> <port> <addr8> <offset>\n"
		"  Usage: i2cspeed <port> [speed in kHz]\n"
		"  Usage: i2cwrite <8 | 16> <port> <addr8> <offset> <data>\n"
		"  Usage: i2cxfer <port> <addr7> <read_count> [bytes...]\n"
		"    <port> i2c port number\n"
		"    <addr8> 8-bit i2c address\n"
		"    <addr7> 7-bit i2c address\n"
		"    <offset> offset to read from or write to\n"
		"    <data> data to write\n"
		"    <read_count> number of bytes to read\n"
		"    [bytes ...] data to write\n"
		);

}

int cmd_i2c_read(int argc, char *argv[])
{
	unsigned int port, addr8, addr7;
	int read_len, write_len;
	uint8_t write_buf[1];
	uint8_t *read_buf = NULL;
	char *e;
	int rv;

	if (argc != 5) {
		cmd_i2c_help();
		return -1;
	}

	read_len = strtol(argv[1], &e, 0);
	if ((e && *e) || (read_len != 8 && read_len != 16)) {
		fprintf(stderr, "Bad read size.\n");
		return -1;
	}
	read_len = read_len / 8;

	port = strtol(argv[2], &e, 0);
	if (e && *e) {
		fprintf(stderr, "Bad port.\n");
		return -1;
	}

	addr8 = strtol(argv[3], &e, 0);
	if (e && *e) {
		fprintf(stderr, "Bad address.\n");
		return -1;
	}
	addr7 = addr8 >> 1;

	write_buf[0] = strtol(argv[4], &e, 0);
	if (e && *e) {
		fprintf(stderr, "Bad offset.\n");
		return -1;
	}
	write_len = 1;

	rv = do_i2c_xfer(port, addr7, write_buf, write_len, &read_buf,
			 read_len);

	if (rv < 0)
		return rv;

	printf("Read from I2C port %d at 0x%x offset 0x%x = 0x%x\n",
	       port, addr8, write_buf[0], *(uint16_t *)read_buf);
	return 0;
}

int cmd_i2c_write(int argc, char *argv[])
{
	unsigned int port, addr8, addr7;
	int write_len;
	uint8_t write_buf[3];
	char *e;
	int rv;

	if (argc != 6) {
		cmd_i2c_help();
		return -1;
	}

	write_len = strtol(argv[1], &e, 0);
	if ((e && *e) || (write_len != 8 && write_len != 16)) {
		fprintf(stderr, "Bad write size.\n");
		return -1;
	}
	/* Include offset (length 1) */
	write_len = 1 + write_len / 8;

	port = strtol(argv[2], &e, 0);
	if (e && *e) {
		fprintf(stderr, "Bad port.\n");
		return -1;
	}

	addr8 = strtol(argv[3], &e, 0);
	if (e && *e) {
		fprintf(stderr, "Bad address.\n");
		return -1;
	}
	addr7 = addr8 >> 1;

	write_buf[0] = strtol(argv[4], &e, 0);
	if (e && *e) {
		fprintf(stderr, "Bad offset.\n");
		return -1;
	}

	*((uint16_t *)&write_buf[1]) = strtol(argv[5], &e, 0);
	if (e && *e) {
		fprintf(stderr, "Bad data.\n");
		return -1;
	}

	rv = do_i2c_xfer(port, addr7, write_buf, write_len, NULL, 0);

	if (rv < 0)
		return rv;

	printf("Wrote 0x%x to I2C port %d at 0x%x offset 0x%x.\n",
	       *((uint16_t *)&write_buf[1]), port, addr8, write_buf[0]);
	return 0;
}

int cmd_i2c_xfer(int argc, char *argv[])
{
	unsigned int port, addr;
	int read_len, write_len;
	uint8_t *write_buf = NULL;
	uint8_t *read_buf;
	char *e;
	int rv, i;

	if (argc < 4) {
		cmd_i2c_help();
		return -1;
	}

	port = strtol(argv[1], &e, 0);
	if (e && *e) {
		fprintf(stderr, "Bad port.\n");
		return -1;
	}

	addr = strtol(argv[2], &e, 0) & 0x7f;
	if (e && *e) {
		fprintf(stderr, "Bad peripheral address.\n");
		return -1;
	}

	read_len = strtol(argv[3], &e, 0);
	if (e && *e) {
		fprintf(stderr, "Bad read length.\n");
		return -1;
	}

	/* Skip over params to bytes to write */
	argc -= 4;
	argv += 4;
	write_len = argc;

	if (write_len) {
		write_buf = (uint8_t *)(malloc(write_len));
		if (write_buf == NULL)
			return -1;
		for (i = 0; i < write_len; i++) {
			write_buf[i] = strtol(argv[i], &e, 0);
			if (e && *e) {
				fprintf(stderr, "Bad write byte %d\n", i);
				free(write_buf);
				return -1;
			}
		}
	}

	rv = do_i2c_xfer(port, addr, write_buf, write_len, &read_buf, read_len);

	if (write_len)
		free(write_buf);

	if (rv)
		return rv;

	if (read_len) {
		if (ascii_mode) {
			for (i = 0; i < read_len; i++)
				printf(isprint(read_buf[i]) ? "%c" : "\\x%02x",
				       read_buf[i]);
		} else {
			printf("Read bytes:");
			for (i = 0; i < read_len; i++)
				printf(" %#02x", read_buf[i]);
		}
		printf("\n");
	} else {
		printf("Write successful.\n");
	}

	return 0;
}

static int i2c_get(int port)
{
	struct ec_params_i2c_control  p;
	struct ec_response_i2c_control r;
	uint16_t speed_khz;
	int rv;

	memset(&p, 0, sizeof(p));
	p.port = port;
	p.cmd = EC_I2C_CONTROL_GET_SPEED;

	rv = ec_command(EC_CMD_I2C_CONTROL, 0, &p, sizeof(p), &r, sizeof(r));
	if (rv < 0)
		return rv;

	speed_khz = r.cmd_response.speed_khz;
	if (speed_khz == EC_I2C_CONTROL_SPEED_UNKNOWN)
		printf("I2C port %d: speed: unknown\n", port);
	else
		printf("I2C port %d: speed: %u kHz\n", port, speed_khz);

	return 0;
}

static int i2c_set(int port, int new_speed_khz)
{
	struct ec_params_i2c_control  p;
	struct ec_response_i2c_control r;
	uint16_t old_speed_khz;
	int rv;

	if ((new_speed_khz == EC_I2C_CONTROL_SPEED_UNKNOWN) ||
	    (new_speed_khz < 0 || new_speed_khz > UINT16_MAX)) {
		fprintf(stderr, "I2C speed %d kHz is not supported\n",
			new_speed_khz);
		return -1;
	}

	memset(&p, 0, sizeof(p));
	p.port = port;
	p.cmd = EC_I2C_CONTROL_SET_SPEED;
	p.cmd_params.speed_khz = new_speed_khz;

	rv = ec_command(EC_CMD_I2C_CONTROL, 0, &p, sizeof(p), &r, sizeof(r));
	if (rv < 0)
		return rv;

	old_speed_khz = r.cmd_response.speed_khz;
	if (old_speed_khz == EC_I2C_CONTROL_SPEED_UNKNOWN) {
		printf("Port %d speed set to %d kHz\n", port, new_speed_khz);
	} else {
		printf("Port %d speed changed from %u kHz to %d kHz\n", port,
		       old_speed_khz,
		       new_speed_khz);
	}

	return 0;
}

int cmd_i2c_speed(int argc, char *argv[])
{
	unsigned int port, speed;
	char *e;

	if (argc < 2 || argc > 3) {
		cmd_i2c_help();
		return -1;
	}

	port = strtol(argv[1], &e, 0);
	if (e && *e) {
		fprintf(stderr, "Bad port.\n");
		return -1;
	}

	if (argc == 2)
		return i2c_get(port);

	speed = strtol(argv[2], &e, 0);
	if (e && *e) {
		fprintf(stderr, "Bad speed. "
			"Typical speeds are one of {100,400,1000}.\n");
		return -1;
	}

	return i2c_set(port, speed);
}
