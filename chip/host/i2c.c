/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Dummy I2C driver for unit test.
 */

#include "i2c.h"

int i2c_read16(int port, int slave_addr, int offset, int *data)
{
	return EC_ERROR_UNKNOWN;
}

int i2c_write16(int port, int slave_addr, int offset, int data)
{
	return EC_ERROR_UNKNOWN;
}

int i2c_read8(int port, int slave_addr, int offset, int *data)
{
	return EC_ERROR_UNKNOWN;
}

int i2c_write8(int port, int slave_addr, int offset, int data)
{
	return EC_ERROR_UNKNOWN;
}

int i2c_read_string(int port, int slave_addr, int offset, uint8_t *data,
			int len)
{
	return EC_ERROR_UNKNOWN;
}
