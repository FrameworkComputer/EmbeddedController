/* Copyright 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Mock I2C driver for unit test.
 */

#include "hooks.h"
#include "i2c.h"
#include "i2c_private.h"
#include "link_defs.h"
#include "test_util.h"

#define MAX_DETACHED_DEV_COUNT 3

struct i2c_dev {
	int port;
	uint16_t slave_addr_flags;
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

int test_detach_i2c(const int port, const uint16_t slave_addr_flags)
{
	int i;

	for (i = 0; i < MAX_DETACHED_DEV_COUNT; ++i)
		if (detached_devs[i].valid == 0)
			break;

	if (i == MAX_DETACHED_DEV_COUNT)
		return EC_ERROR_OVERFLOW;

	detached_devs[i].port = port;
	detached_devs[i].slave_addr_flags = slave_addr_flags;
	detached_devs[i].valid = 1;

	return EC_SUCCESS;
}

int test_attach_i2c(const int port, const uint16_t slave_addr_flags)
{
	int i;

	for (i = 0; i < MAX_DETACHED_DEV_COUNT; ++i)
		if (detached_devs[i].valid &&
		    detached_devs[i].port == port &&
		    detached_devs[i].slave_addr_flags == slave_addr_flags)
			break;

	if (i == MAX_DETACHED_DEV_COUNT)
		return EC_ERROR_INVAL;

	detached_devs[i].valid = 0;
	return EC_SUCCESS;
}

static int test_check_detached(const int port,
			       const uint16_t slave_addr_flags)
{
	int i;

	for (i = 0; i < MAX_DETACHED_DEV_COUNT; ++i)
		if (detached_devs[i].valid &&
		    detached_devs[i].port == port &&
		    detached_devs[i].slave_addr_flags == slave_addr_flags)
			return 1;
	return 0;
}

int chip_i2c_xfer(const int port, const uint16_t slave_addr_flags,
		  const uint8_t *out, int out_size,
		  uint8_t *in, int in_size, int flags)
{
	const struct test_i2c_xfer *p;
	int rv;

	if (test_check_detached(port, slave_addr_flags))
		return EC_ERROR_UNKNOWN;
	for (p = __test_i2c_xfer; p < __test_i2c_xfer_end; ++p) {
		rv = p->routine(port, slave_addr_flags,
				out, out_size,
				in, in_size, flags);
		if (rv != EC_ERROR_INVAL)
			return rv;
	}
	return EC_ERROR_UNKNOWN;
}

int chip_i2c_set_freq(int port, enum i2c_freq freq)
{
	return EC_ERROR_UNIMPLEMENTED;
}

enum i2c_freq chip_i2c_get_freq(int port)
{
	switch (i2c_ports[port].kbps) {
	case 1000:
		return I2C_FREQ_1000KHZ;
	case 400:
		return I2C_FREQ_400KHZ;
	case 100:
		return I2C_FREQ_100KHZ;
	}

	/* fallback to 100k */
	return I2C_FREQ_100KHZ;
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

void i2c_init(void)
{
	/* We don't actually need to initialize anything here for host tests */
}
