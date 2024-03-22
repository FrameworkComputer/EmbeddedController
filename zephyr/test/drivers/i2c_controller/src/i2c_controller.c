/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include "basic_i2c_device_emul.h"
#include "i2c.h"
#include "test/drivers/test_state.h"

#include <zephyr/device.h>
#include <zephyr/fff.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/ztest.h>

struct i2c_controller_fixture {
	int port;
	int addr;
	const struct emul *emul;
	struct basic_i2c_device_data *emul_data;
};

ZTEST_F(i2c_controller, test_write_read32_le)
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

ZTEST_F(i2c_controller, test_write_read32_be)
{
	uint32_t expected = 0xAABBCCDD;
	uint32_t actual;

	zassert_ok(i2c_write32(fixture->port,
			       fixture->addr | I2C_FLAG_BIG_ENDIAN, 0,
			       expected));

	/* Get the first four bytes of the register space as a uint32_t */
	actual = BSWAP_32(*((uint32_t *)&fixture->emul_data->regs[0]));

	zassert_equal(expected, actual, "got %08x, expected %08x", actual,
		      expected);

	/* Now read back through I2C API */
	zassert_ok(i2c_read32(fixture->port,
			      fixture->addr | I2C_FLAG_BIG_ENDIAN, 0, &actual));

	zassert_equal(expected, actual, "got %08x, expected %08x", actual,
		      expected);
}

ZTEST_F(i2c_controller, test_write_read16_be)
{
	uint16_t expected = 0x1122;
	int actual;

	zassert_ok(i2c_write16(fixture->port,
			       fixture->addr | I2C_FLAG_BIG_ENDIAN, 0,
			       expected));

	/* Get the first two bytes of the register space as a uint16_t */
	actual = BSWAP_16(*((uint16_t *)&fixture->emul_data->regs[0]));

	zassert_equal(expected, actual, "got %04x, expected %08x", actual,
		      expected);

	/* Now read back through I2C API */
	zassert_ok(i2c_read16(fixture->port,
			      fixture->addr | I2C_FLAG_BIG_ENDIAN, 0, &actual));

	zassert_equal(expected, actual, "got %04x, expected %04x",
		      (uint16_t)actual, expected);
}

ZTEST_F(i2c_controller, test_read32_fail)
{
	int ret;
	uint32_t data;

	/* Fail by reading from wrong address */
	ret = i2c_read32(fixture->port, fixture->addr + 1, 0, &data);

	zassert_equal(EC_ERROR_INVAL, ret, "Got %d", ret);
}

ZTEST_F(i2c_controller, test_write32_fail)
{
	int ret;

	/* Fail by writing to wrong address */
	ret = i2c_write32(fixture->port, fixture->addr + 1, 0, 0x00000000);

	zassert_equal(EC_ERROR_INVAL, ret, "Got %d", ret);
}

ZTEST_F(i2c_controller, test_field_update16)
{
	/* Write a 16-bit value with mask */

	uint16_t mask = 0xFFFF;
	uint16_t set_value = 0x1234;
	uint16_t actual;

	zassert_ok(i2c_field_update16(fixture->port, fixture->addr, 0, mask,
				      set_value));

	/* Get the first two bytes of the register space as a uint16_t */
	actual = *((uint16_t *)&fixture->emul_data->regs[0]);

	zassert_equal(set_value, actual, "got %04x, expected %04x", actual,
		      set_value);

	/* Force a failure */
	set_value = 0x0001;
	mask = 0x0001;
	i2c_common_emul_set_read_fail_reg(&fixture->emul_data->common, 0);
	zassert_equal(i2c_field_update16(fixture->port, fixture->addr, 0, mask,
					 set_value),
		      EC_ERROR_INVAL);
}

ZTEST_F(i2c_controller, test_read_offset16__one_byte)
{
	/* Read 1 byte from a 16-bit register offset, which will cause us to
	 * access the extended register space of our i2c device
	 */
	int data = 0;
	uint16_t reg = 0xFF01;
	uint8_t expected = 0xAB;

	fixture->emul_data->extended_regs[reg & 0xFF] = expected;

	zassert_ok(
		i2c_read_offset16(fixture->port, fixture->addr, reg, &data, 1));

	zassert_equal(expected, data, "got %02x, expected %02x", data,
		      expected);
}

