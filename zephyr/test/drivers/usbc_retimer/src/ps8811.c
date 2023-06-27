/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "driver/retimer/ps8811.h"
#include "emul/retimer/emul_ps8811.h"
#include "i2c.h"
#include "usb_mux.h"

#include <zephyr/drivers/emul.h>
#include <zephyr/ztest.h>

#define PS811_EMUL EMUL_DT_GET(DT_NODELABEL(ps8811_emul))

static struct usb_mux mux = {
	.i2c_port = I2C_PORT_NODELABEL(i2c3),
	.i2c_addr_flags = PS8811_I2C_ADDR_FLAGS3,
};

ZTEST_SUITE(ps8811, NULL, NULL, NULL, NULL, NULL);

/* Helper functions to make tests clearer. */
static int ps8811_emul_test_read0(int reg, int *val)
{
	return ps8811_i2c_read(&mux, PS8811_REG_PAGE0, reg, val);
}

static int ps8811_emul_test_write0(int reg, int val)
{
	return ps8811_i2c_write(&mux, PS8811_REG_PAGE0, reg, val);
}

static int ps8811_emul_test_update0(int reg, uint8_t mask, uint8_t val)
{
	return ps8811_i2c_field_update(&mux, PS8811_REG_PAGE0, reg, mask, val);
}

static int ps8811_emul_test_read1(int reg, int *val)
{
	return ps8811_i2c_read(&mux, PS8811_REG_PAGE1, reg, val);
}

static int ps8811_emul_test_write1(int reg, int val)
{
	return ps8811_i2c_write(&mux, PS8811_REG_PAGE1, reg, val);
}

static int ps8811_emul_test_update1(int reg, uint8_t mask, uint8_t val)
{
	return ps8811_i2c_field_update(&mux, PS8811_REG_PAGE1, reg, mask, val);
}

static int ps8811_emul_test_get_reg1(int reg, uint8_t *val)
{
	return ps8811_emul_get_reg1(PS811_EMUL, reg, val);
}

static int ps8811_emul_test_set_reg1(int reg, uint8_t val)
{
	return ps8811_emul_set_reg1(PS811_EMUL, reg, val);
}

/* Verify that the reset values for all registers are correct. */
ZTEST(ps8811, test_emul_reset)
{
	uint8_t val;
	int rv;

	rv = ps8811_emul_test_get_reg1(PS8811_REG1_USB_AEQ_LEVEL, &val);
	zexpect_ok(rv);
	zexpect_equal(val, PS8811_REG1_USB_AEQ_LEVEL_DEFAULT);

	rv = ps8811_emul_test_get_reg1(PS8811_REG1_USB_ADE_CONFIG, &val);
	zexpect_ok(rv);
	zexpect_equal(val, PS8811_REG1_USB_ADE_CONFIG_DEFAULT);

	rv = ps8811_emul_test_get_reg1(PS8811_REG1_USB_BEQ_LEVEL, &val);
	zexpect_ok(rv);
	zexpect_equal(val, PS8811_REG1_USB_BEQ_LEVEL_DEFAULT);

	rv = ps8811_emul_test_get_reg1(PS8811_REG1_USB_BDE_CONFIG, &val);
	zexpect_ok(rv);
	zexpect_equal(val, PS8811_REG1_USB_BDE_CONFIG_DEFAULT);

	rv = ps8811_emul_test_get_reg1(PS8811_REG1_USB_CHAN_A_SWING, &val);
	zexpect_ok(rv);
	zexpect_equal(val, PS8811_REG1_USB_CHAN_A_SWING_DEFAULT);

	rv = ps8811_emul_test_get_reg1(PS8811_REG1_50OHM_ADJUST_CHAN_B, &val);
	zexpect_ok(rv);
	zexpect_equal(val, PS8811_REG1_50OHM_ADJUST_CHAN_B_DEFAULT);

	rv = ps8811_emul_test_get_reg1(PS8811_REG1_USB_CHAN_B_SWING, &val);
	zexpect_ok(rv);
	zexpect_equal(val, PS8811_REG1_USB_CHAN_B_SWING_DEFAULT);

	rv = ps8811_emul_test_get_reg1(PS8811_REG1_USB_CHAN_B_DE_PS_LSB, &val);
	zexpect_ok(rv);
	zexpect_equal(val, PS8811_REG1_USB_CHAN_B_DE_PS_LSB_DEFAULT);

	ps8811_emul_test_get_reg1(PS8811_REG1_USB_CHAN_B_DE_PS_MSB, &val);
	zexpect_ok(rv);
	zexpect_equal(val, PS8811_REG1_USB_CHAN_B_DE_PS_MSB_DEFAULT);
}

