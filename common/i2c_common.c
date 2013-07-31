/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* I2C cross-platform code for Chrome EC */

#include "clock.h"
#include "console.h"
#include "host_command.h"
#include "i2c.h"
#include "system.h"
#include "task.h"
#include "util.h"
#include "watchdog.h"

#define CPUTS(outstr) cputs(CC_I2C, outstr)
#define CPRINTF(format, args...) cprintf(CC_I2C, format, ## args)

static struct mutex port_mutex[I2C_PORT_COUNT];

void i2c_lock(int port, int lock)
{
	if (lock) {
#ifdef CHIP_stm32
		/* Don't allow deep sleep when I2C port is locked */
		disable_sleep(SLEEP_MASK_I2C);
#endif
		mutex_lock(port_mutex + port);
	} else {
		mutex_unlock(port_mutex + port);
#ifdef CHIP_stm32
		/* Allow deep sleep again after I2C port is unlocked */
		enable_sleep(SLEEP_MASK_I2C);
#endif
	}
}

int i2c_read16(int port, int slave_addr, int offset, int *data)
{
	int rv;
	uint8_t reg, buf[2];

	reg = offset & 0xff;
	/* I2C read 16-bit word: transmit 8-bit offset, and read 16bits */
	i2c_lock(port, 1);
	rv = i2c_xfer(port, slave_addr, &reg, 1, buf, 2, I2C_XFER_SINGLE);
	i2c_lock(port, 0);

	if (rv)
		return rv;

	if (slave_addr & I2C_FLAG_BIG_ENDIAN)
		*data = ((int)buf[0] << 8) | buf[1];
	else
		*data = ((int)buf[1] << 8) | buf[0];

	return EC_SUCCESS;
}

int i2c_write16(int port, int slave_addr, int offset, int data)
{
	int rv;
	uint8_t buf[3];

	buf[0] = offset & 0xff;

	if (slave_addr & I2C_FLAG_BIG_ENDIAN) {
		buf[1] = (data >> 8) & 0xff;
		buf[2] = data & 0xff;
	} else {
		buf[1] = data & 0xff;
		buf[2] = (data >> 8) & 0xff;
	}

	i2c_lock(port, 1);
	rv = i2c_xfer(port, slave_addr, buf, 3, NULL, 0, I2C_XFER_SINGLE);
	i2c_lock(port, 0);

	return rv;
}

int i2c_read8(int port, int slave_addr, int offset, int *data)
{
	int rv;
	/* We use buf[1] here so it's aligned for DMA on STM32 */
	uint8_t reg, buf[1];

	reg = offset;

	i2c_lock(port, 1);
	rv = i2c_xfer(port, slave_addr, &reg, 1, buf, 1, I2C_XFER_SINGLE);
	i2c_lock(port, 0);

	if (!rv)
		*data = buf[0];

	return rv;
}

int i2c_write8(int port, int slave_addr, int offset, int data)
{
	int rv;
	uint8_t buf[2];

	buf[0] = offset;
	buf[1] = data;

	i2c_lock(port, 1);
	rv = i2c_xfer(port, slave_addr, buf, 2, 0, 0, I2C_XFER_SINGLE);
	i2c_lock(port, 0);

	return rv;
}

/*****************************************************************************/
/* Host commands */

/* TODO: replace with single I2C passthru command */

static int i2c_command_read(struct host_cmd_handler_args *args)
{
	const struct ec_params_i2c_read *p = args->params;
	struct ec_response_i2c_read *r = args->response;
	int data, rv = -1;

#ifdef CONFIG_I2C_PASSTHRU_RESTRICTED
	if (system_is_locked())
		return EC_RES_ACCESS_DENIED;
#endif

	if  (p->read_size == 16)
		rv = i2c_read16(p->port, p->addr, p->offset, &data);
	else if (p->read_size == 8)
		rv = i2c_read8(p->port, p->addr, p->offset, &data);

	if (rv)
		return EC_RES_ERROR;
	r->data = data;
	args->response_size = sizeof(*r);

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_I2C_READ, i2c_command_read, EC_VER_MASK(0));

