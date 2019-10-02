/* Copyright 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* I2C cross-platform code for Chrome EC */

#include "battery.h"
#include "clock.h"
#include "charge_state.h"
#include "console.h"
#include "host_command.h"
#include "gpio.h"
#include "i2c.h"
#include "system.h"
#include "task.h"
#include "usb_pd_tcpm.h"
#include "util.h"
#include "watchdog.h"
#include "virtual_battery.h"

/* Delay for bitbanging i2c corresponds roughly to 100kHz. */
#define I2C_BITBANG_DELAY_US	5

/* Number of attempts to unwedge each pin. */
#define UNWEDGE_SCL_ATTEMPTS  10
#define UNWEDGE_SDA_ATTEMPTS  3

#define CPUTS(outstr) cputs(CC_I2C, outstr)
#define CPRINTS(format, args...) cprints(CC_I2C, format, ## args)
#define CPRINTF(format, args...) cprintf(CC_I2C, format, ## args)

/* Only chips with multi-port controllers will define I2C_CONTROLLER_COUNT */
#ifndef I2C_CONTROLLER_COUNT
#define I2C_CONTROLLER_COUNT I2C_PORT_COUNT
#endif

static struct mutex port_mutex[I2C_CONTROLLER_COUNT];
/* A bitmap of the controllers which are currently servicing a request. */
static uint32_t i2c_port_active_list;
BUILD_ASSERT(I2C_CONTROLLER_COUNT < 32);
static uint8_t port_protected[I2C_PORT_COUNT];

/**
 * Non-deterministically test the lock status of the port.  If another task
 * has locked the port and the caller is accessing it illegally, then this test
 * will incorrectly return true.  However, callers which failed to statically
 * lock the port will fail quickly.
 */
static int i2c_port_is_locked(int port)
{
#ifdef CONFIG_I2C_MULTI_PORT_CONTROLLER
	/* Test the controller, not the port */
	port = i2c_port_to_controller(port);
#endif
	/* can't lock a non-existing port */
	if (port < 0)
		return 0;

	return (i2c_port_active_list >> port) & 1;
}


const struct i2c_port_t *get_i2c_port(const int port)
{
	int i;

	/* Find the matching port in i2c_ports[] table. */
	for (i = 0; i < i2c_ports_used; i++) {
		if (i2c_ports[i].port == port)
			return &i2c_ports[i];
	}

	return NULL;
}

static int chip_i2c_xfer_with_notify(const int port,
				     const uint16_t slave_addr_flags,
				     const uint8_t *out, int out_size,
				     uint8_t *in, int in_size, int flags)
{
	int ret;

	if (IS_ENABLED(CONFIG_I2C_DEBUG))
		i2c_trace_notify(port, slave_addr_flags, 0, out, out_size);

	if (IS_ENABLED(CONFIG_I2C_XFER_BOARD_CALLBACK))
		i2c_start_xfer_notify(port, slave_addr_flags);

	ret = chip_i2c_xfer(port, slave_addr_flags,
			    out, out_size, in, in_size, flags);

	if (IS_ENABLED(CONFIG_I2C_XFER_BOARD_CALLBACK))
		i2c_end_xfer_notify(port, slave_addr_flags);

	if (IS_ENABLED(CONFIG_I2C_DEBUG))
		i2c_trace_notify(port, slave_addr_flags, 1, in, in_size);

	return ret;
}

#ifdef CONFIG_I2C_XFER_LARGE_READ
/*
 * Internal function that splits reading into multiple chip_i2c_xfer() calls
 * if in_size exceeds CONFIG_I2C_CHIP_MAX_READ_SIZE.
 */
static int i2c_xfer_no_retry(const int port,
			     const uint16_t slave_addr_flags,
			     const uint8_t *out, int out_size,
			     uint8_t *in, int in_size, int flags)
{
	int ret;
	int out_flags = flags & I2C_XFER_START;
	int in_chunk_size = MIN(in_size, CONFIG_I2C_CHIP_MAX_READ_SIZE);

	in_size -= in_chunk_size;
	out_flags |= !in_size ? (flags & I2C_XFER_STOP) : 0;
	ret = chip_i2c_xfer_with_notify(port, slave_addr_flags,
					out, out_size, in,
					in_chunk_size, out_flags);
	in += in_chunk_size;
	while (in_size && ret == EC_SUCCESS) {
		in_chunk_size = MIN(in_size, CONFIG_I2C_CHIP_MAX_READ_SIZE);
		in_size -= in_chunk_size;
		ret = chip_i2c_xfer_with_notify(port, slave_addr_flags,
			NULL, 0, in,
			in_chunk_size, !in_size ? (flags & I2C_XFER_STOP) : 0);
		in += in_chunk_size;
	}
	return ret;
}
#endif /* CONFIG_I2C_XFER_LARGE_READ */