/*
 * P0 registers aren't currently implemented,
 * ensure access results in an error.
 */
ZTEST(ps8811, test_emul_page0_registers)
{
	int rv;
	int val;

	rv = ps8811_emul_test_write0(0, 0);
	zexpect_not_equal(rv, EC_SUCCESS);
	rv = ps8811_emul_test_read0(0, &val);
	zexpect_not_equal(rv, EC_SUCCESS);
	rv = ps8811_emul_test_update0(0, 0xff, 0xff);
	zexpect_not_equal(rv, EC_SUCCESS);
}

/*
 * Verify that writing/reading all our registers through I2C works. But
 * don't attempt to verify errors when writing reserved bits.
 */
ZTEST(ps8811, test_emul_page1_registers_rw)
{
	int val;
	int rv;
	uint8_t expected;

	expected = 0xff;
	rv = ps8811_emul_test_write1(PS8811_REG1_USB_AEQ_LEVEL, expected);
	zexpect_ok(rv, EC_SUCCESS);
	rv = ps8811_emul_test_read1(PS8811_REG1_USB_AEQ_LEVEL, &val);
	zexpect_ok(rv);
	zexpect_equal(val, expected);

	expected = 0xff;
	rv = ps8811_emul_test_write1(PS8811_REG1_USB_ADE_CONFIG, expected);
	zexpect_ok(rv);
	rv = ps8811_emul_test_read1(PS8811_REG1_USB_ADE_CONFIG, &val);
	zexpect_ok(rv);
	zexpect_equal(val, expected);

	expected = 0xff;
	rv = ps8811_emul_test_write1(PS8811_REG1_USB_BEQ_LEVEL, expected);
	zexpect_ok(rv);
	rv = ps8811_emul_test_read1(PS8811_REG1_USB_BEQ_LEVEL, &val);
	zexpect_ok(rv);
	zexpect_equal(val, expected);

	expected = 0xff;
	rv = ps8811_emul_test_write1(PS8811_REG1_USB_BDE_CONFIG, expected);
	zexpect_ok(rv);
	rv = ps8811_emul_test_read1(PS8811_REG1_USB_BDE_CONFIG, &val);
	zexpect_ok(rv);
	zexpect_equal(val, expected);

	expected = (uint8_t)(PS8811_REG1_USB_CHAN_A_SWING_RESERVED_MASK &
			     PS8811_REG1_USB_CHAN_A_SWING_DEFAULT);
	expected |= (uint8_t)(~PS8811_REG1_USB_CHAN_A_SWING_RESERVED_MASK);
	rv = ps8811_emul_test_write1(PS8811_REG1_USB_CHAN_A_SWING, expected);
	zexpect_ok(rv);
	rv = ps8811_emul_test_read1(PS8811_REG1_USB_CHAN_A_SWING, &val);
	zexpect_ok(rv);
	zexpect_equal(val, expected);

	expected = (uint8_t)(PS8811_REG1_50OHM_ADJUST_CHAN_B_RESERVED_MASK &
			     PS8811_REG1_50OHM_ADJUST_CHAN_B_DEFAULT);
	expected |= (uint8_t)(~PS8811_REG1_50OHM_ADJUST_CHAN_B_RESERVED_MASK);
	rv = ps8811_emul_test_write1(PS8811_REG1_50OHM_ADJUST_CHAN_B, expected);
	zexpect_ok(rv);
	rv = ps8811_emul_test_read1(PS8811_REG1_50OHM_ADJUST_CHAN_B, &val);
	zexpect_ok(rv);
	zexpect_equal(val, expected);

	expected = (uint8_t)(PS8811_REG1_USB_CHAN_B_SWING_RESERVED_MASK &
			     PS8811_REG1_USB_CHAN_B_SWING_DEFAULT);
	expected |= (uint8_t)(~PS8811_REG1_USB_CHAN_B_SWING_RESERVED_MASK);
	rv = ps8811_emul_test_write1(PS8811_REG1_USB_CHAN_B_SWING, expected);
	zexpect_ok(rv);
	rv = ps8811_emul_test_read1(PS8811_REG1_USB_CHAN_B_SWING, &val);
	zexpect_ok(rv);
	zexpect_equal(val, expected);

	expected = (uint8_t)(PS8811_REG1_USB_CHAN_B_DE_PS_LSB_RESERVED_MASK &
			     PS8811_REG1_USB_CHAN_B_DE_PS_LSB_DEFAULT);
	expected |= (uint8_t)(~PS8811_REG1_USB_CHAN_B_DE_PS_LSB_RESERVED_MASK);
	rv = ps8811_emul_test_write1(PS8811_REG1_USB_CHAN_B_DE_PS_LSB,
				     expected);
	zexpect_ok(rv);
	rv = ps8811_emul_test_read1(PS8811_REG1_USB_CHAN_B_DE_PS_LSB, &val);
	zexpect_ok(rv);
	zexpect_equal(val, expected);

	expected = (uint8_t)(PS8811_REG1_USB_CHAN_B_DE_PS_MSB_RESERVED_MASK &
			     PS8811_REG1_USB_CHAN_B_DE_PS_MSB_DEFAULT);
	expected |= (uint8_t)(~PS8811_REG1_USB_CHAN_B_DE_PS_MSB_RESERVED_MASK);
	rv = ps8811_emul_test_write1(PS8811_REG1_USB_CHAN_B_DE_PS_MSB,
				     expected);
	zexpect_ok(rv);
	rv = ps8811_emul_test_read1(PS8811_REG1_USB_CHAN_B_DE_PS_MSB, &val);
	zexpect_ok(rv);
	zexpect_equal(val, expected);

	/* Verify that accessing a non-existent register fails. */
	rv = ps8811_emul_test_write1(0xff, 0xff);
	zexpect_not_equal(rv, EC_SUCCESS);
	rv = ps8811_emul_test_read1(0xff, &val);
	zexpect_not_equal(rv, EC_SUCCESS);
}

