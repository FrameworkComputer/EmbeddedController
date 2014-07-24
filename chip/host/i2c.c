/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Dummy I2C driver for unit test.
 */

#include "hooks.h"
#include "i2c.h"
#include "link_defs.h"
#include "test_util.h"

#define MAX_DETACHED_DEV_COUNT 3

struct i2c_dev {
	int port;
	int slave_addr;
	int valid;
};

static struct i2c_dev detached_devs[MAX_DETACHED_DEV_COUNT];

static void detach_init(void)
{
	int i;
	for (i = 0; i < MAX_DETACHED_DEV_COUNT; ++i)
		detached_devs[i].valid = 0;
}
DECLARE_HOOK(HOOK_INIT, detach_init, HOOK_PRIO_FIRST);

int test_detach_i2c(int port, int slave_addr)
{
	int i;

	for (i = 0; i < MAX_DETACHED_DEV_COUNT; ++i)
		if (detached_devs[i].valid == 0)
			break;

	if (i == MAX_DETACHED_DEV_COUNT)
		return EC_ERROR_OVERFLOW;

	detached_devs[i].port = port;
	detached_devs[i].slave_addr = slave_addr;
	detached_devs[i].valid = 1;

	return EC_SUCCESS;
}

int test_attach_i2c(int port, int slave_addr)
{
	int i;

	for (i = 0; i < MAX_DETACHED_DEV_COUNT; ++i)
		if (detached_devs[i].valid &&
		    detached_devs[i].port == port &&
		    detached_devs[i].slave_addr == slave_addr)
			break;

	if (i == MAX_DETACHED_DEV_COUNT)
		return EC_ERROR_INVAL;

	detached_devs[i].valid = 0;
	return EC_SUCCESS;
}

static int test_check_detached(int port, int slave_addr)
{
	int i;

	for (i = 0; i < MAX_DETACHED_DEV_COUNT; ++i)
		if (detached_devs[i].valid &&
		    detached_devs[i].port == port &&
		    detached_devs[i].slave_addr == slave_addr)
			return 1;
	return 0;
}

int i2c_read16(int port, int slave_addr, int offset, int *data)
{
	const struct test_i2c_read_dev *p;
	int rv;

	if (test_check_detached(port, slave_addr))
		return EC_ERROR_UNKNOWN;
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

	if (test_check_detached(port, slave_addr))
		return EC_ERROR_UNKNOWN;
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

	if (test_check_detached(port, slave_addr))
		return EC_ERROR_UNKNOWN;
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

	if (test_check_detached(port, slave_addr))
		return EC_ERROR_UNKNOWN;
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

	if (test_check_detached(port, slave_addr))
		return EC_ERROR_UNKNOWN;
	for (p = __test_i2c_read_string; p < __test_i2c_read_string_end; ++p) {
		rv = p->routine(port, slave_addr, offset, data, len);
		if (rv != EC_ERROR_INVAL)
			return rv;
	}
	return EC_ERROR_UNKNOWN;
}

int smbus_write_word(uint8_t i2c_port, uint8_t slave_addr,
			uint8_t smbus_cmd, uint16_t d16)
{
	return i2c_write16(i2c_port, slave_addr, smbus_cmd, d16);
}

int smbus_read_word(uint8_t i2c_port, uint8_t slave_addr,
			uint8_t smbus_cmd, uint16_t *p16)
{
	int rv, d16 = 0;
	rv = i2c_read16(i2c_port, slave_addr, smbus_cmd, &d16);
	*p16 = d16;
	return rv;
}

int smbus_read_string(int i2c_port, uint8_t slave_addr, uint8_t smbus_cmd,
			uint8_t *data, int len)
{
	return i2c_read_string(i2c_port, slave_addr, smbus_cmd, data, len);
}