ZTEST_F(i2c_controller, test_read_offset16__two_bytes)
{
	/* Read 2 bytes from a 16-bit register offset, which will cause us to
	 * access the extended register space of our i2c device
	 */
	int data = 0;
	uint16_t reg = 0xFF01;
	uint16_t expected = 0xABCD;

	*((uint16_t *)&fixture->emul_data->extended_regs[reg & 0xFF]) =
		expected;

	zassert_ok(
		i2c_read_offset16(fixture->port, fixture->addr, reg, &data, 2));

	zassert_equal(expected, data, "got %04x, expected %04x", data,
		      expected);
}

ZTEST_F(i2c_controller, test_read_offset16__two_bytes_be)
{
	/* Read 2 bytes from a 16-bit register offset, which will cause us to
	 * access the extended register space of our i2c device
	 */
	int data = 0;
	uint16_t reg = 0xFF01;
	uint16_t expected = 0xABCD;

	*((uint16_t *)&fixture->emul_data->extended_regs[reg & 0xFF]) =
		expected;

	zassert_ok(i2c_read_offset16(fixture->port,
				     fixture->addr | I2C_FLAG_BIG_ENDIAN, reg,
				     &data, 2));

	uint16_t data16 = BSWAP_16((uint16_t)data);

	zassert_equal(expected, data16, "got %04x, expected %04x", data16,
		      expected);
}

ZTEST_F(i2c_controller, test_read_offset16__invalid)
{
	/* Check length limits */
	zassert_ok(
		!i2c_read_offset16(fixture->port, fixture->addr, 0, NULL, 3));
	zassert_ok(
		!i2c_read_offset16(fixture->port, fixture->addr, 0, NULL, -1));

	/* Failed intermediate write due to bad address */
	zassert_ok(!i2c_read_offset16(fixture->port, fixture->addr + 1, 0, NULL,
				      1));
}

ZTEST_F(i2c_controller, test_write_offset16__one_byte)
{
	/* Write 1 byte to a 16-bit register offset, which will cause us to
	 * access the extended register space of our i2c device
	 */
	int expected = 0xAB;
	uint16_t reg = 0xFF01;
	uint8_t actual;

	zassert_ok(i2c_write_offset16(fixture->port, fixture->addr, reg,
				      expected, 1));

	actual = fixture->emul_data->extended_regs[reg & 0xFF];

	zassert_equal(expected, actual, "got %02x, expected %02x", actual,
		      expected);
}

ZTEST_F(i2c_controller, test_write_offset16__two_bytes)
{
	/* Write 2 bytes to a 16-bit register offset, which will cause us to
	 * access the extended register space of our i2c device
	 */
	int expected = 0xABCD;
	uint16_t reg = 0xFF01;
	uint16_t actual;

	zassert_ok(i2c_write_offset16(fixture->port, fixture->addr, reg,
				      expected, 2));

	actual = *((uint16_t *)&fixture->emul_data->extended_regs[reg & 0xFF]);

	zassert_equal(expected, actual, "got %04x, expected %04x", actual,
		      expected);
}

ZTEST_F(i2c_controller, test_write_offset16__two_bytes_be)
{
	/* Write 2 bytes to a 16-bit register offset, which will cause us to
	 * access the extended register space of our i2c device
	 */
	int expected = 0xABCD;
	uint16_t reg = 0xFF01;
	uint16_t actual;

	zassert_ok(i2c_write_offset16(fixture->port,
				      fixture->addr | I2C_FLAG_BIG_ENDIAN, reg,
				      expected, 2));

	actual = BSWAP_16(
		*((uint16_t *)&fixture->emul_data->extended_regs[reg & 0xFF]));

	zassert_equal(expected, actual, "got %04x, expected %04x", actual,
		      expected);
}

ZTEST_F(i2c_controller, test_write_offset16__invalid)
{
	/* Check length limits */
	zassert_ok(!i2c_write_offset16(fixture->port, fixture->addr, 0, 0, 3));
	zassert_ok(!i2c_write_offset16(fixture->port, fixture->addr, 0, 0, -1));
}

ZTEST_F(i2c_controller, test_read_offset16_block)
{
	/* Read 4 bytes from a 16-bit register offset, which will cause us to
	 * access the extended register space of our i2c device
	 */
	uint32_t data;
	uint16_t reg = 0xFF01;
	uint32_t expected = 0xAABBCCDD;

	*((uint32_t *)&fixture->emul_data->extended_regs[reg & 0xFF]) =
		expected;

	zassert_ok(i2c_read_offset16_block(fixture->port, fixture->addr, reg,
					   (uint8_t *)&data, sizeof(data)));

	zassert_equal(expected, data, "got %08x, expected %08x", data,
		      expected);
}