/* Verify that I2C register updates work. */
ZTEST(ps8811, test_emul_page1_registers_update)
{
	int rv;
	uint8_t val;

	rv = ps8811_emul_test_set_reg1(PS8811_REG1_USB_AEQ_LEVEL, 0xff);
	zexpect_ok(rv);
	rv = ps8811_emul_test_update1(PS8811_REG1_USB_AEQ_LEVEL, 0x0f, 0x00);
	zexpect_ok(rv);
	rv = ps8811_emul_test_get_reg1(PS8811_REG1_USB_AEQ_LEVEL, &val);
	zexpect_ok(rv);
	zexpect_equal(val, 0xf0);

	rv = ps8811_emul_test_set_reg1(PS8811_REG1_USB_ADE_CONFIG, 0xff);
	zexpect_ok(rv);
	rv = ps8811_emul_test_update1(PS8811_REG1_USB_ADE_CONFIG, 0x0f, 0x00);
	zexpect_ok(rv);
	rv = ps8811_emul_test_get_reg1(PS8811_REG1_USB_ADE_CONFIG, &val);
	zexpect_ok(rv);
	zexpect_equal(val, 0xf0);

	rv = ps8811_emul_test_set_reg1(PS8811_REG1_USB_BEQ_LEVEL, 0xff);
	zexpect_ok(rv);
	rv = ps8811_emul_test_update1(PS8811_REG1_USB_BEQ_LEVEL, 0x0f, 0x00);
	zexpect_ok(rv);
	rv = ps8811_emul_test_get_reg1(PS8811_REG1_USB_BEQ_LEVEL, &val);
	zexpect_ok(rv);
	zexpect_equal(val, 0xf0);

	rv = ps8811_emul_test_set_reg1(PS8811_REG1_USB_BDE_CONFIG, 0xff);
	zexpect_ok(rv);
	rv = ps8811_emul_test_update1(PS8811_REG1_USB_BDE_CONFIG, 0x0f, 0x0);
	zexpect_ok(rv);
	rv = ps8811_emul_test_get_reg1(PS8811_REG1_USB_BDE_CONFIG, &val);
	zexpect_equal(val, 0xf0);

	const uint8_t a_swing_mask =
		(uint8_t)(~PS8811_REG1_USB_CHAN_A_SWING_RESERVED_MASK);
	rv = ps8811_emul_test_set_reg1(PS8811_REG1_USB_CHAN_A_SWING,
				       a_swing_mask);
	zexpect_ok(rv);
	rv = ps8811_emul_test_update1(PS8811_REG1_USB_CHAN_A_SWING,
				      a_swing_mask, 0x0);
	zexpect_ok(rv);
	rv = ps8811_emul_test_get_reg1(PS8811_REG1_USB_CHAN_A_SWING, &val);
	zexpect_equal(val, 0x00);

	const uint8_t adjust_b_50ohm_mask =
		(uint8_t)(~PS8811_REG1_50OHM_ADJUST_CHAN_B_RESERVED_MASK);
	rv = ps8811_emul_test_set_reg1(PS8811_REG1_50OHM_ADJUST_CHAN_B,
				       adjust_b_50ohm_mask);
	zexpect_ok(rv);
	rv = ps8811_emul_test_update1(PS8811_REG1_50OHM_ADJUST_CHAN_B,
				      adjust_b_50ohm_mask, 0x0);
	zexpect_ok(rv);
	rv = ps8811_emul_test_get_reg1(PS8811_REG1_50OHM_ADJUST_CHAN_B, &val);
	zexpect_equal(val, 0x00);

	const uint8_t b_swing_mask =
		(uint8_t)(~PS8811_REG1_USB_CHAN_B_SWING_RESERVED_MASK);
	rv = ps8811_emul_test_set_reg1(PS8811_REG1_USB_CHAN_B_SWING,
				       b_swing_mask);
	zexpect_ok(rv);
	rv = ps8811_emul_test_update1(PS8811_REG1_USB_CHAN_B_SWING,
				      b_swing_mask, 0x0);
	zexpect_ok(rv);
	rv = ps8811_emul_test_get_reg1(PS8811_REG1_USB_CHAN_B_SWING, &val);
	zexpect_equal(val, 0x00);

	/* This register has its highest reserved bit set by default. */
	const uint8_t b_de_ps_lsb_mask =
		(uint8_t)(~PS8811_REG1_USB_CHAN_B_DE_PS_LSB_RESERVED_MASK) |
		0x80;
	rv = ps8811_emul_test_set_reg1(PS8811_REG1_USB_CHAN_B_DE_PS_LSB,
				       b_de_ps_lsb_mask);
	zexpect_ok(rv);
	rv = ps8811_emul_test_update1(PS8811_REG1_USB_CHAN_B_DE_PS_LSB,
				      b_de_ps_lsb_mask, 0x80);
	zexpect_ok(rv);
	rv = ps8811_emul_test_get_reg1(PS8811_REG1_USB_CHAN_B_DE_PS_LSB, &val);
	zexpect_equal(val, 0x80);

	const uint8_t b_de_ps_msb_mask =
		(uint8_t)(~PS8811_REG1_USB_CHAN_B_DE_PS_MSB_RESERVED_MASK);
	rv = ps8811_emul_test_set_reg1(PS8811_REG1_USB_CHAN_B_DE_PS_MSB,
				       b_de_ps_msb_mask);
	zexpect_ok(rv);
	rv = ps8811_emul_test_update1(PS8811_REG1_USB_CHAN_B_DE_PS_MSB,
				      b_de_ps_msb_mask, 0x0);
	zexpect_ok(rv);
	rv = ps8811_emul_test_get_reg1(PS8811_REG1_USB_CHAN_B_DE_PS_MSB, &val);
	zexpect_equal(val, 0x00);

	/* Verify that updating a non-existent register fails. */
	rv = ps8811_emul_test_update1(0xff, 0xff, 0xff);
	zexpect_not_equal(rv, EC_SUCCESS);
}

