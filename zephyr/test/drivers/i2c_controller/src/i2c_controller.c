/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include <zephyr/device.h>
#include <zephyr/fff.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/ztest.h>

#include "basic_i2c_device_emul.h"
#include "i2c.h"
#include "test/drivers/test_state.h"

struct i2c_controller_fixture {
	int port;
	int addr;
	const struct emul *emul;
	struct basic_i2c_device_data *emul_data;
};

ZTEST_F(i2c_controller, write_read32_le)
{
	uint32_t expected = 0xAABBCCDD;
	uint32_t actual;

	zassert_ok(i2c_write32(fixture->port, fixture->addr, 0, expected));

	/* Get the first four bytes of the register space as a uint32_t */
	actual = *((uint32_t *)&fixture->emul_data->regs[0]);

	zassert_equal(expected, actual, "got %08x, expected %08x", actual,
		      expected);

	/* Now read back through I2C API */
	zassert_ok(i2c_read32(fixture->port, fixture->addr, 0, &actual));

	zassert_equal(expected, actual, "got %08x, expected %08x", actual,
		      expected);
}

ZTEST_F(i2c_controller, write_read32_be)
{
	uint32_t expected = 0xAABBCCDD;
	uint32_t actual;

	zassert_ok(i2c_write32(fixture->port,
			       fixture->addr | I2C_FLAG_BIG_ENDIAN, 0,
			       expected));

	/* Get the first four bytes of the register space as a uint32_t */
	actual = __bswap_32(*((uint32_t *)&fixture->emul_data->regs[0]));

	zassert_equal(expected, actual, "got %08x, expected %08x", actual,
		      expected);

	/* Now read back through I2C API */
	zassert_ok(i2c_read32(fixture->port,
			      fixture->addr | I2C_FLAG_BIG_ENDIAN, 0, &actual));

	zassert_equal(expected, actual, "got %08x, expected %08x", actual,
		      expected);
}

ZTEST_F(i2c_controller, read32_fail)
{
	int ret;
	uint32_t data;

	/* Fail by reading from wrong address */
	ret = i2c_read32(fixture->port, fixture->addr + 1, 0, &data);

	zassert_equal(EC_ERROR_INVAL, ret, "Got %d", ret);
}

ZTEST_F(i2c_controller, write32_fail)
{
	int ret;

	/* Fail by writing to wrong address */
	ret = i2c_write32(fixture->port, fixture->addr + 1, 0, 0x00000000);

	zassert_equal(EC_ERROR_INVAL, ret, "Got %d", ret);
}

static void *setup(void)
{
	static struct i2c_controller_fixture fixture;
	const struct emul *emul = EMUL_DT_GET(DT_NODELABEL(basic_i2c_device));
	struct basic_i2c_device_data *emul_data =
		(struct basic_i2c_device_data *)emul->data;

	fixture.port = I2C_PORT_BY_DEV(DT_NODELABEL(basic_i2c_device));
	fixture.addr = emul_data->common.cfg->addr;
	fixture.emul = emul;
	fixture.emul_data = emul_data;

	return &fixture;
}

static void reset(void *data)
{
	struct i2c_controller_fixture *f =
		(struct i2c_controller_fixture *)data;

	basic_i2c_device_reset(f->emul);

	i2c_common_emul_set_read_fail_reg(&f->emul_data->common,
					  I2C_COMMON_EMUL_NO_FAIL_REG);
	i2c_common_emul_set_write_fail_reg(&f->emul_data->common,
					   I2C_COMMON_EMUL_NO_FAIL_REG);
}

ZTEST_SUITE(i2c_controller, drivers_predicate_post_main, setup, reset, reset,
	    NULL);
