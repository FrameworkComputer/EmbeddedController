/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* I2C interface for Chrome EC */

#ifndef __CROS_EC_I2C_H
#define __CROS_EC_I2C_H

#include "common.h"

/* Flags for slave address field, in addition to the 8-bit address */
#define I2C_FLAG_BIG_ENDIAN 0x100  /* 16 byte values are MSB-first */

/* Data structure to define I2C port configuration. */
struct i2c_port_t {
	const char *name;  /* Port name */
	int port;          /* Port */
	int kbps;          /* Speed in kbps */
};

extern const struct i2c_port_t i2c_ports[];

/* Flags for i2c_xfer() */
#define I2C_XFER_START (1 << 0)  /* Start smbus session from idle state */
#define I2C_XFER_STOP (1 << 1)  /* Terminate smbus session with stop bit */
#define I2C_XFER_SINGLE (I2C_XFER_START | I2C_XFER_STOP)  /* One transaction */

/**
 * Transmit one block of raw data, then receive one block of raw data.
 *
 * This is a low-level platform-dependent function used by the other functions
 * below.  It must be called between i2c_lock(port, 1) and i2c_lock(port, 0).
 *
 * @param port		Port to access
 * @param slave_addr	Slave device address
 * @param out		Data to send
 * @param out_size	Number of bytes to send
 * @param in		Destination buffer for received data
 * @param in_size	Number of bytes to receive
 * @param flags		Flags (see I2C_XFER_* above)
 * @return EC_SUCCESS, or non-zero if error.
 */
int i2c_xfer(int port, int slave_addr, const uint8_t *out, int out_size,
	     uint8_t *in, int in_size, int flags);

#define I2C_LINE_SCL_HIGH (1 << 0)
#define I2C_LINE_SDA_HIGH (1 << 1)
#define I2C_LINE_IDLE (I2C_LINE_SCL_HIGH | I2C_LINE_SDA_HIGH)

/**
 * Return raw I/O line levels (I2C_LINE_*) for a port.
 *
 * @param port		Port to check
 */
int i2c_get_line_levels(int port);

/**
 * Lock / unlock an I2C port.
 * @param port		Port to lock
 * @param lock		1 to lock, 0 to unlock
 */
void i2c_lock(int port, int lock);

/* Read a 16-bit register from the slave at 8-bit slave address <slaveaddr>, at
 * the specified 8-bit <offset> in the slave's address space. */
int i2c_read16(int port, int slave_addr, int offset, int* data);

/* Write a 16-bit register to the slave at 8-bit slave address <slaveaddr>, at
 * the specified 8-bit <offset> in the slave's address space. */
int i2c_write16(int port, int slave_addr, int offset, int data);

/* Read an 8-bit register from the slave at 8-bit slave address <slaveaddr>, at
 * the specified 8-bit <offset> in the slave's address space. */
int i2c_read8(int port, int slave_addr, int offset, int* data);

/* Write an 8-bit register to the slave at 8-bit slave address <slaveaddr>, at
 * the specified 8-bit <offset> in the slave's address space. */
int i2c_write8(int port, int slave_addr, int offset, int data);

/* Read ascii string using smbus read block protocol.
 * Read bytestream from <slaveaddr>:<offset> with format:
 *     [length_N] [byte_0] [byte_1] ... [byte_N-1]
 *
 * <len>      : the max length of receving buffer. to read N bytes
 *              ascii, len should be at least N+1 to include the
 *              terminating 0.
 * <len> == 0 : buffer size > 255
 */
int i2c_read_string(int port, int slave_addr, int offset, uint8_t *data,
			int len);

#endif  /* __CROS_EC_I2C_H */