/* Verify that writing to reserved bits results in an error. */
ZTEST(ps8811, test_emul_reserved)
{
	int rv;

	rv = ps8811_emul_test_write1(
		PS8811_REG1_USB_CHAN_A_SWING,
		PS8811_REG1_USB_CHAN_A_SWING_RESERVED_MASK);
	zexpect_not_equal(rv, EC_SUCCESS);

	rv = ps8811_emul_test_write1(
		PS8811_REG1_50OHM_ADJUST_CHAN_B,
		PS8811_REG1_50OHM_ADJUST_CHAN_B_RESERVED_MASK);
	zexpect_not_equal(rv, EC_SUCCESS);

	rv = ps8811_emul_test_write1(
		PS8811_REG1_USB_CHAN_B_SWING,
		PS8811_REG1_USB_CHAN_B_SWING_RESERVED_MASK);
	zexpect_not_equal(rv, EC_SUCCESS);

	rv = ps8811_emul_test_write1(
		PS8811_REG1_USB_CHAN_B_DE_PS_LSB,
		PS8811_REG1_USB_CHAN_B_DE_PS_LSB_RESERVED_MASK);
	zexpect_not_equal(rv, EC_SUCCESS);

	rv = ps8811_emul_test_write1(
		PS8811_REG1_USB_CHAN_B_DE_PS_MSB,
		PS8811_REG1_USB_CHAN_B_DE_PS_MSB_RESERVED_MASK);
	zexpect_not_equal(rv, EC_SUCCESS);
}
