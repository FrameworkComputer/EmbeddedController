/* Copyright 2013 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* I2C cross-platform code for Chrome EC */

/*
 * TODO(b/272518464): Work around coreboot GCC preprocessor bug.
 * #line marks the *next* line, so it is off by one.
 */
#line 13

#include "builtin/assert.h"
#include "console.h"
#include "crc8.h"
#include "host_command.h"
#include "i2c.h"
#include "i2c_bitbang.h"
#include "i2c_private.h"
#include "printf.h"
#include "system.h"
#include "task.h"
#include "util.h"

#ifdef CONFIG_ZEPHYR
#include "i2c/i2c.h"

#include <zephyr/drivers/i2c.h>
#endif /* CONFIG_ZEPHYR */

#define CPUTS(outstr) cputs(CC_I2C, outstr)
#define CPRINTS(format, args...) cprints(CC_I2C, format, ##args)
#define CPRINTF(format, args...) cprintf(CC_I2C, format, ##args)

/* Only chips with multi-port controllers will define I2C_CONTROLLER_COUNT */
#ifndef I2C_CONTROLLER_COUNT
#define I2C_CONTROLLER_COUNT I2C_PORT_COUNT
#endif

static mutex_t port_mutex[I2C_CONTROLLER_COUNT + I2C_BITBANG_PORT_COUNT];

/* A bitmap of the controllers which are currently servicing a request. */
static volatile uint32_t i2c_port_active_list;
BUILD_ASSERT(ARRAY_SIZE(port_mutex) < 32);

#ifdef CONFIG_ZEPHYR
static int init_port_mutex(void)
{
	for (int i = 0; i < ARRAY_SIZE(port_mutex); ++i)
		k_mutex_init(port_mutex + i);

	return 0;
}
SYS_INIT(init_port_mutex, POST_KERNEL, 50);
#endif /* CONFIG_ZEPHYR */

/**
 * Non-deterministically test the lock status of the port.  If another task
 * has locked the port and the caller is accessing it illegally, then this test
 * will incorrectly return true.  However, callers which failed to statically
 * lock the port will fail quickly.
 */
STATIC_IF_NOT(CONFIG_ZTEST)
int i2c_port_is_locked(int port)
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

	/*
	 * If the EC's I2C driver implementation is task event based and the
	 * I2C is accessed before the task is initialized, it causes the system
	 * panic hence these I2C will fall back to bitbang mode if enabled at
	 * board level and will again switch back to event based I2C upon task
	 * initialization.
	 */
	if (task_start_called()) {
		/* Find the matching port in i2c_ports[] table. */
		for (i = 0; i < i2c_ports_used; i++) {
			if (i2c_ports[i].port == port)
				return &i2c_ports[i];
		}
	}

	if (IS_ENABLED(CONFIG_I2C_BITBANG)) {
		/* Find the matching port in i2c_bitbang_ports[] table. */
		for (i = 0; i < i2c_bitbang_ports_used; i++) {
			if (i2c_bitbang_ports[i].port == port)
				return &i2c_bitbang_ports[i];
		}
	}

	return NULL;
}

__maybe_unused static int chip_i2c_xfer_with_notify(const int port,
						    const uint16_t addr_flags,
						    const uint8_t *out,
						    int out_size, uint8_t *in,
						    int in_size, int flags)
{
	int ret;
	uint16_t no_pec_af = addr_flags;
	const struct i2c_port_t *i2c_port = get_i2c_port(port);

	if (i2c_port == NULL)
		return EC_ERROR_INVAL;

	if (IS_ENABLED(CONFIG_I2C_XFER_BOARD_CALLBACK))
		i2c_start_xfer_notify(port, addr_flags);

	if (IS_ENABLED(CONFIG_SMBUS_PEC))
		/*
		 * Since we've done PEC processing here,
		 * remove the flag so it won't confuse chip driver.
		 */
		no_pec_af &= ~I2C_FLAG_PEC;

	if (i2c_port->drv)
		ret = i2c_port->drv->xfer(i2c_port, no_pec_af, out, out_size,
					  in, in_size, flags);
	else
		ret = chip_i2c_xfer(port, no_pec_af, out, out_size, in, in_size,
				    flags);

	if (IS_ENABLED(CONFIG_I2C_XFER_BOARD_CALLBACK))
		i2c_end_xfer_notify(port, addr_flags);

	if (IS_ENABLED(CONFIG_I2C_DEBUG)) {
		i2c_trace_notify(port, addr_flags, out, out_size, in, in_size,
				 ret);
	}

	return ret;
}

