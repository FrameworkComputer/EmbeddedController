/*
 * Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <zephyr/device.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/logging/log.h>
#include <zephyr/ztest.h>

#define DAGWOOD_I2C_DEV DT_NODELABEL(dagwood_eeprom)

LOG_MODULE_REGISTER(aic_i2c);

DEVICE_DT_DEFINE(DAGWOOD_I2C_DEV, NULL, NULL, NULL, NULL, POST_KERNEL, 90, 0);

struct aic_i2c_fixture {
	struct i2c_dt_spec dagwood_eeprom;
};

static void *aic_i2c_setup(void)
{
	static struct aic_i2c_fixture fixture = {
		.dagwood_eeprom = I2C_DT_SPEC_GET(DAGWOOD_I2C_DEV),
	};

	return &fixture;
}

ZTEST_SUITE(aic_i2c, NULL, aic_i2c_setup, NULL, NULL, NULL);

#define I2C_GOOD_OFFSET 24

ZTEST_F(aic_i2c, test_i2c_read_write)
{
	uint8_t read_byte;
	uint8_t write_byte;
	const struct i2c_dt_spec *i2c_spec = &fixture->dagwood_eeprom;

	LOG_INF("Dagwood eeprom %s:0x%02x", fixture->dagwood_eeprom.bus->name,
		fixture->dagwood_eeprom.addr);

	zassert_ok(i2c_reg_read_byte_dt(i2c_spec, I2C_GOOD_OFFSET, &read_byte));

	write_byte = ~read_byte;
	zassert_ok(
		i2c_reg_write_byte_dt(i2c_spec, I2C_GOOD_OFFSET, write_byte));

	zassert_ok(i2c_reg_read_byte_dt(i2c_spec, I2C_GOOD_OFFSET, &read_byte));

	zassert_equal(read_byte, write_byte);

	LOG_INF("I2C Read modify write successful");
}