int i2c_xfer_unlocked(const int port,
		      const uint16_t slave_addr_flags,
		      const uint8_t *out, int out_size,
		      uint8_t *in, int in_size, int flags)
{
	int i;
	int ret = EC_SUCCESS;

	if (!i2c_port_is_locked(port)) {
		CPUTS("Access I2C without lock!");
		return EC_ERROR_INVAL;
	}

	for (i = 0; i <= CONFIG_I2C_NACK_RETRY_COUNT; i++) {
#ifdef CONFIG_I2C_XFER_LARGE_READ
		ret = i2c_xfer_no_retry(port, slave_addr_flags,
					    out, out_size, in,
					    in_size, flags);
#else
		ret = chip_i2c_xfer_with_notify(port, slave_addr_flags,
						    out, out_size,
						    in, in_size, flags);
#endif /* CONFIG_I2C_XFER_LARGE_READ */
		if (ret != EC_ERROR_BUSY)
			break;
	}
	return ret;
}

int i2c_xfer(const int port,
	     const uint16_t slave_addr_flags,
	     const uint8_t *out, int out_size,
	     uint8_t *in, int in_size)
{
	int rv;

	i2c_lock(port, 1);
	rv = i2c_xfer_unlocked(port, slave_addr_flags,
			       out, out_size, in, in_size,
			       I2C_XFER_SINGLE);
	i2c_lock(port, 0);

	return rv;
}

void i2c_lock(int port, int lock)
{
#ifdef CONFIG_I2C_MULTI_PORT_CONTROLLER
	/* Lock the controller, not the port */
	port = i2c_port_to_controller(port);
#endif
	if (port < 0)
		return;

	if (lock) {
		mutex_lock(port_mutex + port);

		/* Disable interrupt during changing counter for preemption. */
		interrupt_disable();

		i2c_port_active_list |= 1 << port;
		/* Ec cannot enter sleep if there's any i2c port active. */
		disable_sleep(SLEEP_MASK_I2C_MASTER);

		interrupt_enable();
	} else {
		interrupt_disable();

		i2c_port_active_list &= ~BIT(port);
		/* Once there is no i2c port active, enable sleep bit of i2c. */
		if (!i2c_port_active_list)
			enable_sleep(SLEEP_MASK_I2C_MASTER);

		interrupt_enable();

		mutex_unlock(port_mutex + port);
	}
}

void i2c_prepare_sysjump(void)
{
	int i;

	/* Lock all i2c controllers */
	for (i = 0; i < I2C_CONTROLLER_COUNT; ++i)
		mutex_lock(port_mutex + i);
}

int i2c_read32(const int port,
	       const uint16_t slave_addr_flags,
	       int offset, int *data)
{
	int rv;
	uint8_t reg, buf[sizeof(uint32_t)];

	reg = offset & 0xff;
	/* I2C read 32-bit word: transmit 8-bit offset, and read 32bits */
	rv = i2c_xfer(port, slave_addr_flags,
		      &reg, 1, buf, sizeof(uint32_t));

	if (rv)
		return rv;

	if (I2C_IS_BIG_ENDIAN(slave_addr_flags))
		*data = ((int)buf[0] << 24) | ((int)buf[1] << 16) |
			((int)buf[0] << 8) | buf[1];
	else
		*data = ((int)buf[3] << 24) | ((int)buf[2] << 16) |
			((int)buf[1] << 8) | buf[0];

	return EC_SUCCESS;
}

int i2c_write32(const int port,
		const uint16_t slave_addr_flags,
		int offset, int data)
{
	uint8_t buf[1 + sizeof(uint32_t)];

	buf[0] = offset & 0xff;

	if (I2C_IS_BIG_ENDIAN(slave_addr_flags)) {
		buf[1] = (data >> 24) & 0xff;
		buf[2] = (data >> 16) & 0xff;
		buf[3] = (data >> 8) & 0xff;
		buf[4] = data & 0xff;
	} else {
		buf[1] = data & 0xff;
		buf[2] = (data >> 8) & 0xff;
		buf[3] = (data >> 16) & 0xff;
		buf[4] = (data >> 24) & 0xff;
	}

	return i2c_xfer(port, slave_addr_flags,
			buf, sizeof(uint32_t) + 1, NULL, 0);
}

int i2c_read16(const int port,
	       const uint16_t slave_addr_flags,
	       int offset, int *data)
{
	int rv;
	uint8_t reg, buf[sizeof(uint16_t)];

	reg = offset & 0xff;
	/* I2C read 16-bit word: transmit 8-bit offset, and read 16bits */
	rv = i2c_xfer(port, slave_addr_flags,
		      &reg, 1, buf, sizeof(uint16_t));

	if (rv)
		return rv;

	if (I2C_IS_BIG_ENDIAN(slave_addr_flags))
		*data = ((int)buf[0] << 8) | buf[1];
	else
		*data = ((int)buf[1] << 8) | buf[0];

	return EC_SUCCESS;
}

