/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* I2C cross-platform code for Chrome EC */

#include "battery.h"
#include "clock.h"
#include "console.h"
#include "host_command.h"
#include "gpio.h"
#include "i2c.h"
#include "system.h"
#include "task.h"
#include "util.h"
#include "watchdog.h"

/* Delay for bitbanging i2c corresponds roughly to 100kHz. */
#define I2C_BITBANG_DELAY_US	5

/* Number of attempts to unwedge each pin. */
#define UNWEDGE_SCL_ATTEMPTS  10
#define UNWEDGE_SDA_ATTEMPTS  3

#define CPUTS(outstr) cputs(CC_I2C, outstr)
#define CPRINTS(format, args...) cprints(CC_I2C, format, ## args)

static struct mutex port_mutex[I2C_PORT_COUNT];

void i2c_lock(int port, int lock)
{
	if (lock) {
		/* Don't allow deep sleep when I2C port is locked */
		disable_sleep(SLEEP_MASK_I2C);

		mutex_lock(port_mutex + port);
	} else {
		mutex_unlock(port_mutex + port);

		/* Allow deep sleep again after I2C port is unlocked */
		enable_sleep(SLEEP_MASK_I2C);
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

int get_sda_from_i2c_port(int port, enum gpio_signal *sda)
{
	int i;

	/* Find the matching port in i2c_ports[] table. */
	for (i = 0; i < i2c_ports_used; i++) {
		if (i2c_ports[i].port == port)
			break;
	}

	/* Crash if the port given is not in the i2c_ports[] table. */
	ASSERT(i != i2c_ports_used);

	/* Check if the SCL and SDA pins have been defined for this port. */
	if (i2c_ports[i].scl == 0 && i2c_ports[i].sda == 0)
		return EC_ERROR_INVAL;

	*sda = i2c_ports[i].sda;
	return EC_SUCCESS;
}

int get_scl_from_i2c_port(int port, enum gpio_signal *scl)
{
	int i;

	/* Find the matching port in i2c_ports[] table. */
	for (i = 0; i < i2c_ports_used; i++) {
		if (i2c_ports[i].port == port)
			break;
	}

	/* Crash if the port given is not in the i2c_ports[] table. */
	ASSERT(i != i2c_ports_used);

	/* Check if the SCL and SDA pins have been defined for this port. */
	if (i2c_ports[i].scl == 0 && i2c_ports[i].sda == 0)
		return EC_ERROR_INVAL;

	*scl = i2c_ports[i].scl;
	return EC_SUCCESS;
}

void i2c_raw_set_scl(int port, int level)
{
	enum gpio_signal g;

	if (get_scl_from_i2c_port(port, &g) == EC_SUCCESS)
		gpio_set_level(g, level);
}

void i2c_raw_set_sda(int port, int level)
{
	enum gpio_signal g;

	if (get_sda_from_i2c_port(port, &g) == EC_SUCCESS)
		gpio_set_level(g, level);
}

int i2c_raw_mode(int port, int enable)
{
	enum gpio_signal sda, scl;
	static struct mutex raw_mode_mutex;

	/* Get the SDA and SCL pins for this port. If none, then return. */
	if (get_sda_from_i2c_port(port, &sda) != EC_SUCCESS)
		return EC_ERROR_INVAL;
	if (get_scl_from_i2c_port(port, &scl) != EC_SUCCESS)
		return EC_ERROR_INVAL;

	if (enable) {
		/*
		 * Lock access to raw mode functionality. Note, this is
		 * necessary because when we exit raw mode, we put all I2C
		 * ports into normal mode. This means that if another port
		 * is using the raw mode capabilities, that port will be
		 * re-configured from underneath it.
		 */
		mutex_lock(&raw_mode_mutex);

		/*
		 * To enable raw mode, take out of alternate function mode and
		 * set the flags to open drain output.
		 */
		gpio_set_alternate_function(gpio_list[sda].port,
						gpio_list[sda].mask, -1);
		gpio_set_alternate_function(gpio_list[scl].port,
						gpio_list[scl].mask, -1);

		gpio_set_flags(scl, GPIO_ODR_HIGH);
		gpio_set_flags(sda, GPIO_ODR_HIGH);
	} else {
		/*
		 * Note that this will return *all* I2C ports to normal mode.
		 * If two I2C ports are both in raw mode, whichever one
		 * finishes first will yank raw mode away from the other one.
		 */

		/* To disable raw mode, configure the I2C pins. */
		gpio_config_module(MODULE_I2C, 1);

		/* Unlock mutex, allow other I2C busses to use raw mode. */
		mutex_unlock(&raw_mode_mutex);
	}

	return EC_SUCCESS;
}


/*
 * Unwedge the i2c bus for the given port.
 *
 * Some devices on our i2c busses keep power even if we get a reset.  That
 * means that they could be part way through a transaction and could be
 * driving the bus in a way that makes it hard for us to talk on the bus.
 * ...or they might listen to the next transaction and interpret it in a
 * weird way.
 *
 * Note that devices could be in one of several states:
 * - If a device got interrupted in a write transaction it will be watching
 *   for additional data to finish its write.  It will probably be looking to
 *   ack the data (drive the data line low) after it gets everything.
 * - If a device got interrupted while responding to a register read, it will
 *   be watching for clocks and will drive data out when it sees clocks.  At
 *   the moment it might be trying to send out a 1 (so both clock and data
 *   may be high) or it might be trying to send out a 0 (so it's driving data
 *   low).
 *
 * We attempt to unwedge the bus by doing:
 * - If SCL is being held low, then a slave is clock extending. The only
 *   thing we can do is try to wait until the slave stops clock extending.
 * - Otherwise, we will toggle the clock until the slave releases the SDA line.
 *   Once the SDA line is released, try to send a STOP bit. Rinse and repeat
 *   until either the bus is normal, or we run out of attempts.
 *
 * Note this should work for most devices, but depending on the slaves i2c
 * state machine, it may not be possible to unwedge the bus.
 */
int i2c_unwedge(int port)
{
	int i, j;
	int ret = EC_SUCCESS;

	/* Try to put port in to raw bit bang mode. */
	if (i2c_raw_mode(port, 1) != EC_SUCCESS)
		return EC_ERROR_UNKNOWN;

	/*
	 * If clock is low, wait for a while in case of clock stretched
	 * by a slave.
	 */
	if (!i2c_raw_get_scl(port)) {
		for (i = 0; i < UNWEDGE_SCL_ATTEMPTS; i++) {
			udelay(I2C_BITBANG_DELAY_US);
			if (i2c_raw_get_scl(port))
				break;
		}

		/*
		 * If we get here, a slave is holding the clock low and there
		 * is nothing we can do.
		 */
		CPRINTS("I2C unwedge failed, SCL is being held low");
		ret = EC_ERROR_UNKNOWN;
		goto unwedge_done;
	}

	if (i2c_raw_get_sda(port))
		goto unwedge_done;

	CPRINTS("I2C unwedge called with SDA held low");

	/* Keep trying to unwedge the SDA line until we run out of attempts. */
	for (i = 0; i < UNWEDGE_SDA_ATTEMPTS; i++) {
		/* Drive the clock high. */
		i2c_raw_set_scl(port, 1);
		udelay(I2C_BITBANG_DELAY_US);

		/*
		 * Clock through the problem by clocking out 9 bits. If slave
		 * releases the SDA line, then we can stop clocking bits and
		 * send a STOP.
		 */
		for (j = 0; j < 9; j++) {
			if (i2c_raw_get_sda(port))
				break;

			i2c_raw_set_scl(port, 0);
			udelay(I2C_BITBANG_DELAY_US);
			i2c_raw_set_scl(port, 1);
			udelay(I2C_BITBANG_DELAY_US);
		}

		/* Take control of SDA line and issue a STOP command. */
		i2c_raw_set_sda(port, 0);
		udelay(I2C_BITBANG_DELAY_US);
		i2c_raw_set_sda(port, 1);
		udelay(I2C_BITBANG_DELAY_US);

		/* Check if the bus is unwedged. */
		if (i2c_raw_get_sda(port) && i2c_raw_get_scl(port))
			break;
	}

	if (!i2c_raw_get_sda(port)) {
		CPRINTS("I2C unwedge failed, SDA still low");
		ret = EC_ERROR_UNKNOWN;
	}
	if (!i2c_raw_get_scl(port)) {
		CPRINTS("I2C unwedge failed, SCL still low");
		ret = EC_ERROR_UNKNOWN;
	}

unwedge_done:
	/* Take port out of raw bit bang mode. */
	i2c_raw_mode(port, 0);

	return ret;
}

/*****************************************************************************/
/* Host commands */

/*
 * TODO(crosbug.com/p/23570): remove separate read and write commands, as soon
 * as ectool supports EC_CMD_I2C_PASSTHRU.
 */

static int port_is_valid(int port)
{
	int i;

	for (i = 0; i < i2c_ports_used; i++)
		if (i2c_ports[i].port == port)
			return 1;
	return 0;
}

static int i2c_command_read(struct host_cmd_handler_args *args)
{
	const struct ec_params_i2c_read *p = args->params;
	struct ec_response_i2c_read *r = args->response;
	int data, rv = -1;

#ifdef CONFIG_I2C_PASSTHRU_RESTRICTED
	if (system_is_locked())
		return EC_RES_ACCESS_DENIED;
#endif

	if (!port_is_valid(p->port))
		return EC_RES_INVALID_PARAM;

	if (p->read_size == 16)
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

	if (!port_is_valid(p->port))
		return EC_RES_INVALID_PARAM;

	if (p->write_size == 16)
		rv = i2c_write16(p->port, p->addr, p->offset, p->data);
	else if (p->write_size == 8)
		rv = i2c_write8(p->port, p->addr, p->offset, p->data);

	if (rv)
		return EC_RES_ERROR;

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_I2C_WRITE, i2c_command_write, EC_VER_MASK(0));

#ifdef CONFIG_I2C_DEBUG_PASSTHRU
#define PTHRUPRINTF(format, args...) CPRINTS(format, ## args)
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
		PTHRUPRINTF("i2c passthru no params, params_size=%d, "
			    "need at least %d",
			    args->params_size, sizeof(*params));
		return EC_RES_INVALID_PARAM;
	}
	size = sizeof(*params) + params->num_msgs * sizeof(*msg);
	if (args->params_size < size) {
		PTHRUPRINTF("i2c passthru params_size=%d, "
			    "need at least %d",
			    args->params_size, size);
		return EC_RES_INVALID_PARAM;
	}

	if (!port_is_valid(params->port)) {
		PTHRUPRINTF("i2c passthru invalid port %d",
			    params->port);
		return EC_RES_INVALID_PARAM;
	}

	/* Loop and process messages */;
	for (msgnum = 0, msg = params->msg; msgnum < params->num_msgs;
	     msgnum++, msg++) {
		unsigned int addr_flags = msg->addr_flags;

		PTHRUPRINTF("i2c passthru port=%d, %s, addr=0x%02x, "
			    "len=0x%02x",
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
		PTHRUPRINTF("i2c passthru overflow1");
		return EC_RES_INVALID_PARAM;
	}

	/* Must have bytes to write */
	if (args->params_size < size + write_len) {
		PTHRUPRINTF("i2c passthru overflow2");
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

#ifdef CONFIG_BATTERY_CUT_OFF
	/*
	 * Some batteries would wake up after cut-off if we talk to it.
	 */
	if (battery_is_cut_off())
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
		PTHRUPRINTF("i2c passthru xfer port=%x, addr=%x, out=%p, "
			    "write_len=%x, data=%p, read_len=%x, flags=%x",
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

#ifdef CONFIG_CMD_I2C_SCAN
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

#ifdef CHIP_FAMILY_STM32F
		/*
		 * TODO(crosbug.com/p/23569): The i2c_xfer() implementation on
		 * STM32F can't read a byte without writing one first.  So
		 * write a byte and hope nothing bad happens.  Remove this
		 * workaround when STM32F is fixed.
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

	for (i = 0; i < i2c_ports_used; i++)
		scan_bus(i2c_ports[i].port, i2c_ports[i].name);
	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(i2cscan, command_scan,
			NULL,
			"Scan I2C ports for devices",
			NULL);
#endif

#ifdef CONFIG_CMD_I2C_XFER
static int command_i2cxfer(int argc, char **argv)
{
	int port, slave_addr;
	uint8_t offset;
	int v = 0;
	uint8_t data[32];
	char *e;
	int rv = 0;

	if (argc < 5)
		return EC_ERROR_PARAM_COUNT;

	port = strtoi(argv[2], &e, 0);
	if (*e)
		return EC_ERROR_PARAM2;

	slave_addr = strtoi(argv[3], &e, 0);
	if (*e)
		return EC_ERROR_PARAM3;

	offset = strtoi(argv[4], &e, 0);
	if (*e)
		return EC_ERROR_PARAM4;

	if (argc >= 6) {
		v = strtoi(argv[5], &e, 0);
		if (*e)
			return EC_ERROR_PARAM5;
	}

	if (strcasecmp(argv[1], "r") == 0) {
		/* 8-bit read */
		rv = i2c_read8(port, slave_addr, offset, &v);
		if (!rv)
			ccprintf("0x%02x [%d]\n", v);

	} else if (strcasecmp(argv[1], "r16") == 0) {
		/* 16-bit read */
		rv = i2c_read16(port, slave_addr, offset, &v);
		if (!rv)
			ccprintf("0x%04x [%d]\n", v);

	} else if (strcasecmp(argv[1], "rlen") == 0) {
		/* Arbitrary length read; param5 = len */
		if (argc < 6 || v < 0 || v > sizeof(data))
			return EC_ERROR_PARAM5;

		i2c_lock(port, 1);
		rv = i2c_xfer(port, slave_addr,
			      &offset, 1, data, v, I2C_XFER_SINGLE);
		i2c_lock(port, 0);

		if (!rv)
			ccprintf("Data: %.*h\n", v, data);

	} else if (strcasecmp(argv[1], "w") == 0) {
		/* 8-bit write */
		if (argc < 6)
			return EC_ERROR_PARAM5;

		rv = i2c_write8(port, slave_addr, offset, v);

	} else if (strcasecmp(argv[1], "w16") == 0) {
		/* 16-bit write */
		if (argc < 6)
			return EC_ERROR_PARAM5;

		rv = i2c_write16(port, slave_addr, offset, v);

	} else {
		return EC_ERROR_PARAM1;
	}

	return rv;
}
DECLARE_CONSOLE_COMMAND(i2cxfer, command_i2cxfer,
			"r/r16/rlen/w/w16 port addr offset [value | len]",
			"Read write I2C",
			NULL);
#endif
