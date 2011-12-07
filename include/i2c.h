/* Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
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

/* Reads a 16-bit register from the slave at 8-bit slave address
 * <slaveaddr>, at the specified 8-bit <offset> in the slave's address
 * space. */
int i2c_read16(int port, int slave_addr, int offset, int* data);

/* Writes a 16-bit register to the slave at 8-bit slave address
 * <slaveaddr>, at the specified 8-bit <offset> in the slave's address
 * space. */
int i2c_write16(int port, int slave_addr, int offset, int data);

#endif  /* __CROS_EC_I2C_H */
