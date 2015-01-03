/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Host command master module for Chrome EC */

#include "common.h"
#include "console.h"
#include "host_command.h"
#include "i2c.h"
#include "task.h"
#include "timer.h"
#include "usb_pd.h"
#include "util.h"

/* Console output macros */
#define CPUTS(outstr) cputs(CC_HOSTCMD, outstr)
#define CPRINTF(format, args...) cprintf(CC_HOSTCMD, format, ## args)

/* Number of attempts for each PD host command */
#define PD_HOST_COMMAND_ATTEMPTS 3

static struct mutex pd_mutex;

/**
 * Non-task-safe internal version of pd_host_command().
 *
 * Do not call this version directly!  Use pd_host_command().
 */
static int pd_host_command_internal(int command, int version,
				    const void *outdata, int outsize,
				    void *indata, int insize)
{
	int ret, i;
	int resp_len;
	struct ec_host_request rq;
	struct ec_host_response rs;
	static uint8_t req_buf[EC_LPC_HOST_PACKET_SIZE];
	static uint8_t resp_buf[EC_LPC_HOST_PACKET_SIZE];
	uint8_t sum = 0;
	const uint8_t *c;
	uint8_t *d;

	/* Fail if output size is too big */
	if (outsize + sizeof(rq) > EC_LPC_HOST_PACKET_SIZE)
		return -EC_RES_REQUEST_TRUNCATED;

	/* Fill in request packet */
	rq.struct_version = EC_HOST_REQUEST_VERSION;
	rq.checksum = 0;
	rq.command = command;
	rq.command_version = version;
	rq.reserved = 0;
	rq.data_len = outsize;

	/* Copy data and start checksum */
	for (i = 0, c = (const uint8_t *)outdata; i < outsize; i++, c++) {
		req_buf[sizeof(rq) + 1 + i] = *c;
		sum += *c;
	}

	/* Finish checksum */
	for (i = 0, c = (const uint8_t *)&rq; i < sizeof(rq); i++, c++)
		sum += *c;

	/* Write checksum field so the entire packet sums to 0 */
	rq.checksum = (uint8_t)(-sum);

	/* Copy header */
	for (i = 0, c = (const uint8_t *)&rq; i < sizeof(rq); i++, c++)
		req_buf[1 + i] = *c;

	/* Set command to use protocol v3 */
	req_buf[0] = EC_COMMAND_PROTOCOL_3;

	/*
	 * Transmit all data and receive 2 bytes for return value and response
	 * length.
	 */
	i2c_lock(I2C_PORT_PD_MCU, 1);
	i2c_set_timeout(I2C_PORT_PD_MCU, PD_HOST_COMMAND_TIMEOUT_US);
	ret = i2c_xfer(I2C_PORT_PD_MCU, CONFIG_USB_PD_I2C_SLAVE_ADDR,
			&req_buf[0], outsize + sizeof(rq) + 1, &resp_buf[0],
			2, I2C_XFER_START);
	i2c_set_timeout(I2C_PORT_PD_MCU, 0);
	if (ret) {
		i2c_lock(I2C_PORT_PD_MCU, 0);
		CPRINTF("[%T i2c transaction 1 failed: %d]\n", ret);
		return -EC_RES_BUS_ERROR;
	}

	resp_len = resp_buf[1];

	if (resp_len > (insize + sizeof(rs))) {
		/* Do a dummy read to generate stop condition */
		i2c_xfer(I2C_PORT_PD_MCU, CONFIG_USB_PD_I2C_SLAVE_ADDR,
			0, 0, &resp_buf[2], 1, I2C_XFER_STOP);
		i2c_lock(I2C_PORT_PD_MCU, 0);
		CPRINTF("[%T response size is too large %d > %d]\n",
				resp_len, insize + sizeof(rs));
		return -EC_RES_RESPONSE_TOO_BIG;
	}

	/* Receive remaining data */
	ret = i2c_xfer(I2C_PORT_PD_MCU, CONFIG_USB_PD_I2C_SLAVE_ADDR, 0, 0,
			&resp_buf[2], resp_len, I2C_XFER_STOP);
	i2c_lock(I2C_PORT_PD_MCU, 0);
	if (ret) {
		CPRINTF("[%T i2c transaction 2 failed: %d]\n", ret);
		return -EC_RES_BUS_ERROR;
	}

	/* Check for host command error code */
	ret = resp_buf[0];
	if (ret) {
		CPRINTF("[%T command 0x%02x returned error %d]\n", command,
			ret);
		return -ret;
	}

	/* Read back response header and start checksum */
	sum = 0;
	for (i = 0, d = (uint8_t *)&rs; i < sizeof(rs); i++, d++) {
		*d = resp_buf[i + 2];
		sum += *d;
	}

	if (rs.struct_version != EC_HOST_RESPONSE_VERSION) {
		CPRINTF("[%T PD response version mismatch]\n");
		return -EC_RES_INVALID_RESPONSE;
	}

	if (rs.reserved) {
		CPRINTF("[%T PD response reserved != 0]\n");
		return -EC_RES_INVALID_RESPONSE;
	}

	if (rs.data_len > insize) {
		CPRINTF("[%T PD returned too much data]\n");
		return -EC_RES_RESPONSE_TOO_BIG;
	}

	/* Read back data and update checksum */
	resp_len -= sizeof(rs);
	for (i = 0, d = (uint8_t *)indata; i < resp_len; i++, d++) {
		*d = resp_buf[sizeof(rs) + i + 2];
		sum += *d;
	}


	if ((uint8_t)sum) {
		CPRINTF("[%T command 0x%02x bad checksum returned: "
			"%d]\n", command, sum);
		return -EC_RES_INVALID_CHECKSUM;
	}

	/* Return output buffer size */
	return resp_len;
}

int pd_host_command(int command, int version,
		    const void *outdata, int outsize,
		    void *indata, int insize)
{
	int rv;
	int tries = 0;

	/* Try multiple times to send host command. */
	for (tries = 0; tries < PD_HOST_COMMAND_ATTEMPTS; tries++) {
		/* Acquire mutex */
		mutex_lock(&pd_mutex);
		/* Call internal version of host command */
		rv = pd_host_command_internal(command, version, outdata,
					      outsize, indata, insize);
		/* Release mutex */
		mutex_unlock(&pd_mutex);

		/* If host command error due to i2c bus error, try again. */
		if (rv != -EC_RES_BUS_ERROR)
			break;
		task_wait_event(50*MSEC);
	}

	return rv;
}

static int command_pd_mcu(int argc, char **argv)
{
	char *e;
	static char outbuf[128];
	static char inbuf[128];
	int command, version;
	int i, ret, tmp;

	if (argc < 3)
		return EC_ERROR_PARAM_COUNT;

	command = strtoi(argv[1], &e, 0);
	if (*e)
		return EC_ERROR_PARAM1;

	version = strtoi(argv[2], &e, 0);
	if (*e)
		return EC_ERROR_PARAM2;

	for (i = 3; i < argc; i++) {
		tmp = strtoi(argv[i], &e, 0);
		if (*e)
			return EC_ERROR_PARAM3;
		outbuf[i-3] = tmp;
	}

	ret = pd_host_command(command, version, &outbuf, argc - 3, &inbuf,
			sizeof(inbuf));

	ccprintf("Host command 0x%02x, returned %d\n", command, ret);
	for (i = 0; i < ret; i++)
		ccprintf("0x%02x\n", inbuf[i]);

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(pdcmd, command_pd_mcu,
			"cmd ver [params]",
			"Send PD host command",
			NULL);