#ifdef CONFIG_I2C_XFER_LARGE_TRANSFER
/*
 * Internal function that splits transfer into multiple chip_i2c_xfer() calls
 * if in_size or out_size exceeds CONFIG_I2C_CHIP_MAX_TRANSFER_SIZE.
 */
static int i2c_xfer_no_retry(const int port, const uint16_t addr_flags,
			     const uint8_t *out, int out_size, uint8_t *in,
			     int in_size, int flags)
{
	int offset;

	for (offset = 0; offset < out_size;) {
		int chunk_size = MIN(out_size - offset,
				     CONFIG_I2C_CHIP_MAX_TRANSFER_SIZE);
		int out_flags = 0;

		if (offset == 0)
			out_flags |= flags & I2C_XFER_START;
		if (in_size == 0 && offset + chunk_size == out_size)
			out_flags |= flags & I2C_XFER_STOP;

		RETURN_ERROR(chip_i2c_xfer_with_notify(port, addr_flags,
						       out + offset, chunk_size,
						       NULL, 0, out_flags));
		offset += chunk_size;
	}
	for (offset = 0; offset < in_size;) {
		int chunk_size = MIN(in_size - offset,
				     CONFIG_I2C_CHIP_MAX_TRANSFER_SIZE);
		int in_flags = 0;

		if (offset == 0)
			in_flags |= flags & I2C_XFER_START;
		if (offset + chunk_size == in_size)
			in_flags |= flags & I2C_XFER_STOP;

		RETURN_ERROR(chip_i2c_xfer_with_notify(port, addr_flags, NULL,
						       0, in + offset,
						       chunk_size, in_flags));
		offset += chunk_size;
	}
	return EC_SUCCESS;
}
#endif /* CONFIG_I2C_XFER_LARGE_TRANSFER */

int i2c_xfer_unlocked(const int port, const uint16_t addr_flags,
		      const uint8_t *out, int out_size, uint8_t *in,
		      int in_size, int flags)
{
	int i;
	int ret = EC_SUCCESS;
	uint16_t no_pec_af = addr_flags & ~I2C_FLAG_PEC;

	if (!i2c_port_is_locked(port)) {
		CPUTS("Access I2C without lock!");
		return EC_ERROR_INVAL;
	}

	for (i = 0; i <= CONFIG_I2C_NACK_RETRY_COUNT; i++) {
#ifdef CONFIG_ZEPHYR
		struct i2c_msg msg[2];
		int num_msgs = 0;

		/* Be careful to respect the flags passed in */
		if (out_size) {
			unsigned int wflags = I2C_MSG_WRITE;

			msg[num_msgs].buf = (uint8_t *)out;
			msg[num_msgs].len = out_size;

			/* If this is the last write, add a stop */
			if (!in_size && (flags & I2C_XFER_STOP))
				wflags |= I2C_MSG_STOP;
			msg[num_msgs].flags = wflags;
			num_msgs++;
		}
		if (in_size) {
			unsigned int rflags = I2C_MSG_READ;

			msg[num_msgs].buf = (uint8_t *)in;
			msg[num_msgs].len = in_size;
			rflags = I2C_MSG_READ;

			/* If a stop is requested, add it */
			if (flags & I2C_XFER_STOP)
				rflags |= I2C_MSG_STOP;

			/*
			 * If this read follows a write (above) then we need a
			 * restart
			 */
			if (num_msgs)
				rflags |= I2C_MSG_RESTART;
			msg[num_msgs].flags = rflags;
			num_msgs++;
		}

		/* Big endian flag is used in wrappers for this call */
		if (no_pec_af & ~(I2C_ADDR_MASK | I2C_FLAG_BIG_ENDIAN))
			ccprintf("Ignoring flags from i2c addr_flags: %04x",
				 no_pec_af);

		ret = i2c_transfer(i2c_get_device_for_port(port), msg, num_msgs,
				   I2C_STRIP_FLAGS(no_pec_af));

		if (IS_ENABLED(CONFIG_I2C_DEBUG)) {
			i2c_trace_notify(port, addr_flags, out, out_size, in,
					 in_size, ret);
		}

		switch (ret) {
		case 0:
			return EC_SUCCESS;
		case -EIO:
			ret = EC_ERROR_INVAL;
			continue;
		default:
			return EC_ERROR_UNKNOWN;
		}
#elif defined(CONFIG_I2C_XFER_LARGE_TRANSFER)
		ret = i2c_xfer_no_retry(port, no_pec_af, out, out_size, in,
					in_size, flags);
#else
		ret = chip_i2c_xfer_with_notify(port, no_pec_af, out, out_size,
						in, in_size, flags);
#endif /* CONFIG_I2C_XFER_LARGE_TRANSFER */
		if (ret != EC_ERROR_BUSY)
			break;
	}
	return ret;
}

