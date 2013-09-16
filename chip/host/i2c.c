/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Dummy I2C driver for unit test.
 */

#include "i2c.h"
#include "link_defs.h"
#include "test_util.h"

int i2c_read16(int port, int slave_addr, int offset, int *data)
{
	const struct test_i2c_read_dev *p;
	int rv;

	for (p = __test_i2c_read16; p < __test_i2c_read16_end; ++p) {
		rv = p->routine(port, slave_addr, offset, data);
		if (rv != EC_ERROR_INVAL)
			return rv;
	}
	return EC_ERROR_UNKNOWN;
}

int i2c_write16(int port, int slave_addr, int offset, int data)
{
	const struct test_i2c_write_dev *p;
	int rv;

	for (p = __test_i2c_write16; p < __test_i2c_write16_end; ++p) {
		rv = p->routine(port, slave_addr, offset, data);
		if (rv != EC_ERROR_INVAL)
			return rv;
	}
	return EC_ERROR_UNKNOWN;
}

int i2c_read8(int port, int slave_addr, int offset, int *data)
{
	const struct test_i2c_read_dev *p;
	int rv;

	for (p = __test_i2c_read8; p < __test_i2c_read8_end; ++p) {
		rv = p->routine(port, slave_addr, offset, data);
		if (rv != EC_ERROR_INVAL)
			return rv;
	}
	return EC_ERROR_UNKNOWN;
}

int i2c_write8(int port, int slave_addr, int offset, int data)
{
	const struct test_i2c_write_dev *p;
	int rv;

	for (p = __test_i2c_write8; p < __test_i2c_write8_end; ++p) {
		rv = p->routine(port, slave_addr, offset, data);
		if (rv != EC_ERROR_INVAL)
			return rv;
	}
	return EC_ERROR_UNKNOWN;
}

int i2c_read_string(int port, int slave_addr, int offset, uint8_t *data,
			int len)
{
	const struct test_i2c_read_string_dev *p;
	int rv;

	for (p = __test_i2c_read_string; p < __test_i2c_read_string_end; ++p) {
		rv = p->routine(port, slave_addr, offset, data, len);
		if (rv != EC_ERROR_INVAL)
			return rv;
	}
	return EC_ERROR_UNKNOWN;
}