static int i2c_command_write(struct host_cmd_handler_args *args)
{
	const struct ec_params_i2c_write *p = args->params;
	int rv = -1;

#ifdef CONFIG_I2C_PASSTHRU_RESTRICTED
	if (system_is_locked())
		return EC_RES_ACCESS_DENIED;
#endif

	if (p->write_size == 16)
		rv = i2c_write16(p->port, p->addr, p->offset, p->data);
	else if (p->write_size == 8)
		rv = i2c_write8(p->port, p->addr, p->offset, p->data);

	if (rv)
		return EC_RES_ERROR;

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_I2C_WRITE, i2c_command_write, EC_VER_MASK(0));

/* TODO: remove temporary extra debugging for help host-side debugging */
#ifdef CONFIG_I2C_DEBUG_PASSTHRU
#define PTHRUPRINTF(format, args...) cprintf(CC_I2C, format, ## args)
#else
#define PTHRUPRINTF(format, args...)
#endif

/**
 * Perform the voluminous checking required for this message
 *
 * @param args	Arguments
 * @return 0 if OK, EC_RES_INVALID_PARAM on error
 */
static int check_i2c_params(const struct host_cmd_handler_args *args)
{
	const struct ec_params_i2c_passthru *params = args->params;
	const struct ec_params_i2c_passthru_msg *msg;
	int read_len = 0, write_len = 0;
	unsigned int size;
	int msgnum;

	if (args->params_size < sizeof(*params)) {
		PTHRUPRINTF("[%T i2c passthru no params, params_size=%d, need at least %d]\n",
			    args->params_size, sizeof(*params));
		return EC_RES_INVALID_PARAM;
	}
	size = sizeof(*params) + params->num_msgs * sizeof(*msg);
	if (args->params_size < size) {
		PTHRUPRINTF("[%T i2c passthru params_size=%d, need at least %d]\n",
			    args->params_size, size);
		return EC_RES_INVALID_PARAM;
	}

	if (params->port >= I2C_PORT_COUNT) {
		PTHRUPRINTF("[%T i2c passthru invalid port %d]\n",
			    params->port);
		return EC_RES_INVALID_PARAM;
	}

	/* Loop and process messages */;
	for (msgnum = 0, msg = params->msg; msgnum < params->num_msgs;
	     msgnum++, msg++) {
		unsigned int addr_flags = msg->addr_flags;

		/* Parse slave address if necessary */
		if (addr_flags & EC_I2C_FLAG_10BIT) {
			/* 10-bit addressing not supported yet */
			PTHRUPRINTF("[%T i2c passthru no 10-bit addressing]\n");
			return EC_RES_INVALID_PARAM;
		}

		PTHRUPRINTF("[%T i2c passthru port=%d, %s, addr=0x%02x, len=0x%02x]\n",
			    params->port,
			    addr_flags & EC_I2C_FLAG_READ ? "read" : "write",
			    addr_flags & EC_I2C_ADDR_MASK,
			    msg->len);

		if (addr_flags & EC_I2C_FLAG_READ)
			read_len += msg->len;
		else
			write_len += msg->len;
	}

	/* Check there is room for the data */
	if (args->response_max <
			sizeof(struct ec_response_i2c_passthru) + read_len) {
		PTHRUPRINTF("[%T i2c passthru overflow1]\n");
		return EC_RES_INVALID_PARAM;
	}

	/* Must have bytes to write */
	if (args->params_size < size + write_len) {
		PTHRUPRINTF("[%T i2c passthru overflow2]\n");
		return EC_RES_INVALID_PARAM;
	}

	return EC_RES_SUCCESS;
}