int i2c_xfer(const int port, const uint16_t addr_flags, const uint8_t *out,
	     int out_size, uint8_t *in, int in_size)
{
	int rv;

	i2c_lock(port, 1);
	rv = i2c_xfer_unlocked(port, addr_flags, out, out_size, in, in_size,
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
	if (port < 0 || port >= ARRAY_SIZE(port_mutex))
		return;

	if (lock) {
		uint32_t irq_lock_key;

		mutex_lock(port_mutex + port);

		/* Disable interrupt during changing counter for preemption. */
		irq_lock_key = irq_lock();

		i2c_port_active_list |= BIT(port);
		/* EC cannot enter sleep if there's any i2c port active. */
		disable_sleep(SLEEP_MASK_I2C_CONTROLLER);

		irq_unlock(irq_lock_key);
	} else {
		uint32_t irq_lock_key = irq_lock();

		i2c_port_active_list &= ~BIT(port);
		/* Once there is no i2c port active, enable sleep bit of i2c. */
		if (!i2c_port_active_list)
			enable_sleep(SLEEP_MASK_I2C_CONTROLLER);

		irq_unlock(irq_lock_key);

		mutex_unlock(port_mutex + port);
	}
}

void i2c_prepare_sysjump(void)
{
	int i;

	/* Lock all i2c controllers */
	for (i = 0; i < ARRAY_SIZE(port_mutex); ++i)
		mutex_lock(port_mutex + i);
}

/* i2c_readN with optional error checking */
static int platform_ec_i2c_read(const int port, const uint16_t addr_flags,
				uint8_t reg, uint8_t *in, int in_size)
{
	if (!IS_ENABLED(CONFIG_SMBUS_PEC) && I2C_USE_PEC(addr_flags))
		return EC_ERROR_UNIMPLEMENTED;

	if (IS_ENABLED(CONFIG_SMBUS_PEC) && I2C_USE_PEC(addr_flags)) {
		int i, rv;
		/* addr_8bit = 7 bit addr_flags + 1 bit r/w */
		uint8_t addr_8bit = I2C_STRIP_FLAGS(addr_flags) << 1;
		uint8_t out[3] = { addr_8bit, reg, addr_8bit | 1 };
		uint8_t pec_local = 0, pec_remote;

		i2c_lock(port, 1);
		for (i = 0; i <= CONFIG_I2C_NACK_RETRY_COUNT; i++) {
			rv = i2c_xfer_unlocked(port, addr_flags, &reg, 1, in,
					       in_size, I2C_XFER_START);
			if (rv)
				continue;

			rv = i2c_xfer_unlocked(port, addr_flags, NULL, 0,
					       &pec_remote, 1, I2C_XFER_STOP);
			if (rv)
				continue;

			pec_local = cros_crc8(out, ARRAY_SIZE(out));
			pec_local = cros_crc8_arg(in, in_size, pec_local);
			if (pec_local == pec_remote)
				break;

			rv = EC_ERROR_CRC;
		}
		i2c_lock(port, 0);

		return rv;
	}

	return i2c_xfer(port, addr_flags, &reg, 1, in, in_size);
}

/* i2c_writeN with optional error checking */
static int platform_ec_i2c_write(const int port, const uint16_t addr_flags,
				 const uint8_t *out, int out_size)
{
	if (!IS_ENABLED(CONFIG_SMBUS_PEC) && I2C_USE_PEC(addr_flags))
		return EC_ERROR_UNIMPLEMENTED;

	if (IS_ENABLED(CONFIG_SMBUS_PEC) && I2C_USE_PEC(addr_flags)) {
		int i, rv;
		uint8_t addr_8bit = I2C_STRIP_FLAGS(addr_flags) << 1;
		uint8_t pec;

		pec = cros_crc8(&addr_8bit, 1);
		pec = cros_crc8_arg(out, out_size, pec);

		i2c_lock(port, 1);
		for (i = 0; i <= CONFIG_I2C_NACK_RETRY_COUNT; i++) {
			rv = i2c_xfer_unlocked(port, addr_flags, out, out_size,
					       NULL, 0, I2C_XFER_START);
			if (rv)
				continue;

			rv = i2c_xfer_unlocked(port, addr_flags, &pec, 1, NULL,
					       0, I2C_XFER_STOP);
			if (!rv)
				break;
		}
		i2c_lock(port, 0);

		return rv;
	}

	return i2c_xfer(port, addr_flags, out, out_size, NULL, 0);
}

int i2c_read32(const int port, const uint16_t addr_flags, int offset, int *data)
{
	int rv;
	uint8_t reg, buf[sizeof(uint32_t)];

	reg = offset & 0xff;
	/* I2C read 32-bit word: transmit 8-bit offset, and read 32bits */
	rv = platform_ec_i2c_read(port, addr_flags, reg, buf, sizeof(uint32_t));

	if (rv)
		return rv;

	if (I2C_IS_BIG_ENDIAN(addr_flags))
		*data = ((int)buf[0] << 24) | ((int)buf[1] << 16) |
			((int)buf[2] << 8) | buf[3];
	else
		*data = ((int)buf[3] << 24) | ((int)buf[2] << 16) |
			((int)buf[1] << 8) | buf[0];

	return EC_SUCCESS;
}

int i2c_write32(const int port, const uint16_t addr_flags, int offset, int data)
{
	uint8_t buf[1 + sizeof(uint32_t)];

	buf[0] = offset & 0xff;

	if (I2C_IS_BIG_ENDIAN(addr_flags)) {
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

	return platform_ec_i2c_write(port, addr_flags, buf,
				     sizeof(uint32_t) + 1);
}

int i2c_read16(const int port, const uint16_t addr_flags, int offset, int *data)
{
	int rv;
	uint8_t reg, buf[sizeof(uint16_t)];

	reg = offset & 0xff;
	/* I2C read 16-bit word: transmit 8-bit offset, and read 16bits */
	rv = platform_ec_i2c_read(port, addr_flags, reg, buf, sizeof(uint16_t));

	if (rv)
		return rv;

	if (I2C_IS_BIG_ENDIAN(addr_flags))
		*data = ((int)buf[0] << 8) | buf[1];
	else
		*data = ((int)buf[1] << 8) | buf[0];

	return EC_SUCCESS;
}

int i2c_write16(const int port, const uint16_t addr_flags, int offset, int data)
{
	uint8_t buf[1 + sizeof(uint16_t)];

	buf[0] = offset & 0xff;

	if (I2C_IS_BIG_ENDIAN(addr_flags)) {
		buf[1] = (data >> 8) & 0xff;
		buf[2] = data & 0xff;
	} else {
		buf[1] = data & 0xff;
		buf[2] = (data >> 8) & 0xff;
	}

	return platform_ec_i2c_write(port, addr_flags, buf,
				     1 + sizeof(uint16_t));
}

int i2c_read8(const int port, const uint16_t addr_flags, int offset, int *data)
{
	int rv;
	uint8_t reg = offset;
	uint8_t buf;

	reg = offset;

	rv = platform_ec_i2c_read(port, addr_flags, reg, &buf, sizeof(uint8_t));
	if (!rv)
		*data = buf;

	return rv;
}

int i2c_write8(const int port, const uint16_t addr_flags, int offset, int data)
{
	uint8_t buf[2];

	buf[0] = offset;
	buf[1] = data;

	return platform_ec_i2c_write(port, addr_flags, buf, sizeof(buf));
}

int i2c_update8(const int port, const uint16_t addr_flags, const int offset,
		const uint8_t mask, const enum mask_update_action action)
{
	int rv;
	int read_val;
	int write_val;

	rv = i2c_read8(port, addr_flags, offset, &read_val);
	if (rv)
		return rv;

	write_val = (action == MASK_SET) ? (read_val | mask) :
					   (read_val & ~mask);

	if (IS_ENABLED(CONFIG_I2C_UPDATE_IF_CHANGED) && write_val == read_val)
		return EC_SUCCESS;

	return i2c_write8(port, addr_flags, offset, write_val);
}

int i2c_update16(const int port, const uint16_t addr_flags, const int offset,
		 const uint16_t mask, const enum mask_update_action action)
{
	int rv;
	int read_val;
	int write_val;

	rv = i2c_read16(port, addr_flags, offset, &read_val);
	if (rv)
		return rv;

	write_val = (action == MASK_SET) ? (read_val | mask) :
					   (read_val & ~mask);

	if (IS_ENABLED(CONFIG_I2C_UPDATE_IF_CHANGED) && write_val == read_val)
		return EC_SUCCESS;

	return i2c_write16(port, addr_flags, offset, write_val);
}

int i2c_field_update8(const int port, const uint16_t addr_flags,
		      const int offset, const uint8_t field_mask,
		      const uint8_t set_value)
{
	int rv;
	int read_val;
	int write_val;

	rv = i2c_read8(port, addr_flags, offset, &read_val);
	if (rv)
		return rv;

	write_val = (read_val & (~field_mask)) | set_value;

	if (IS_ENABLED(CONFIG_I2C_UPDATE_IF_CHANGED) && write_val == read_val)
		return EC_SUCCESS;

	return i2c_write8(port, addr_flags, offset, write_val);
}

int i2c_field_update16(const int port, const uint16_t addr_flags,
		       const int offset, const uint16_t field_mask,
		       const uint16_t set_value)
{
	int rv;
	int read_val;
	int write_val;

	rv = i2c_read16(port, addr_flags, offset, &read_val);
	if (rv)
		return rv;

	write_val = (read_val & (~field_mask)) | set_value;

	if (IS_ENABLED(CONFIG_I2C_UPDATE_IF_CHANGED) && write_val == read_val)
		return EC_SUCCESS;

	return i2c_write16(port, addr_flags, offset, write_val);
}

int i2c_read_offset16(const int port, const uint16_t addr_flags,
		      uint16_t offset, int *data, int len)
{
	int rv;
	uint8_t buf[sizeof(uint16_t)], addr[sizeof(uint16_t)];

	if (len < 0 || len > 2)
		return EC_ERROR_INVAL;

	addr[0] = (offset >> 8) & 0xff;
	addr[1] = offset & 0xff;

	/* I2C read 16-bit word: transmit 16-bit offset, and read buffer */
	rv = i2c_xfer(port, addr_flags, addr, 2, buf, len);

	if (rv)
		return rv;

	if (len == 1) {
		*data = buf[0];
	} else {
		if (I2C_IS_BIG_ENDIAN(addr_flags))
			*data = ((int)buf[0] << 8) | buf[1];
		else
			*data = ((int)buf[1] << 8) | buf[0];
	}

	return EC_SUCCESS;
}

int i2c_write_offset16(const int port, const uint16_t addr_flags,
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
		if (I2C_IS_BIG_ENDIAN(addr_flags)) {
			buf[2] = (data >> 8) & 0xff;
			buf[3] = data & 0xff;
		} else {
			buf[2] = data & 0xff;
			buf[3] = (data >> 8) & 0xff;
		}
	}

	return i2c_xfer(port, addr_flags, buf, 2 + len, NULL, 0);
}

int i2c_read_offset16_block(const int port, const uint16_t addr_flags,
			    uint16_t offset, uint8_t *data, int len)
{
	uint8_t addr[sizeof(uint16_t)];

	addr[0] = (offset >> 8) & 0xff;
	addr[1] = offset & 0xff;

	return i2c_xfer(port, addr_flags, addr, 2, data, len);
}

int i2c_write_offset16_block(const int port, const uint16_t addr_flags,
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
	rv = i2c_xfer_unlocked(port, addr_flags, addr, 2, NULL, 0,
			       I2C_XFER_START);
	if (!rv)
		rv = i2c_xfer_unlocked(port, addr_flags, data, len, NULL, 0,
				       I2C_XFER_STOP);
	i2c_lock(port, 0);

	return rv;
}

int i2c_read_sized_block(const int port, const uint16_t addr_flags, int offset,
			 uint8_t *data, int max_len, int *read_len)
{
	int i, rv;
	uint8_t reg, block_length;

	if (max_len == 0)
		return EC_ERROR_INVAL;

	if (!IS_ENABLED(CONFIG_SMBUS_PEC) && I2C_USE_PEC(addr_flags))
		return EC_ERROR_UNIMPLEMENTED;

	reg = offset;
	i2c_lock(port, 1);

	for (i = 0; i <= CONFIG_I2C_NACK_RETRY_COUNT; i++) {
		int data_length = 0;

		/*
		 * Send device reg space offset, and read back block length.
		 * Keep this session open without a stop.
		 */
		rv = i2c_xfer_unlocked(port, addr_flags, &reg, 1, &block_length,
				       1, I2C_XFER_START);
		if (rv)
			continue;

		if (block_length > max_len)
			data_length = max_len;
		else
			data_length = block_length;

		if (IS_ENABLED(CONFIG_SMBUS_PEC) && I2C_USE_PEC(addr_flags)) {
			uint8_t addr_8bit = I2C_STRIP_FLAGS(addr_flags) << 1;
			uint8_t out[3] = { addr_8bit, reg, addr_8bit | 1 };
			uint8_t pec, pec_remote;

			rv = i2c_xfer_unlocked(port, addr_flags, 0, 0, data,
					       data_length, 0);
			if (rv)
				continue;

			pec = cros_crc8(out, sizeof(out));
			pec = cros_crc8_arg(&block_length, 1, pec);
			pec = cros_crc8_arg(data, data_length, pec);

			/* read all remaining bytes */
			block_length -= data_length;
			while (block_length) {
				uint8_t byte;

				rv = i2c_xfer_unlocked(port, addr_flags, NULL,
						       0, &byte, 1, 0);
				if (rv)
					break;
				pec = cros_crc8_arg(&byte, 1, pec);
				--block_length;
			}
			if (rv)
				continue;

			rv = i2c_xfer_unlocked(port, addr_flags, NULL, 0,
					       &pec_remote, 1, I2C_XFER_STOP);
			if (rv)
				continue;

			if (pec != pec_remote)
				rv = EC_ERROR_CRC;
		} else {
			rv = i2c_xfer_unlocked(port, addr_flags, 0, 0, data,
					       data_length, I2C_XFER_STOP);
			if (rv)
				continue;
		}

		/* execution reaches here implies rv=0, so we can exit now */
		*read_len = data_length;
		break;
	}

	i2c_lock(port, 0);
	return rv;
}

int i2c_read_string(const int port, const uint16_t addr_flags, int offset,
		    uint8_t *data, int len)
{
	int read_len = 0;
	int rv = 0;

	if (len == 0)
		return EC_ERROR_INVAL;

	rv = i2c_read_sized_block(port, addr_flags, offset, data, len - 1,
				  &read_len);
	data[read_len] = 0;
	return rv;
}

int i2c_read_block(const int port, const uint16_t addr_flags, int offset,
		   uint8_t *data, int len)
{
	int rv;
	uint8_t reg_address = offset;

	rv = i2c_xfer(port, addr_flags, &reg_address, 1, data, len);
	return rv;
}

int i2c_write_block(const int port, const uint16_t addr_flags, int offset,
		    const uint8_t *data, int len)
{
	int i, rv;
	uint8_t reg_address = offset, pec = 0;

	if (!IS_ENABLED(CONFIG_SMBUS_PEC) && I2C_USE_PEC(addr_flags))
		return EC_ERROR_UNIMPLEMENTED;

	if (IS_ENABLED(CONFIG_SMBUS_PEC) && I2C_USE_PEC(addr_flags)) {
		uint8_t addr_8bit = I2C_STRIP_FLAGS(addr_flags) << 1;

		pec = cros_crc8(&addr_8bit, sizeof(uint8_t));
		pec = cros_crc8_arg(&reg_address, sizeof(uint8_t), pec);
		pec = cros_crc8_arg(data, len, pec);
	}

	/*
	 * Split into two transactions to avoid the stack space consumption of
	 * appending the destination address with the data array.
	 */
	i2c_lock(port, 1);
	for (i = 0; i <= CONFIG_I2C_NACK_RETRY_COUNT; i++) {
		rv = i2c_xfer_unlocked(port, addr_flags, &reg_address, 1, NULL,
				       0, I2C_XFER_START);
		if (rv)
			continue;

		if (IS_ENABLED(CONFIG_SMBUS_PEC) && I2C_USE_PEC(addr_flags)) {
			rv = i2c_xfer_unlocked(port, addr_flags, data, len,
					       NULL, 0, 0);
			if (rv)
				continue;

			rv = i2c_xfer_unlocked(port, addr_flags, &pec,
					       sizeof(uint8_t), NULL, 0,
					       I2C_XFER_STOP);
			if (rv)
				continue;
		} else {
			rv = i2c_xfer_unlocked(port, addr_flags, data, len,
					       NULL, 0, I2C_XFER_STOP);
			if (rv)
				continue;
		}

		/* execution reaches here implies rv=0, so we can exit now */
		break;
	}
	i2c_lock(port, 0);

	return rv;
}

int i2c_freq_to_khz(enum i2c_freq freq)
{
	switch (freq) {
	case I2C_FREQ_100KHZ:
		return 100;
	case I2C_FREQ_400KHZ:
		return 400;
	case I2C_FREQ_1000KHZ:
		return 1000;
	default:
		return 0;
	}
}

enum i2c_freq i2c_khz_to_freq(int speed_khz)
{
	switch (speed_khz) {
	case 100:
		return I2C_FREQ_100KHZ;
	case 400:
		return I2C_FREQ_400KHZ;
	case 1000:
		return I2C_FREQ_1000KHZ;
	default:
		return I2C_FREQ_COUNT;
	}
}

int i2c_set_freq(int port, enum i2c_freq freq)
{
	int ret;
	const struct i2c_port_t *cfg;

	cfg = get_i2c_port(port);
	if (cfg == NULL)
		return EC_ERROR_INVAL;

	if (!(cfg->flags & I2C_PORT_FLAG_DYNAMIC_SPEED))
		return EC_ERROR_UNIMPLEMENTED;

	i2c_lock(port, 1);
	ret = chip_i2c_set_freq(port, freq);
	i2c_lock(port, 0);
	return ret;
}

enum i2c_freq i2c_get_freq(int port)
{
	return chip_i2c_get_freq(port);
}

/*****************************************************************************/
/* Host commands */

#ifdef CONFIG_HOSTCMD_I2C_CONTROL

static enum ec_status i2c_command_control(struct host_cmd_handler_args *args)
{
#ifdef CONFIG_ZEPHYR
	/* For Zephyr, convert the received remote port number to a port number
	 * used in EC.
	 */
	((struct ec_params_i2c_control *)(args->params))->port =
		i2c_get_port_from_remote_port(
			((struct ec_params_i2c_control *)(args->params))->port);
#endif
	const struct ec_params_i2c_control *params = args->params;
	struct ec_response_i2c_control *resp = args->response;
	enum i2c_freq old_i2c_freq;
	enum i2c_freq new_i2c_freq;
	const struct i2c_port_t *cfg;
	uint16_t old_i2c_speed_khz;
	uint16_t new_i2c_speed_khz;
	enum ec_error_list rv;
	int khz;

	cfg = get_i2c_port(params->port);
	if (!cfg)
		return EC_RES_INVALID_PARAM;

	switch (params->cmd) {
	case EC_I2C_CONTROL_GET_SPEED:
		old_i2c_freq = i2c_get_freq(cfg->port);
		khz = i2c_freq_to_khz(old_i2c_freq);
		old_i2c_speed_khz = (khz != 0) ? khz :
						 EC_I2C_CONTROL_SPEED_UNKNOWN;
		break;

	case EC_I2C_CONTROL_SET_SPEED:
		new_i2c_speed_khz = params->cmd_params.speed_khz;
		new_i2c_freq = i2c_khz_to_freq(new_i2c_speed_khz);
		if (new_i2c_freq == I2C_FREQ_COUNT)
			return EC_RES_INVALID_PARAM;

		old_i2c_freq = i2c_get_freq(cfg->port);
		old_i2c_speed_khz = i2c_freq_to_khz(old_i2c_freq);

		rv = i2c_set_freq(cfg->port, new_i2c_freq);
		if (rv != EC_SUCCESS)
			return EC_RES_ERROR;

		CPRINTS("I2C%d speed changed from %d kHz to %d kHz",
			params->port, old_i2c_speed_khz, new_i2c_speed_khz);
		break;

	default:
		return EC_RES_INVALID_COMMAND;
	}

	resp->cmd_response.speed_khz = old_i2c_speed_khz;
	args->response_size = sizeof(*resp);

	return EC_RES_SUCCESS;
}

DECLARE_HOST_COMMAND(EC_CMD_I2C_CONTROL, i2c_command_control, EC_VER_MASK(0));

#endif /* CONFIG_HOSTCMD_I2C_CONTROL */

/*****************************************************************************/
/* Console commands */

#ifdef CONFIG_CMD_I2C_STRESS_TEST
static void i2c_test_status(struct i2c_test_results *i2c_test, int test_dev)
{
	ccprintf("test_dev=%2d, ", test_dev);
	ccprintf("r=%5d, rs=%5d, rf=%5d, ",
		 i2c_test->read_success + i2c_test->read_fail,
		 i2c_test->read_success, i2c_test->read_fail);

	ccprintf("w=%5d, ws=%5d, wf=%5d\n",
		 i2c_test->write_success + i2c_test->write_fail,
		 i2c_test->write_success, i2c_test->write_fail);

	i2c_test->read_success = 0;
	i2c_test->read_fail = 0;
	i2c_test->write_success = 0, i2c_test->write_fail = 0;
}

#define I2C_STRESS_TEST_DATA_VERIFY_RETRY_COUNT 3
static int command_i2ctest(int argc, const char **argv)
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
							  reg_s_info->read_reg,
							  &data) :
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
							  reg_s_info->read_reg,
							  &data) :
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
					     i2c_s_test->i2c_write(
						     port, addr_flags,
						     reg_s_info->write_reg,
						     data) :
					     i2c_s_test->i2c_write_dev(
						     reg_s_info->write_reg,
						     data);
				i++;
				if (rv) {
					/* Skip reading as write failed */
					test_s_results->write_fail++;
					break;
				}
				test_s_results->write_success++;

				/* Read back to verify the data */
				rv = i2c_s_test->i2c_read ?
					     i2c_s_test->i2c_read(
						     port, addr_flags,
						     reg_s_info->read_reg,
						     &data_verify) :
					     i2c_s_test->i2c_read_dev(
						     reg_s_info->read_reg,
						     &data_verify);
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
DECLARE_CONSOLE_COMMAND(i2ctest, command_i2ctest, "i2ctest count|udelay|dev",
			"I2C stress test");
#endif /* CONFIG_CMD_I2C_STRESS_TEST */