int i2c_write16(const int port,
		const uint16_t slave_addr_flags,
		int offset, int data)
{
	uint8_t buf[1 + sizeof(uint16_t)];

	buf[0] = offset & 0xff;

	if (I2C_IS_BIG_ENDIAN(slave_addr_flags)) {
		buf[1] = (data >> 8) & 0xff;
		buf[2] = data & 0xff;
	} else {
		buf[1] = data & 0xff;
		buf[2] = (data >> 8) & 0xff;
	}

	return i2c_xfer(port, slave_addr_flags,
			buf, 1 + sizeof(uint16_t), NULL, 0);
}

int i2c_read8(const int port,
	      const uint16_t slave_addr_flags,
	      int offset, int *data)
{
	int rv;
	uint8_t reg = offset;
	uint8_t buf;

	reg = offset;

	rv = i2c_xfer(port, slave_addr_flags, &reg, 1, &buf, 1);
	if (!rv)
		*data = buf;

	return rv;
}

int i2c_write8(const int port,
	       const uint16_t slave_addr_flags,
	       int offset, int data)
{
	uint8_t buf[2];

	buf[0] = offset;
	buf[1] = data;

	return i2c_xfer(port, slave_addr_flags, buf, 2, 0, 0);
}

int i2c_read_offset16(const int port,
		      const uint16_t slave_addr_flags,
		      uint16_t offset, int *data, int len)
{
	int rv;
	uint8_t buf[sizeof(uint16_t)], addr[sizeof(uint16_t)];

	if (len < 0 || len > 2)
		return EC_ERROR_INVAL;

	addr[0] = (offset >> 8) & 0xff;
	addr[1] = offset & 0xff;

	/* I2C read 16-bit word: transmit 16-bit offset, and read buffer */
	rv = i2c_xfer(port, slave_addr_flags, addr, 2, buf, len);

	if (rv)
		return rv;

	if (len == 1) {
		*data = buf[0];
	} else {
		if (I2C_IS_BIG_ENDIAN(slave_addr_flags))
			*data = ((int)buf[0] << 8) | buf[1];
		else
			*data = ((int)buf[1] << 8) | buf[0];
	}

	return EC_SUCCESS;
}

int i2c_write_offset16(const int port,
		       const uint16_t slave_addr_flags,
		       uint16_t offset, int data, int len)
{
	uint8_t buf[2 + sizeof(uint16_t)];

	if (len < 0 || len > 2)
		return EC_ERROR_INVAL;

	buf[0] = (offset >> 8) & 0xff;
	buf[1] = offset & 0xff;

	if (len == 1) {
		buf[2] = data & 0xff;
	} else {
		if (I2C_IS_BIG_ENDIAN(slave_addr_flags)) {
			buf[2] = (data >> 8) & 0xff;
			buf[3] = data & 0xff;
		} else {
			buf[2] = data & 0xff;
			buf[3] = (data >> 8) & 0xff;
		}
	}

	return i2c_xfer(port, slave_addr_flags, buf, 2 + len, NULL, 0);
}

int i2c_read_offset16_block(const int port,
			    const uint16_t slave_addr_flags,
			    uint16_t offset, uint8_t *data, int len)
{
	uint8_t addr[sizeof(uint16_t)];

	addr[0] = (offset >> 8) & 0xff;
	addr[1] = offset & 0xff;

	return i2c_xfer(port, slave_addr_flags, addr, 2, data, len);
}

int i2c_write_offset16_block(const int port,
			     const uint16_t slave_addr_flags,
			     uint16_t offset, const uint8_t *data, int len)
{
	int rv;
	uint8_t addr[sizeof(uint16_t)];

	addr[0] = (offset >> 8) & 0xff;
	addr[1] = offset & 0xff;

	/*
	 * Split into two transactions to avoid the stack space consumption of
	 * appending the destination address with the data array.
	 */
	i2c_lock(port, 1);
	rv = i2c_xfer_unlocked(port, slave_addr_flags, addr, 2, NULL, 0,
			       I2C_XFER_START);
	if (!rv)
		rv = i2c_xfer_unlocked(port, slave_addr_flags,
				       data, len, NULL, 0, I2C_XFER_STOP);
	i2c_lock(port, 0);

	return rv;
}

int i2c_read_string(const int port,
		    const uint16_t slave_addr_flags,
		    int offset, uint8_t *data, int len)
{
	int rv;
	uint8_t reg, block_length;

	i2c_lock(port, 1);

	reg = offset;
	/*
	 * Send device reg space offset, and read back block length.  Keep this
	 * session open without a stop.
	 */
	rv = i2c_xfer_unlocked(port, slave_addr_flags,
			       &reg, 1, &block_length, 1, I2C_XFER_START);
	if (rv)
		goto exit;

	if (len && block_length > (len - 1))
		block_length = len - 1;

	rv = i2c_xfer_unlocked(port, slave_addr_flags,
			       0, 0, data, block_length, I2C_XFER_STOP);
	data[block_length] = 0;

exit:
	i2c_lock(port, 0);
	return rv;
}

