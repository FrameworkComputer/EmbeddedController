/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* I2C interface for Chrome EC */

#ifndef __CROS_EC_I2C_H
#define __CROS_EC_I2C_H

#include "common.h"

/* Flags for slave address field, in addition to the 8-bit address */
#define I2C_FLAG_BIG_ENDIAN 0x100  /* 16 byte values are MSB-first */

/* Initializes the module. */
int i2c_init(void);

/* Notifies the module the system clock frequency has changed to <freq>. */
void i2c_clock_changed(int freq);

/* Reads a 16-bit register from the slave at 8-bit slave address
 * <slaveaddr>, at the specified 8-bit <offset> in the slave's address
 * space. */
int i2c_read16(int port, int slave_addr, int offset, int* data);

/* Writes a 16-bit register to the slave at 8-bit slave address
 * <slaveaddr>, at the specified 8-bit <offset> in the slave's address
 * space. */
int i2c_write16(int port, int slave_addr, int offset, int data);

/* Reads an 8-bit register from the slave at 8-bit slave address
 * <slaveaddr>, at the specified 8-bit <offset> in the slave's address
 * space. */
int i2c_read8(int port, int slave_addr, int offset, int* data);

/* Writes an 8-bit register to the slave at 8-bit slave address
 * <slaveaddr>, at the specified 8-bit <offset> in the slave's address
 * space. */
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
