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

int chip_i2c_xfer(int port, int slave_addr, const uint8_t *out, int out_size,
		  uint8_t *in, int in_size, int flags)
{
	const struct test_i2c_xfer *p;
	int rv;

	if (test_check_detached(port, slave_addr))
		return EC_ERROR_UNKNOWN;
	for (p = __test_i2c_xfer; p < __test_i2c_xfer_end; ++p) {
		rv = p->routine(port, slave_addr, out, out_size,
				in, in_size, flags);
		if (rv != EC_ERROR_INVAL)
			return rv;
	}
	return EC_ERROR_UNKNOWN;
}

int i2c_raw_get_scl(int port)
{
	return 1;
}

int i2c_raw_get_sda(int port)
{
	return 1;
}

int i2c_get_line_levels(int port)
{
	return 0;
}