int i2c_read_block(const int port,
		   const uint16_t slave_addr_flags,
		   int offset, uint8_t *data, int len)
{
	int rv;
	uint8_t reg_address = offset;

	rv = i2c_xfer(port, slave_addr_flags, &reg_address, 1, data, len);
	return rv;
}

int i2c_write_block(const int port,
		    const uint16_t slave_addr_flags,
		    int offset, const uint8_t *data, int len)
{
	int rv;
	uint8_t reg_address = offset;

	/*
	 * Split into two transactions to avoid the stack space consumption of
	 * appending the destination address with the data array.
	 */
	i2c_lock(port, 1);
	rv = i2c_xfer_unlocked(port, slave_addr_flags,
			       &reg_address, 1, NULL, 0, I2C_XFER_START);
	if (!rv) {
		rv = i2c_xfer_unlocked(port, slave_addr_flags,
				       data, len, NULL, 0, I2C_XFER_STOP);
	}
	i2c_lock(port, 0);

	return rv;
}

int get_sda_from_i2c_port(int port, enum gpio_signal *sda)
{
	const struct i2c_port_t *i2c_port = get_i2c_port(port);

	/* Crash if the port given is not in the i2c_ports[] table. */
	ASSERT(i2c_port);

	/* Check if the SCL and SDA pins have been defined for this port. */
	if (i2c_port->scl == 0 && i2c_port->sda == 0)
		return EC_ERROR_INVAL;

	*sda = i2c_port->sda;
	return EC_SUCCESS;
}

int get_scl_from_i2c_port(int port, enum gpio_signal *scl)
{
	const struct i2c_port_t *i2c_port = get_i2c_port(port);

	/* Crash if the port given is not in the i2c_ports[] table. */
	ASSERT(i2c_port);

	/* Check if the SCL and SDA pins have been defined for this port. */
	if (i2c_port->scl == 0 && i2c_port->sda == 0)
		return EC_ERROR_INVAL;

	*scl = i2c_port->scl;
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
	int ret_sda, ret_scl;

	/* Get the SDA and SCL pins for this port. If none, then return. */
	if (get_sda_from_i2c_port(port, &sda) != EC_SUCCESS)
		return EC_ERROR_INVAL;
	if (get_scl_from_i2c_port(port, &scl) != EC_SUCCESS)
		return EC_ERROR_INVAL;

	if (enable) {
		int raw_gpio_mode_flags = GPIO_ODR_HIGH;

		/* If the CLK line is 1.8V, then ensure we set 1.8V mode */
		if ((gpio_list + scl)->flags & GPIO_SEL_1P8V)
			raw_gpio_mode_flags |= GPIO_SEL_1P8V;

		/*
		 * To enable raw mode, take out of alternate function mode and
		 * set the flags to open drain output.
		 */
		ret_sda = gpio_config_pin(MODULE_I2C, sda, 0);
		ret_scl = gpio_config_pin(MODULE_I2C, scl, 0);

		gpio_set_flags(scl, raw_gpio_mode_flags);
		gpio_set_flags(sda, raw_gpio_mode_flags);
	} else {
		/*
		 * Configure the I2C pins to exit raw mode and return
		 * to normal mode.
		 */
		ret_sda = gpio_config_pin(MODULE_I2C, sda, 1);
		ret_scl = gpio_config_pin(MODULE_I2C, scl, 1);
	}

	return ret_sda == EC_SUCCESS ? ret_scl : ret_sda;
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

#ifdef CONFIG_I2C_BUS_MAY_BE_UNPOWERED
	/*
	 * Don't try to unwedge the port if we know it's unpowered; it's futile.
	 */
	if (!board_is_i2c_port_powered(port)) {
		CPRINTS("Skipping i2c unwedge, bus not powered.");
		return EC_ERROR_NOT_POWERED;
	}
#endif /* CONFIG_I2C_BUS_MAY_BE_UNPOWERED */

	/* Try to put port in to raw bit bang mode. */
	if (i2c_raw_mode(port, 1) != EC_SUCCESS)
		return EC_ERROR_UNKNOWN;

	/*
	 * If clock is low, wait for a while in case of clock stretched
	 * by a slave.
	 */
	if (!i2c_raw_get_scl(port)) {
		for (i = 0;; i++) {
			if (i >= UNWEDGE_SCL_ATTEMPTS) {
				/*
				 * If we get here, a slave is holding the clock
				 * low and there is nothing we can do.
				 */
				CPRINTS("I2C%d unwedge failed, "
					"SCL is held low", port);
				ret = EC_ERROR_UNKNOWN;
				goto unwedge_done;
			}
			udelay(I2C_BITBANG_DELAY_US);
			if (i2c_raw_get_scl(port))
				break;
		}
	}

	if (i2c_raw_get_sda(port))
		goto unwedge_done;

	CPRINTS("I2C%d unwedge called with SDA held low", port);

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
		CPRINTS("I2C%d unwedge failed, SDA still low", port);
		ret = EC_ERROR_UNKNOWN;
	}
	if (!i2c_raw_get_scl(port)) {
		CPRINTS("I2C%d unwedge failed, SCL still low", port);
		ret = EC_ERROR_UNKNOWN;
	}