ZTEST_F(i2c_controller, test_write_offset16_block)
{
	/* Write 4 bytes to a 16-bit register offset, which will cause us to
	 * access the extended register space of our i2c device
	 */
	uint32_t expected = 0xAABBCCDD;
	uint16_t reg = 0xFF01;
	uint32_t actual;

	zassert_ok(i2c_write_offset16_block(fixture->port, fixture->addr, reg,
					    (uint8_t *)&expected,
					    sizeof(expected)));

	actual = *((uint32_t *)&fixture->emul_data->extended_regs[reg & 0xFF]);

	zassert_equal(expected, actual, "got %08x, expected %08x", actual,
		      expected);
}

ZTEST_F(i2c_controller, test_pec_disabled)
{
	uint16_t addr_flags;
	uint8_t write_data[] = {
		0xAA,
		0xBB,
		0xCC,
		0xDD,
	};
	int write_data32 = 0x11223344;
	uint8_t read_data[4];
	int actual_read_len;
	uint16_t reg = 0x01;

	/*
	 * Verify I2C reads and writes through the various APIs fail when
	 * CONFIG_PLATFORM_EC_SMBUS_PEC=n
	 */
	if (IS_ENABLED(CONFIG_PLATFORM_EC_SMBUS_PEC)) {
		ztest_test_skip();
		return;
	}

	addr_flags = fixture->addr | I2C_FLAG_PEC;

	zassert_equal(i2c_read32(fixture->port, addr_flags, reg,
				 (int *)read_data),
		      EC_ERROR_UNIMPLEMENTED);
	zassert_equal(i2c_write32(fixture->port, addr_flags, reg, write_data32),
		      EC_ERROR_UNIMPLEMENTED);
	zassert_equal(i2c_read_sized_block(fixture->port, addr_flags, reg,
					   read_data, sizeof(read_data),
					   &actual_read_len),
		      EC_ERROR_UNIMPLEMENTED);
	zassert_equal(i2c_read_sized_block(fixture->port, addr_flags, reg,
					   read_data, 0, &actual_read_len),
		      EC_ERROR_INVAL);
	zassert_equal(i2c_write_block(fixture->port, addr_flags, reg,
				      write_data, sizeof(write_data)),
		      EC_ERROR_UNIMPLEMENTED);
}

ZTEST_F(i2c_controller, test_i2c_xfer_unlocked__error_paths)
{
	uint8_t out_buffer[1];
	int out_size;
	uint8_t in_buffer[1];
	int in_size;
	int flags;

	/* First confirm i2c_xfer_unlocked() works with a lock. */
	out_buffer[0] = 0;
	out_size = 1;
	in_size = 1;
	flags = I2C_XFER_STOP;

	i2c_lock(fixture->port, 1);
	zassert_equal(i2c_port_is_locked(fixture->port), 1,
		      "Port %d not locked", fixture->port);
	zassert_ok(i2c_xfer_unlocked(fixture->port, fixture->addr, out_buffer,
				     out_size, in_buffer, in_size, flags));
	i2c_lock(fixture->port, 0);
	zassert_equal(i2c_port_is_locked(fixture->port), 0, "Port %d is locked",
		      fixture->port);

	/* Try the transfer without a lock. */
	zassert_equal(i2c_xfer_unlocked(fixture->port, fixture->addr,
					out_buffer, out_size, in_buffer,
					in_size, flags),
		      EC_ERROR_INVAL);

	/* Set an invalid flag on the transfer, expected to still pass */
	i2c_lock(fixture->port, 1);
	zassert_equal(i2c_port_is_locked(fixture->port), 1,
		      "Port %d not locked", fixture->port);
	zassert_ok(i2c_xfer_unlocked(
		fixture->port, fixture->addr | I2C_FLAG_ADDR_IS_SPI, out_buffer,
		out_size, in_buffer, in_size, flags));
	i2c_lock(fixture->port, 0);
	zassert_equal(i2c_port_is_locked(fixture->port), 0, "Port %d is locked",
		      fixture->port);
}

static struct i2c_controller_fixture i2c_controller_fixture;

static void *setup(void)
{
	const struct emul *emul = EMUL_DT_GET(DT_NODELABEL(basic_i2c_device));
	struct basic_i2c_device_data *emul_data =
		(struct basic_i2c_device_data *)emul->data;

	i2c_controller_fixture.port =
		I2C_PORT_BY_DEV(DT_NODELABEL(basic_i2c_device));
	i2c_controller_fixture.addr = emul_data->common.cfg->addr;
	i2c_controller_fixture.emul = emul;
	i2c_controller_fixture.emul_data = emul_data;

	return &i2c_controller_fixture;
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