static int i2c_command_passthru(struct host_cmd_handler_args *args)
{
	const struct ec_params_i2c_passthru *params = args->params;
	const struct ec_params_i2c_passthru_msg *msg;
	struct ec_response_i2c_passthru *resp = args->response;
	const uint8_t *out;
	int in_len;
	int ret;

#ifdef CONFIG_I2C_PASSTHRU_RESTRICTED
	if (system_is_locked())
		return EC_RES_ACCESS_DENIED;
#endif

	ret = check_i2c_params(args);
	if (ret)
		return ret;

	/* Loop and process messages */
	resp->i2c_status = 0;
	out = args->params + sizeof(*params) + params->num_msgs * sizeof(*msg);
	in_len = 0;

	i2c_lock(params->port, 1);

	for (resp->num_msgs = 0, msg = params->msg;
	     resp->num_msgs < params->num_msgs;
	     resp->num_msgs++, msg++) {
		/* EC uses 8-bit slave address */
		unsigned int addr = (msg->addr_flags & EC_I2C_ADDR_MASK) << 1;
		int xferflags = I2C_XFER_START;
		int read_len = 0, write_len = 0;
		int rv;

		if (msg->addr_flags & EC_I2C_FLAG_READ)
			read_len = msg->len;
		else
			write_len = msg->len;

		/* Set stop bit for last message */
		if (resp->num_msgs == params->num_msgs - 1)
			xferflags |= I2C_XFER_STOP;

		/* Transfer next message */
		PTHRUPRINTF("[%T i2c passthru xfer port=%x, addr=%x, out=%p, write_len=%x, data=%p, read_len=%x, xferflags=%x]\n",
			    params->port, addr, out, write_len,
			    &resp->data[in_len], read_len, xferflags);
		rv = i2c_xfer(params->port, addr, out, write_len,
			      &resp->data[in_len], read_len, xferflags);
		if (rv) {
			/* Driver will have sent a stop bit here */
			if (rv == EC_ERROR_TIMEOUT)
				resp->i2c_status = EC_I2C_STATUS_TIMEOUT;
			else
				resp->i2c_status = EC_I2C_STATUS_NAK;
			break;
		}

		in_len += read_len;
		out += write_len;
	}
	args->response_size = sizeof(*resp) + in_len;

	/* Unlock port */
	i2c_lock(params->port, 0);

	/*
	 * Return success even if transfer failed so response is sent.  Host
	 * will check message status to determine the transfer result.
	 */
	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_I2C_PASSTHRU, i2c_command_passthru, EC_VER_MASK(0));

/*****************************************************************************/
/* Console commands */

static void scan_bus(int port, const char *desc)
{
	int a;
	uint8_t tmp;

	ccprintf("Scanning %d %s", port, desc);

	/* Don't scan a busy port, since reads will just fail / time out */
	a = i2c_get_line_levels(port);
	if (a != I2C_LINE_IDLE) {
		ccprintf(": port busy (SDA=%d, SCL=%d)\n",
			 (a & I2C_LINE_SDA_HIGH) ? 1 : 0,
			 (a & I2C_LINE_SCL_HIGH) ? 1 : 0);
		return;
	}

	i2c_lock(port, 1);

	for (a = 0; a < 0x100; a += 2) {
		watchdog_reload();  /* Otherwise a full scan trips watchdog */
		ccputs(".");

#ifdef CHIP_FAMILY_stm32f
		/*
		 * Hope that address 0 exists, because the i2c_xfer()
		 * implementation on STM32F can't read a byte without writing
		 * one first.
		 *
		 * TODO: remove when that limitation is fixed.
		 */
		tmp = 0;
		if (!i2c_xfer(port, a, &tmp, 1, &tmp, 1, I2C_XFER_SINGLE))
#else
		/* Do a single read */
		if (!i2c_xfer(port, a, NULL, 0, &tmp, 1, I2C_XFER_SINGLE))
#endif
			ccprintf("\n  0x%02x", a);
	}

	i2c_lock(port, 0);
	ccputs("\n");
}

static int command_scan(int argc, char **argv)
{
	int i;

	for (i = 0; i < I2C_PORTS_USED; i++)
		scan_bus(i2c_ports[i].port, i2c_ports[i].name);
	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(i2cscan, command_scan,
			NULL,
			"Scan I2C ports for devices",
			NULL);