unwedge_done:
	/* Take port out of raw bit bang mode. */
	i2c_raw_mode(port, 0);

	return ret;
}

/*****************************************************************************/
/* Host commands */

#ifdef CONFIG_I2C_DEBUG_PASSTHRU
#define PTHRUPRINTS(format, args...) CPRINTS("I2C_PTHRU " format, ## args)
#define PTHRUPRINTF(format, args...) CPRINTF(format, ## args)
#else
#define PTHRUPRINTS(format, args...)
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
		PTHRUPRINTS("no params, params_size=%d, need at least %d",
			    args->params_size, sizeof(*params));
		return EC_RES_INVALID_PARAM;
	}
	size = sizeof(*params) + params->num_msgs * sizeof(*msg);
	if (args->params_size < size) {
		PTHRUPRINTS("params_size=%d, need at least %d",
			    args->params_size, size);
		return EC_RES_INVALID_PARAM;
	}

	/* Loop and process messages */;
	for (msgnum = 0, msg = params->msg; msgnum < params->num_msgs;
	     msgnum++, msg++) {
		unsigned int addr_flags = msg->addr_flags;

		PTHRUPRINTS("port=%d, %s, addr=0x%x(7-bit), len=%d",
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
		PTHRUPRINTS("overflow1");
		return EC_RES_INVALID_PARAM;
	}

	/* Must have bytes to write */
	if (args->params_size < size + write_len) {
		PTHRUPRINTS("overflow2");
		return EC_RES_INVALID_PARAM;
	}

	return EC_RES_SUCCESS;
}

static enum ec_status i2c_command_passthru(struct host_cmd_handler_args *args)
{
	const struct ec_params_i2c_passthru *params = args->params;
	const struct ec_params_i2c_passthru_msg *msg;
	struct ec_response_i2c_passthru *resp = args->response;
	const struct i2c_port_t *i2c_port;
	const uint8_t *out;
	int in_len;
	int ret, i;
	int port_is_locked = 0;

#ifdef CONFIG_BATTERY_CUT_OFF
	/*
	 * Some batteries would wake up after cut-off if we talk to it.
	 */
	if (battery_is_cut_off())
		return EC_RES_ACCESS_DENIED;
#endif

	i2c_port = get_i2c_port(params->port);
	if (!i2c_port)
		return EC_RES_INVALID_PARAM;

	ret = check_i2c_params(args);
	if (ret)
		return ret;

	if (port_protected[params->port] && i2c_port->passthru_allowed) {
		for (i = 0; i < params->num_msgs; i++) {
			if (!i2c_port->passthru_allowed(i2c_port,
					params->msg[i].addr_flags))
				return EC_RES_ACCESS_DENIED;
		}
	}

	/* Loop and process messages */
	resp->i2c_status = 0;
	out = args->params + sizeof(*params) + params->num_msgs * sizeof(*msg);
	in_len = 0;

	for (resp->num_msgs = 0, msg = params->msg;
	     resp->num_msgs < params->num_msgs;
	     resp->num_msgs++, msg++) {
		int xferflags = I2C_XFER_START;
		int read_len = 0, write_len = 0;
		int rv = 1;

		/* Have to remove the EC flags from the address flags */
		uint16_t addr_flags = msg->addr_flags & EC_I2C_ADDR_MASK;


		if (msg->addr_flags & EC_I2C_FLAG_READ)
			read_len = msg->len;
		else
			write_len = msg->len;

		/* Set stop bit for last message */
		if (resp->num_msgs == params->num_msgs - 1)
			xferflags |= I2C_XFER_STOP;

#if defined(VIRTUAL_BATTERY_ADDR_FLAGS) && defined(I2C_PORT_VIRTUAL_BATTERY)
		if (params->port == I2C_PORT_VIRTUAL_BATTERY &&
		    addr_flags == VIRTUAL_BATTERY_ADDR_FLAGS) {
			if (virtual_battery_handler(resp, in_len, &rv,
						xferflags, read_len,
						write_len, out))
				break;
		}
#endif
		/* Transfer next message */
		PTHRUPRINTS("xfer port=%x addr=0x%x rlen=%d flags=0x%x",
			    params->port, addr_flags,
			    read_len, xferflags);
		if (write_len) {
			PTHRUPRINTF("  out:");
			for (i = 0; i < write_len; i++)
				PTHRUPRINTF(" 0x%02x", out[i]);
			PTHRUPRINTF("\n");
		}
		if (rv) {
#ifdef CONFIG_I2C_PASSTHRU_RESTRICTED
			if (system_is_locked() &&
			    !board_allow_i2c_passthru(params->port)) {
				if (port_is_locked)
					i2c_lock(params->port, 0);
				return EC_RES_ACCESS_DENIED;
			}
#endif
			if (!port_is_locked)
				i2c_lock(params->port, (port_is_locked = 1));
			rv = i2c_xfer_unlocked(params->port,
					       addr_flags,
					       out, write_len,
					       &resp->data[in_len], read_len,
					       xferflags);
		}

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
	if (port_is_locked)
		i2c_lock(params->port, 0);

	/*
	 * Return success even if transfer failed so response is sent.  Host
	 * will check message status to determine the transfer result.
	 */
	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_I2C_PASSTHRU, i2c_command_passthru, EC_VER_MASK(0));

static void i2c_passthru_protect_port(uint32_t port)
{
	if (port < I2C_PORT_COUNT)
		port_protected[port] = 1;
	else
		PTHRUPRINTS("Invalid I2C port %d to be protected\n", port);
}

static void i2c_passthru_protect_tcpc_ports(void)
{
#ifdef CONFIG_USB_PD_PORT_MAX_COUNT
	int i;

	/*
	 * If WP is not enabled i.e. system is not locked leave the tunnels open
	 * so that factory line can do updates without a new RO BIOS.
	 */
	if (!system_is_locked()) {
		CPRINTS("System unlocked, TCPC I2C tunnels may be unprotected");
		return;
	}

	for (i = 0; i < CONFIG_USB_PD_PORT_MAX_COUNT; i++) {
		/* TCPC tunnel not configured. No need to protect anything */
		if (!I2C_GET_ADDR(tcpc_config[i].i2c_info.addr_flags))
			continue;
		i2c_passthru_protect_port(tcpc_config[i].i2c_info.port);
	}
#endif
}

static enum ec_status
i2c_command_passthru_protect(struct host_cmd_handler_args *args)
{
	const struct ec_params_i2c_passthru_protect *params = args->params;
	struct ec_response_i2c_passthru_protect *resp = args->response;

	if (args->params_size < sizeof(*params)) {
		PTHRUPRINTS("protect no params, params_size=%d, ",
			    args->params_size);
		return EC_RES_INVALID_PARAM;
	}

	if (!get_i2c_port(params->port)) {
		PTHRUPRINTS("protect invalid port %d", params->port);
		return EC_RES_INVALID_PARAM;
	}

	if (params->subcmd == EC_CMD_I2C_PASSTHRU_PROTECT_STATUS) {
		if (args->response_max < sizeof(*resp)) {
			PTHRUPRINTS("protect no response, "
					"response_max=%d, need at least %d",
					args->response_max, sizeof(*resp));
			return EC_RES_INVALID_PARAM;
		}

		resp->status = port_protected[params->port];
		args->response_size = sizeof(*resp);
	} else if (params->subcmd == EC_CMD_I2C_PASSTHRU_PROTECT_ENABLE) {
		i2c_passthru_protect_port(params->port);
	} else if (params->subcmd == EC_CMD_I2C_PASSTHRU_PROTECT_ENABLE_TCPCS) {
		if (IS_ENABLED(CONFIG_USB_POWER_DELIVERY) &&
				!IS_ENABLED(CONFIG_USB_PD_TCPM_STUB))
			i2c_passthru_protect_tcpc_ports();
	} else {
		return EC_RES_INVALID_COMMAND;
	}

	return EC_RES_SUCCESS;
}
DECLARE_HOST_COMMAND(EC_CMD_I2C_PASSTHRU_PROTECT, i2c_command_passthru_protect,
		     EC_VER_MASK(0));

/*****************************************************************************/
/* Console commands */

#ifdef CONFIG_CMD_I2C_PROTECT
static int command_i2cprotect(int argc, char **argv)
{
	if (argc == 1) {
		int i, port;

		for (i = 0; i < i2c_ports_used; i++) {
			port = i2c_ports[i].port;
			ccprintf("Port %d: %s\n", port,
			   port_protected[port] ? "Protected" : "Unprotected");
		}
	} else if (argc == 2) {
		int port;
		char *e;

		port = strtoi(argv[1], &e, 0);
		if (*e)
			return EC_ERROR_PARAM2;

		if (!get_i2c_port(port)) {
			ccprintf("i2c passthru protect invalid port %d\n",
				port);
			return EC_RES_INVALID_PARAM;
		}

		port_protected[port] = 1;
	} else {
		return EC_ERROR_PARAM_COUNT;
	}

	return EC_RES_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(i2cprotect, command_i2cprotect,
			"[port]",
			"Protect I2C bus");
#endif

#ifdef CONFIG_CMD_I2C_SCAN
static void scan_bus(int port, const char *desc)
{
	int level;
	uint8_t tmp;
	uint16_t addr_flags;

	ccprintf("Scanning %d %s", port, desc);

	i2c_lock(port, 1);

	/* Don't scan a busy port, since reads will just fail / time out */
	level = i2c_get_line_levels(port);
	if (level != I2C_LINE_IDLE) {
		ccprintf(": port busy (SDA=%d, SCL=%d)",
			 (level & I2C_LINE_SDA_HIGH) ? 1 : 0,
			 (level & I2C_LINE_SCL_HIGH) ? 1 : 0);
		goto scan_bus_exit;
	}
	/*
	 * Only scan in the valid client device address range, otherwise some
	 * client devices stretch the clock in weird ways that prevent the
	 * discovery of other devices.
	 */
	for (addr_flags = I2C_FIRST_VALID_ADDR;
	     addr_flags <= I2C_LAST_VALID_ADDR; ++addr_flags) {
		watchdog_reload();  /* Otherwise a full scan trips watchdog */
		ccputs(".");

		/* Do a single read */
		if (!i2c_xfer_unlocked(port, addr_flags,
				       NULL, 0, &tmp, 1, I2C_XFER_SINGLE))
			ccprintf("\n  0x%02x", addr_flags);
	}

scan_bus_exit:
	i2c_lock(port, 0);
	ccputs("\n");
}

static int command_scan(int argc, char **argv)
{
	int port;
	char *e;

	if (argc == 1) {
		for (port = 0; port < i2c_ports_used; port++)
			scan_bus(i2c_ports[port].port, i2c_ports[port].name);
		return EC_SUCCESS;
	}


	port = strtoi(argv[1], &e, 0);
	if ((*e) || (port >= i2c_ports_used))
		return EC_ERROR_PARAM2;

	scan_bus(i2c_ports[port].port, i2c_ports[port].name);
	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(i2cscan, command_scan,
			"i2cscan [port]",
			"Scan I2C ports for devices");
#endif

#ifdef CONFIG_CMD_I2C_XFER
static int command_i2cxfer(int argc, char **argv)
{
	int port;
	uint16_t addr_flags;
	uint16_t offset = 0;
	uint8_t offset_size = 0;
	int v = 0;
	uint8_t data[32];
	char *e;
	int rv = 0;

	if (argc < 5)
		return EC_ERROR_PARAM_COUNT;

	port = strtoi(argv[2], &e, 0);
	if (*e)
		return EC_ERROR_PARAM2;

	addr_flags = strtoi(argv[3], &e, 0);
	if (*e)
		return EC_ERROR_PARAM3;

	offset = strtoi(argv[4], &e, 0);
	if (*e)
		return EC_ERROR_PARAM4;

	offset_size = (strlen(argv[4]) == 6) ? 2 : 1;

	if (argc >= 6) {
		v = strtoi(argv[5], &e, 0);
		if (*e)
			return EC_ERROR_PARAM5;
	}

	if (strcasecmp(argv[1], "r") == 0) {
		/* 8-bit read */
		if (offset_size == 2)
			rv = i2c_read_offset16(port, addr_flags,
					       offset, &v, 1);
		else
			rv = i2c_read8(port, addr_flags,
				       offset, &v);
		if (!rv)
			ccprintf("0x%02x [%d]\n", v, v);

	} else if (strcasecmp(argv[1], "r16") == 0) {
		/* 16-bit read */
		if (offset_size == 2)
			rv = i2c_read_offset16(port, addr_flags,
					       offset, &v, 2);
		else
			rv = i2c_read16(port, addr_flags,
					offset, &v);
		if (!rv)
			ccprintf("0x%04x [%d]\n", v, v);

	} else if (strcasecmp(argv[1], "rlen") == 0) {
		/* Arbitrary length read; param5 = len */
		if (argc < 6 || v < 0 || v > sizeof(data))
			return EC_ERROR_PARAM5;

		rv = i2c_xfer(port, addr_flags,
			      (uint8_t *)&offset, 1, data, v);

		if (!rv)
			ccprintf("Data: %ph\n", HEX_BUF(data, v));

	} else if (strcasecmp(argv[1], "w") == 0) {
		/* 8-bit write */
		if (argc < 6)
			return EC_ERROR_PARAM5;
		if (offset_size == 2)
			rv = i2c_write_offset16(port, addr_flags,
						offset, v, 1);
		else
			rv = i2c_write8(port, addr_flags,
					offset, v);

	} else if (strcasecmp(argv[1], "w16") == 0) {
		/* 16-bit write */
		if (argc < 6)
			return EC_ERROR_PARAM5;
		if (offset_size == 2)
			rv = i2c_write_offset16(port, addr_flags,
						offset, v, 2);
		else
			rv = i2c_write16(port, addr_flags,
					 offset, v);

	} else {
		return EC_ERROR_PARAM1;
	}

	return rv;
}
DECLARE_CONSOLE_COMMAND(i2cxfer, command_i2cxfer,
			"r/r16/rlen/w/w16 port addr offset [value | len]",
			"Read write I2C");
#endif

#ifdef CONFIG_CMD_I2C_STRESS_TEST
static void i2c_test_status(struct i2c_test_results *i2c_test, int test_dev)
{
	ccprintf("test_dev=%2d, ", test_dev);
	ccprintf("r=%5d, rs=%5d, rf=%5d, ",
		i2c_test->read_success + i2c_test->read_fail,
		i2c_test->read_success,
		i2c_test->read_fail);

	ccprintf("w=%5d, ws=%5d, wf=%5d\n",
		i2c_test->write_success + i2c_test->write_fail,
		i2c_test->write_success,
		i2c_test->write_fail);

	i2c_test->read_success = 0;
	i2c_test->read_fail = 0;
	i2c_test->write_success = 0,
	i2c_test->write_fail = 0;
}

#define I2C_STRESS_TEST_DATA_VERIFY_RETRY_COUNT 3
static int command_i2ctest(int argc, char **argv)
{
	char *e;
	int i, j, rv;
	uint32_t rand;
	int data, data_verify;
	int count = 10000;
	int udelay = 100;
	int test_dev = i2c_test_dev_used;
	struct i2c_stress_test_dev *i2c_s_test = NULL;
	struct i2c_test_reg_info *reg_s_info;
	struct i2c_test_results *test_s_results;

	if (argc > 1) {
		count = strtoi(argv[1], &e, 0);
		if (*e)
			return EC_ERROR_PARAM2;
	}

	if (argc > 2) {
		udelay = strtoi(argv[2], &e, 0);
		if (*e)
			return EC_ERROR_PARAM3;
	}

	if (argc > 3) {
		test_dev = strtoi(argv[3], &e, 0);
		if (*e || test_dev < 1 || test_dev > i2c_test_dev_used)
			return EC_ERROR_PARAM4;
		test_dev--;
	}

	for (i = 0; i < count; i++) {
		int port;
		uint16_t addr_flags;

		if (!(i % 1000))
			ccprintf("running test %d\n", i);

		if (argc < 4) {
			rand = get_time().val;
			test_dev = rand % i2c_test_dev_used;
		}

		port = i2c_stress_tests[test_dev].port;
		addr_flags = i2c_stress_tests[test_dev].addr_flags;
		i2c_s_test = i2c_stress_tests[test_dev].i2c_test;
		reg_s_info = &i2c_s_test->reg_info;
		test_s_results = &i2c_s_test->test_results;

		rand = get_time().val;
		if (rand & 0x1) {
			/* read */
			rv = i2c_s_test->i2c_read ?
				i2c_s_test->i2c_read(port, addr_flags,
					reg_s_info->read_reg, &data) :
				i2c_s_test->i2c_read_dev(
					reg_s_info->read_reg, &data);
			if (rv || data != reg_s_info->read_val)
				test_s_results->read_fail++;
			else
				test_s_results->read_success++;
		} else {
			/*
			 * Reads are more than writes in the system.
			 * Read and then write same value to ensure we are
			 * not changing any settings.
			 */

			/* Read the write register */
			rv = i2c_s_test->i2c_read ?
				i2c_s_test->i2c_read(port, addr_flags,
					reg_s_info->read_reg, &data) :
				i2c_s_test->i2c_read_dev(
					reg_s_info->read_reg, &data);
			if (rv) {
				/* Skip writing invalid data */
				test_s_results->read_fail++;
				continue;
			} else
				test_s_results->read_success++;

			j = I2C_STRESS_TEST_DATA_VERIFY_RETRY_COUNT;
			do {
				/* Write same value back */
				rv = i2c_s_test->i2c_write ?
					i2c_s_test->i2c_write(port,
					addr_flags,
					reg_s_info->write_reg, data) :
					i2c_s_test->i2c_write_dev(
					reg_s_info->write_reg, data);
				i++;
				if (rv) {
					/* Skip reading as write failed */
					test_s_results->write_fail++;
					break;
				}
				test_s_results->write_success++;

				/* Read back to verify the data */
				rv = i2c_s_test->i2c_read ?
					i2c_s_test->i2c_read(port,
					addr_flags,
					reg_s_info->read_reg, &data_verify) :
					i2c_s_test->i2c_read_dev(
					reg_s_info->read_reg, &data_verify);
				i++;
				if (rv) {
					/* Read failed try next time */
					test_s_results->read_fail++;
					break;
				} else if (!rv && data != data_verify) {
					/* Either data writes/read is wrong */
					j--;
				} else {
					j = 0;
					test_s_results->read_success++;
				}
			} while (j);
		}

		usleep(udelay);
	}

	ccprintf("\n**********final result **********\n");

	cflush();
	if (argc > 3) {
		i2c_test_status(&i2c_s_test->test_results, test_dev + 1);
	} else {
		for (i = 0; i < i2c_test_dev_used; i++) {
			i2c_s_test = i2c_stress_tests[i].i2c_test;
			i2c_test_status(&i2c_s_test->test_results, i + 1);
			msleep(100);
		}
	}
	cflush();

	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(i2ctest, command_i2ctest,
			"i2ctest count|udelay|dev",
			"I2C stress test");
#endif /* CONFIG_CMD_I2C_STRESS_TEST */
