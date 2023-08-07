/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "driver/retimer/anx7483.h"
#include "driver/retimer/anx7483_public.h"
#include "emul/retimer/emul_anx7483.h"
#include "i2c.h"
#include "power.h"
#include "usb_mux.h"

#include <zephyr/drivers/emul.h>
#include <zephyr/ztest.h>

#define ANX7483_EMUL EMUL_DT_GET(DT_NODELABEL(anx7483_emul))

int anx7483_init(const struct usb_mux *me);
int anx7483_set(const struct usb_mux *me, mux_state_t mux_state,
		bool *ack_required);
int anx7483_get(const struct usb_mux *me, mux_state_t *mux_state);
int anx7483_read(const struct usb_mux *me, uint8_t reg, int *val);
int anx7483_write(const struct usb_mux *me, uint8_t reg, uint8_t val);

extern const struct anx7483_tuning_set anx7483_usb_enabled[];
extern const struct anx7483_tuning_set anx7483_dp_enabled[];
extern const struct anx7483_tuning_set anx7483_dock_noflip[];
extern const struct anx7483_tuning_set anx7483_dock_flip[];

extern const size_t anx7483_usb_enabled_count;
extern const size_t anx7483_dp_enabled_count;
extern const size_t anx7483_dock_noflip_count;
extern const size_t anx7483_dock_flip_count;

static struct usb_mux mux = {
	.i2c_port = I2C_PORT_NODELABEL(i2c3),
	.i2c_addr_flags = 0x3e,
};

/* Helper functions to make tests clearer. */
static int anx7483_emul_test_get_reg(int reg, uint8_t *val)
{
	return anx7483_emul_get_reg(ANX7483_EMUL, reg, val);
}

static int anx7483_emul_test_set_mask(int reg, uint8_t val, uint8_t def)
{
	return anx7483_emul_set_reg_reserved_mask(ANX7483_EMUL, reg, val, def);
}

static int anx7483_emul_test_set_reg(int reg, uint8_t val)
{
	return anx7483_emul_set_reg(ANX7483_EMUL, reg, val);
}

static int anx7483_emul_test_get_eq(enum anx7483_tune_pin pin,
				    enum anx7483_eq_setting *eq)
{
	return anx7483_emul_get_eq(ANX7483_EMUL, pin, eq);
}

static int anx7483_emul_test_get_fg(enum anx7483_tune_pin pin,
				    enum anx7483_fg_setting *fg)
{
	return anx7483_emul_get_fg(ANX7483_EMUL, pin, fg);
}

static int anx7483_test_set_eq(enum anx7483_tune_pin pin,
			       enum anx7483_eq_setting eq)
{
	return anx7483_set_eq(&mux, pin, eq);
}

static int anx7483_i2c_read(int reg, int *data)
{
	return anx7483_read(&mux, reg, data);
}

static int anx7483_i2c_write(int reg, int data)
{
	return anx7483_write(&mux, reg, data);
}

static void anx7483_before(void *fixture)
{
	ARG_UNUSED(fixture);

	/* Ensure the ANX7483 is on. */
	power_set_state(POWER_S0);
}

ZTEST_SUITE(anx7483, NULL, NULL, anx7483_before, NULL, NULL);

/* Verify that the reset values for all registers are correct. */
ZTEST(anx7483, test_emul_reset)
{
	uint8_t val;
	int rv;

	rv = anx7483_emul_test_get_reg(ANX7483_LFPS_TIMER_REG, &val);
	zexpect_ok(rv);
	zexpect_equal(val, ANX7483_LFPS_TIMER_REG_DEFAULT);

	rv = anx7483_emul_test_get_reg(ANX7483_ANALOG_STATUS_CTRL_REG, &val);
	zexpect_ok(rv);
	zexpect_equal(val, ANX7483_ANALOG_STATUS_CTRL_REG_DEFAULT);

	rv = anx7483_emul_test_get_reg(ANX7483_ENABLE_EQ_FLAT_SWING_REG, &val);
	zexpect_ok(rv);
	zexpect_equal(val, ANX7483_ENABLE_EQ_FLAT_SWING_REG_DEFAULT);

	rv = anx7483_emul_test_get_reg(ANX7483_AUX_SNOOPING_CTRL_REG, &val);
	zexpect_ok(rv);
	zexpect_equal(val, ANX7483_AUX_SNOOPING_CTRL_REG_DEFAULT);

	/* CFG0 */
	rv = anx7483_emul_test_get_reg(ANX7483_UTX1_PORT_CFG0_REG, &val);
	zexpect_ok(rv);
	zexpect_equal(val, ANX7483_UTX1_PORT_CFG0_REG_DEFAULT);

	rv = anx7483_emul_test_get_reg(ANX7483_UTX2_PORT_CFG0_REG, &val);
	zexpect_ok(rv);
	zexpect_equal(val, ANX7483_UTX2_PORT_CFG0_REG_DEFAULT);

	rv = anx7483_emul_test_get_reg(ANX7483_URX1_PORT_CFG0_REG, &val);
	zexpect_ok(rv);
	zexpect_equal(val, ANX7483_URX1_PORT_CFG0_REG_DEFAULT);

	rv = anx7483_emul_test_get_reg(ANX7483_URX2_PORT_CFG0_REG, &val);
	zexpect_ok(rv);
	zexpect_equal(val, ANX7483_URX2_PORT_CFG0_REG_DEFAULT);

	rv = anx7483_emul_test_get_reg(ANX7483_DRX1_PORT_CFG0_REG, &val);
	zexpect_ok(rv);
	zexpect_equal(val, ANX7483_DRX1_PORT_CFG0_REG_DEFAULT);

	rv = anx7483_emul_test_get_reg(ANX7483_DRX2_PORT_CFG0_REG, &val);
	zexpect_ok(rv);
	zexpect_equal(val, ANX7483_DRX2_PORT_CFG0_REG_DEFAULT);

	/* CFG1 */
	rv = anx7483_emul_test_get_reg(ANX7483_UTX1_PORT_CFG1_REG, &val);
	zexpect_ok(rv);
	zexpect_equal(val, ANX7483_UTX1_PORT_CFG1_REG_DEFAULT);

	rv = anx7483_emul_test_get_reg(ANX7483_UTX2_PORT_CFG1_REG, &val);
	zexpect_ok(rv);
	zexpect_equal(val, ANX7483_UTX2_PORT_CFG1_REG_DEFAULT);

	rv = anx7483_emul_test_get_reg(ANX7483_URX1_PORT_CFG1_REG, &val);
	zexpect_ok(rv);
	zexpect_equal(val, ANX7483_URX1_PORT_CFG1_REG_DEFAULT);

	rv = anx7483_emul_test_get_reg(ANX7483_URX2_PORT_CFG1_REG, &val);
	zexpect_ok(rv);
	zexpect_equal(val, ANX7483_URX2_PORT_CFG1_REG_DEFAULT);

	rv = anx7483_emul_test_get_reg(ANX7483_DRX1_PORT_CFG1_REG, &val);
	zexpect_ok(rv);
	zexpect_equal(val, ANX7483_DRX1_PORT_CFG1_REG_DEFAULT);

	rv = anx7483_emul_test_get_reg(ANX7483_DRX2_PORT_CFG1_REG, &val);
	zexpect_ok(rv);
	zexpect_equal(val, ANX7483_DRX2_PORT_CFG1_REG_DEFAULT);

	/* CFG2 */
	rv = anx7483_emul_test_get_reg(ANX7483_UTX1_PORT_CFG2_REG, &val);
	zexpect_ok(rv);
	zexpect_equal(val, ANX7483_UTX1_PORT_CFG2_REG_DEFAULT);

	rv = anx7483_emul_test_get_reg(ANX7483_UTX2_PORT_CFG2_REG, &val);
	zexpect_ok(rv);
	zexpect_equal(val, ANX7483_UTX2_PORT_CFG2_REG_DEFAULT);

	rv = anx7483_emul_test_get_reg(ANX7483_URX1_PORT_CFG2_REG, &val);
	zexpect_ok(rv);
	zexpect_equal(val, ANX7483_URX1_PORT_CFG2_REG_DEFAULT);

	rv = anx7483_emul_test_get_reg(ANX7483_URX2_PORT_CFG2_REG, &val);
	zexpect_ok(rv);
	zexpect_equal(val, ANX7483_URX2_PORT_CFG2_REG_DEFAULT);

	rv = anx7483_emul_test_get_reg(ANX7483_DRX1_PORT_CFG2_REG, &val);
	zexpect_ok(rv);
	zexpect_equal(val, ANX7483_DRX1_PORT_CFG2_REG_DEFAULT);

	rv = anx7483_emul_test_get_reg(ANX7483_DRX2_PORT_CFG2_REG, &val);
	zexpect_ok(rv);
	zexpect_equal(val, ANX7483_DRX2_PORT_CFG2_REG_DEFAULT);

	/* CFG3 */
	rv = anx7483_emul_test_get_reg(ANX7483_UTX1_PORT_CFG3_REG, &val);
	zexpect_ok(rv);
	zexpect_equal(val, ANX7483_UTX1_PORT_CFG3_REG_DEFAULT);

	rv = anx7483_emul_test_get_reg(ANX7483_UTX2_PORT_CFG3_REG, &val);
	zexpect_ok(rv);
	zexpect_equal(val, ANX7483_UTX2_PORT_CFG3_REG_DEFAULT);

	rv = anx7483_emul_test_get_reg(ANX7483_URX1_PORT_CFG3_REG, &val);
	zexpect_ok(rv);
	zexpect_equal(val, ANX7483_URX1_PORT_CFG3_REG_DEFAULT);

	rv = anx7483_emul_test_get_reg(ANX7483_URX2_PORT_CFG3_REG, &val);
	zexpect_ok(rv);
	zexpect_equal(val, ANX7483_URX2_PORT_CFG3_REG_DEFAULT);

	rv = anx7483_emul_test_get_reg(ANX7483_DRX1_PORT_CFG3_REG, &val);
	zexpect_ok(rv);
	zexpect_equal(val, ANX7483_DRX1_PORT_CFG3_REG_DEFAULT);

	rv = anx7483_emul_test_get_reg(ANX7483_DRX2_PORT_CFG3_REG, &val);
	zexpect_ok(rv);
	zexpect_equal(val, ANX7483_DRX2_PORT_CFG3_REG_DEFAULT);

	/* CFG4 */
	rv = anx7483_emul_test_get_reg(ANX7483_UTX1_PORT_CFG4_REG, &val);
	zexpect_ok(rv);
	zexpect_equal(val, ANX7483_UTX1_PORT_CFG4_REG_DEFAULT);

	rv = anx7483_emul_test_get_reg(ANX7483_UTX2_PORT_CFG4_REG, &val);
	zexpect_ok(rv);
	zexpect_equal(val, ANX7483_UTX2_PORT_CFG4_REG_DEFAULT);

	rv = anx7483_emul_test_get_reg(ANX7483_URX1_PORT_CFG4_REG, &val);
	zexpect_ok(rv);
	zexpect_equal(val, ANX7483_URX1_PORT_CFG4_REG_DEFAULT);

	rv = anx7483_emul_test_get_reg(ANX7483_URX2_PORT_CFG4_REG, &val);
	zexpect_ok(rv);
	zexpect_equal(val, ANX7483_URX2_PORT_CFG4_REG_DEFAULT);

	rv = anx7483_emul_test_get_reg(ANX7483_DRX1_PORT_CFG4_REG, &val);
	zexpect_ok(rv);
	zexpect_equal(val, ANX7483_DRX1_PORT_CFG4_REG_DEFAULT);

	rv = anx7483_emul_test_get_reg(ANX7483_DRX2_PORT_CFG4_REG, &val);
	zexpect_ok(rv);
	zexpect_equal(val, ANX7483_DRX2_PORT_CFG4_REG_DEFAULT);
}

/* Test the ANX7483 driver's initialization function. */
ZTEST(anx7483, test_init)
{
	int rv;
	uint8_t val;

	rv = anx7483_init(&mux);
	zexpect_ok(rv);

	rv = anx7483_emul_test_get_reg(ANX7483_ANALOG_STATUS_CTRL_REG, &val);
	zexpect_ok(rv);
	zexpect_true(val & ANX7483_CTRL_REG_EN);
}

/*
 * Test the ANX7483 driver's anx7483_set_eq, function  which sets the
 * equalization for a pin.
 */

ZTEST(anx7483, test_set_eq_func)
{
	int rv;
	enum anx7483_eq_setting eq;

	rv = anx7483_test_set_eq(ANX7483_PIN_UTX1, ANX7483_EQ_SETTING_12_5DB);
	zexpect_ok(rv);
	rv = anx7483_emul_test_get_eq(ANX7483_PIN_UTX1, &eq);
	zexpect_ok(rv);
	zexpect_equal(eq, ANX7483_EQ_SETTING_12_5DB);

	rv = anx7483_test_set_eq(ANX7483_PIN_UTX2, ANX7483_EQ_SETTING_12_5DB);
	zexpect_ok(rv);
	rv = anx7483_emul_test_get_eq(ANX7483_PIN_UTX2, &eq);
	zexpect_ok(rv);
	zexpect_equal(eq, ANX7483_EQ_SETTING_12_5DB);

	rv = anx7483_test_set_eq(ANX7483_PIN_URX1, ANX7483_EQ_SETTING_12_5DB);
	zexpect_ok(rv);
	rv = anx7483_emul_test_get_eq(ANX7483_PIN_URX1, &eq);
	zexpect_ok(rv);
	zexpect_equal(eq, ANX7483_EQ_SETTING_12_5DB);

	rv = anx7483_test_set_eq(ANX7483_PIN_URX2, ANX7483_EQ_SETTING_12_5DB);
	zexpect_ok(rv);
	rv = anx7483_emul_test_get_eq(ANX7483_PIN_URX2, &eq);
	zexpect_ok(rv);
	zexpect_equal(eq, ANX7483_EQ_SETTING_12_5DB);

	rv = anx7483_test_set_eq(ANX7483_PIN_DRX1, ANX7483_EQ_SETTING_12_5DB);
	zexpect_ok(rv);
	rv = anx7483_emul_test_get_eq(ANX7483_PIN_DRX1, &eq);
	zexpect_ok(rv);
	zexpect_equal(eq, ANX7483_EQ_SETTING_12_5DB);

	rv = anx7483_test_set_eq(ANX7483_PIN_DRX2, ANX7483_EQ_SETTING_12_5DB);
	zexpect_ok(rv);
	rv = anx7483_emul_test_get_eq(ANX7483_PIN_DRX2, &eq);
	zexpect_ok(rv);
	zexpect_equal(eq, ANX7483_EQ_SETTING_12_5DB);

	/* Test invalid pin. */
	rv = anx7483_test_set_eq(0xff, ANX7483_EQ_SETTING_12_5DB);
	zexpect_not_equal(rv, 0);
}

/*
 * Test the ANX7483 driver's anx7483_set_fg, function  which sets the flat gain
 * for a pin.
 */
ZTEST(anx7483, test_set_fg)
{
	int rv;
	enum anx7483_fg_setting fg;

	rv = anx7483_set_fg(&mux, ANX7483_PIN_UTX1, ANX7483_FG_SETTING_1_2DB);
	zexpect_ok(rv);
	rv = anx7483_emul_test_get_fg(ANX7483_PIN_UTX1, &fg);
	zexpect_ok(rv);
	zexpect_equal(fg, ANX7483_FG_SETTING_1_2DB);

	rv = anx7483_set_fg(&mux, ANX7483_PIN_UTX2, ANX7483_FG_SETTING_1_2DB);
	zexpect_ok(rv);
	rv = anx7483_emul_test_get_fg(ANX7483_PIN_UTX2, &fg);
	zexpect_ok(rv);
	zexpect_equal(fg, ANX7483_FG_SETTING_1_2DB);

	rv = anx7483_set_fg(&mux, ANX7483_PIN_URX2, ANX7483_FG_SETTING_1_2DB);
	zexpect_ok(rv);
	rv = anx7483_emul_test_get_fg(ANX7483_PIN_URX2, &fg);
	zexpect_ok(rv);
	zexpect_equal(fg, ANX7483_FG_SETTING_1_2DB);

	rv = anx7483_set_fg(&mux, ANX7483_PIN_DRX1, ANX7483_FG_SETTING_1_2DB);
	zexpect_ok(rv);
	rv = anx7483_emul_test_get_fg(ANX7483_PIN_DRX1, &fg);
	zexpect_ok(rv);
	zexpect_equal(fg, ANX7483_FG_SETTING_1_2DB);

	rv = anx7483_set_fg(&mux, ANX7483_PIN_DRX2, ANX7483_FG_SETTING_1_2DB);
	zexpect_ok(rv);
	rv = anx7483_emul_test_get_fg(ANX7483_PIN_DRX2, &fg);
	zexpect_ok(rv);
	zexpect_equal(fg, ANX7483_FG_SETTING_1_2DB);

	/* Test invalid pin. */
	rv = anx7483_set_fg(&mux, 0xff, ANX7483_FG_SETTING_1_2DB);
	zexpect_not_equal(rv, 0);
}

/* Validate that accessing the emulator's registers through I2C works. */
ZTEST(anx7483, test_emul_registers_rw)
{
	int rv;
	uint8_t expected;
	int val;

	expected = (uint8_t)(ANX7483_LFPS_TIMER_REG_RESERVED_MASK &
			     ANX7483_LFPS_TIMER_REG_DEFAULT);
	expected |= (uint8_t)(~ANX7483_LFPS_TIMER_REG_RESERVED_MASK);
	rv = anx7483_i2c_write(ANX7483_LFPS_TIMER_REG, expected);
	zexpect_ok(rv);
	rv = anx7483_i2c_read(ANX7483_LFPS_TIMER_REG, &val);
	zexpect_equal(val, expected);

	expected = (uint8_t)(ANX7483_ANALOG_STATUS_CTRL_REG_RESERVED_MASK &
			     ANX7483_ANALOG_STATUS_CTRL_REG_DEFAULT);
	expected |= (uint8_t)(~ANX7483_ANALOG_STATUS_CTRL_REG_RESERVED_MASK);
	rv = anx7483_i2c_write(ANX7483_ANALOG_STATUS_CTRL_REG, expected);
	zexpect_ok(rv);
	rv = anx7483_i2c_read(ANX7483_ANALOG_STATUS_CTRL_REG, &val);
	zexpect_equal(val, expected);

	expected = (uint8_t)(ANX7483_ENABLE_EQ_FLAT_SWING_REG_RESERVED_MASK &
			     ANX7483_ENABLE_EQ_FLAT_SWING_REG_DEFAULT);
	expected |= (uint8_t)(~ANX7483_ENABLE_EQ_FLAT_SWING_REG_RESERVED_MASK);
	rv = anx7483_i2c_write(ANX7483_ENABLE_EQ_FLAT_SWING_REG, expected);
	zexpect_ok(rv);
	rv = anx7483_i2c_read(ANX7483_ENABLE_EQ_FLAT_SWING_REG, &val);
	zexpect_equal(val, expected);

	expected = (uint8_t)(ANX7483_AUX_SNOOPING_CTRL_REG_RESERVED_MASK &
			     ANX7483_AUX_SNOOPING_CTRL_REG_DEFAULT);
	expected |= (uint8_t)(~ANX7483_AUX_SNOOPING_CTRL_REG_RESERVED_MASK);
	rv = anx7483_i2c_write(ANX7483_AUX_SNOOPING_CTRL_REG, expected);
	zexpect_ok(rv);
	rv = anx7483_i2c_read(ANX7483_AUX_SNOOPING_CTRL_REG, &val);
	zexpect_equal(val, expected);

	/* CFG0 */
	expected = (uint8_t)(ANX7483_UTX1_PORT_CFG0_REG_RESERVED_MASK &
			     ANX7483_UTX1_PORT_CFG0_REG_DEFAULT);
	expected |= (uint8_t)(~ANX7483_UTX1_PORT_CFG0_REG_RESERVED_MASK);
	rv = anx7483_i2c_write(ANX7483_UTX1_PORT_CFG0_REG, expected);
	zexpect_ok(rv);
	rv = anx7483_i2c_read(ANX7483_UTX1_PORT_CFG0_REG, &val);
	zexpect_equal(val, expected);

	expected = (uint8_t)(ANX7483_UTX2_PORT_CFG0_REG_RESERVED_MASK &
			     ANX7483_UTX2_PORT_CFG0_REG_DEFAULT);
	expected |= (uint8_t)(~ANX7483_UTX2_PORT_CFG0_REG_RESERVED_MASK);
	rv = anx7483_i2c_write(ANX7483_UTX2_PORT_CFG0_REG, expected);
	zexpect_ok(rv);
	rv = anx7483_i2c_read(ANX7483_UTX2_PORT_CFG0_REG, &val);
	zexpect_equal(val, expected);

	expected = (uint8_t)(ANX7483_URX1_PORT_CFG0_REG_RESERVED_MASK &
			     ANX7483_URX1_PORT_CFG0_REG_DEFAULT);
	expected |= (uint8_t)(~ANX7483_URX1_PORT_CFG0_REG_RESERVED_MASK);
	rv = anx7483_i2c_write(ANX7483_URX1_PORT_CFG0_REG, expected);
	zexpect_ok(rv);
	rv = anx7483_i2c_read(ANX7483_URX1_PORT_CFG0_REG, &val);
	zexpect_equal(val, expected);

	expected = (uint8_t)(ANX7483_URX2_PORT_CFG0_REG_RESERVED_MASK &
			     ANX7483_URX1_PORT_CFG0_REG_DEFAULT);
	expected |= (uint8_t)(~ANX7483_URX2_PORT_CFG0_REG_RESERVED_MASK);
	rv = anx7483_i2c_write(ANX7483_URX2_PORT_CFG0_REG, expected);
	zexpect_ok(rv);
	rv = anx7483_i2c_read(ANX7483_URX2_PORT_CFG0_REG, &val);
	zexpect_equal(val, expected);

	expected = (uint8_t)(ANX7483_DRX1_PORT_CFG0_REG_RESERVED_MASK &
			     ANX7483_DRX1_PORT_CFG0_REG_DEFAULT);
	expected |= (uint8_t)(~ANX7483_DRX1_PORT_CFG0_REG_RESERVED_MASK);
	rv = anx7483_i2c_write(ANX7483_DRX1_PORT_CFG0_REG, expected);
	zexpect_ok(rv);
	rv = anx7483_i2c_read(ANX7483_DRX1_PORT_CFG0_REG, &val);
	zexpect_equal(val, expected);

	expected = (uint8_t)(ANX7483_DRX2_PORT_CFG0_REG_RESERVED_MASK &
			     ANX7483_DRX2_PORT_CFG0_REG_DEFAULT);
	expected |= (uint8_t)(~ANX7483_DRX2_PORT_CFG0_REG_RESERVED_MASK);
	rv = anx7483_i2c_write(ANX7483_DRX2_PORT_CFG0_REG, expected);
	zexpect_ok(rv);
	rv = anx7483_i2c_read(ANX7483_DRX2_PORT_CFG0_REG, &val);
	zexpect_equal(val, expected);

	/* CFG1 */
	expected = 0xff;
	rv = anx7483_i2c_write(ANX7483_UTX1_PORT_CFG1_REG, expected);
	zexpect_ok(rv);
	rv = anx7483_i2c_read(ANX7483_UTX1_PORT_CFG1_REG, &val);
	zexpect_equal(val, expected);

	expected = 0xff;
	rv = anx7483_i2c_write(ANX7483_UTX2_PORT_CFG1_REG, expected);
	zexpect_ok(rv);
	rv = anx7483_i2c_read(ANX7483_UTX2_PORT_CFG1_REG, &val);
	zexpect_equal(val, expected);

	expected = 0xff;
	rv = anx7483_i2c_write(ANX7483_URX1_PORT_CFG1_REG, expected);
	zexpect_ok(rv);
	rv = anx7483_i2c_read(ANX7483_URX1_PORT_CFG1_REG, &val);
	zexpect_equal(val, expected);

	expected = 0xff;
	rv = anx7483_i2c_write(ANX7483_URX2_PORT_CFG1_REG, expected);
	zexpect_ok(rv);
	rv = anx7483_i2c_read(ANX7483_URX2_PORT_CFG1_REG, &val);
	zexpect_equal(val, expected);

	expected = 0xff;
	rv = anx7483_i2c_write(ANX7483_DRX1_PORT_CFG1_REG, expected);
	zexpect_ok(rv);
	rv = anx7483_i2c_read(ANX7483_DRX1_PORT_CFG1_REG, &val);
	zexpect_equal(val, expected);

	expected = 0xff;
	rv = anx7483_i2c_write(ANX7483_DRX2_PORT_CFG1_REG, expected);
	zexpect_ok(rv);
	rv = anx7483_i2c_read(ANX7483_DRX2_PORT_CFG1_REG, &val);
	zexpect_equal(val, expected);

	/* CFG2 */
	expected = (uint8_t)(ANX7483_UTX1_PORT_CFG2_REG_RESERVED_MASK &
			     ANX7483_UTX1_PORT_CFG2_REG_DEFAULT);
	expected |= (uint8_t)(~ANX7483_UTX1_PORT_CFG2_REG_RESERVED_MASK);
	rv = anx7483_i2c_write(ANX7483_UTX1_PORT_CFG2_REG, expected);
	zexpect_ok(rv);
	rv = anx7483_i2c_read(ANX7483_UTX1_PORT_CFG2_REG, &val);
	zexpect_equal(val, expected);

	expected = (uint8_t)(ANX7483_UTX2_PORT_CFG2_REG_RESERVED_MASK &
			     ANX7483_UTX2_PORT_CFG2_REG_DEFAULT);
	expected |= (uint8_t)(~ANX7483_UTX2_PORT_CFG2_REG_RESERVED_MASK);
	rv = anx7483_i2c_write(ANX7483_UTX2_PORT_CFG2_REG, expected);
	zexpect_ok(rv);
	rv = anx7483_i2c_read(ANX7483_UTX2_PORT_CFG2_REG, &val);
	zexpect_equal(val, expected);

	expected = (uint8_t)(ANX7483_URX1_PORT_CFG2_REG_RESERVED_MASK &
			     ANX7483_URX1_PORT_CFG2_REG_DEFAULT);
	expected |= (uint8_t)(~ANX7483_URX1_PORT_CFG2_REG_RESERVED_MASK);
	rv = anx7483_i2c_write(ANX7483_URX1_PORT_CFG2_REG, expected);
	zexpect_ok(rv);
	rv = anx7483_i2c_read(ANX7483_URX1_PORT_CFG2_REG, &val);
	zexpect_equal(val, expected);

	expected = (uint8_t)(ANX7483_URX2_PORT_CFG2_REG_RESERVED_MASK &
			     ANX7483_URX1_PORT_CFG2_REG_DEFAULT);
	expected |= (uint8_t)(~ANX7483_URX2_PORT_CFG2_REG_RESERVED_MASK);
	rv = anx7483_i2c_write(ANX7483_URX2_PORT_CFG2_REG, expected);
	zexpect_ok(rv);
	rv = anx7483_i2c_read(ANX7483_URX2_PORT_CFG2_REG, &val);
	zexpect_equal(val, expected);

	expected = (uint8_t)(ANX7483_DRX1_PORT_CFG2_REG_RESERVED_MASK &
			     ANX7483_DRX1_PORT_CFG2_REG_DEFAULT);
	expected |= (uint8_t)(~ANX7483_DRX1_PORT_CFG2_REG_RESERVED_MASK);
	rv = anx7483_i2c_write(ANX7483_DRX1_PORT_CFG2_REG, expected);
	zexpect_ok(rv);
	rv = anx7483_i2c_read(ANX7483_DRX1_PORT_CFG2_REG, &val);
	zexpect_equal(val, expected);

	expected = (uint8_t)(ANX7483_DRX2_PORT_CFG2_REG_RESERVED_MASK &
			     ANX7483_DRX2_PORT_CFG2_REG_DEFAULT);
	expected |= (uint8_t)(~ANX7483_DRX2_PORT_CFG2_REG_RESERVED_MASK);
	rv = anx7483_i2c_write(ANX7483_DRX2_PORT_CFG2_REG, expected);
	zexpect_ok(rv);
	rv = anx7483_i2c_read(ANX7483_DRX2_PORT_CFG2_REG, &val);
	zexpect_equal(val, expected);

	/* CFG3 */
	expected = 0xff;
	rv = anx7483_i2c_write(ANX7483_UTX1_PORT_CFG3_REG, expected);
	zexpect_ok(rv);
	rv = anx7483_i2c_read(ANX7483_UTX1_PORT_CFG3_REG, &val);
	zexpect_equal(val, expected);

	expected = 0xff;
	rv = anx7483_i2c_write(ANX7483_UTX2_PORT_CFG3_REG, expected);
	zexpect_ok(rv);
	rv = anx7483_i2c_read(ANX7483_UTX2_PORT_CFG3_REG, &val);
	zexpect_equal(val, expected);

	expected = 0xff;
	rv = anx7483_i2c_write(ANX7483_URX1_PORT_CFG3_REG, expected);
	zexpect_ok(rv);
	rv = anx7483_i2c_read(ANX7483_URX1_PORT_CFG3_REG, &val);
	zexpect_equal(val, expected);

	expected = 0xff;
	rv = anx7483_i2c_write(ANX7483_URX2_PORT_CFG3_REG, expected);
	zexpect_ok(rv);
	rv = anx7483_i2c_read(ANX7483_URX2_PORT_CFG3_REG, &val);
	zexpect_equal(val, expected);

	expected = 0xff;
	rv = anx7483_i2c_write(ANX7483_DRX1_PORT_CFG3_REG, expected);
	zexpect_ok(rv);
	rv = anx7483_i2c_read(ANX7483_DRX1_PORT_CFG3_REG, &val);
	zexpect_equal(val, expected);

	expected = 0xff;
	rv = anx7483_i2c_write(ANX7483_DRX2_PORT_CFG3_REG, expected);
	zexpect_ok(rv);
	rv = anx7483_i2c_read(ANX7483_DRX2_PORT_CFG3_REG, &val);
	zexpect_equal(val, expected);

	/* CFG4 */
	expected = (uint8_t)(ANX7483_UTX1_PORT_CFG4_REG_RESERVED_MASK &
			     ANX7483_UTX1_PORT_CFG4_REG_DEFAULT);
	expected |= (uint8_t)(~ANX7483_UTX1_PORT_CFG4_REG_RESERVED_MASK);
	rv = anx7483_i2c_write(ANX7483_UTX1_PORT_CFG4_REG, expected);
	zexpect_ok(rv);
	rv = anx7483_i2c_read(ANX7483_UTX1_PORT_CFG4_REG, &val);
	zexpect_equal(val, expected);

	expected = (uint8_t)(ANX7483_UTX2_PORT_CFG4_REG_RESERVED_MASK &
			     ANX7483_UTX2_PORT_CFG4_REG_DEFAULT);
	expected |= (uint8_t)(~ANX7483_UTX2_PORT_CFG4_REG_RESERVED_MASK);
	rv = anx7483_i2c_write(ANX7483_UTX2_PORT_CFG4_REG, expected);
	zexpect_ok(rv);
	rv = anx7483_i2c_read(ANX7483_UTX2_PORT_CFG4_REG, &val);
	zexpect_equal(val, expected);

	expected = (uint8_t)(ANX7483_URX1_PORT_CFG4_REG_RESERVED_MASK &
			     ANX7483_URX1_PORT_CFG4_REG_DEFAULT);
	expected |= (uint8_t)(~ANX7483_URX1_PORT_CFG4_REG_RESERVED_MASK);
	rv = anx7483_i2c_write(ANX7483_URX1_PORT_CFG4_REG, expected);
	zexpect_ok(rv);
	rv = anx7483_i2c_read(ANX7483_URX1_PORT_CFG4_REG, &val);
	zexpect_equal(val, expected);

	expected = (uint8_t)(ANX7483_URX2_PORT_CFG4_REG_RESERVED_MASK &
			     ANX7483_URX1_PORT_CFG4_REG_DEFAULT);
	expected |= (uint8_t)(~ANX7483_URX2_PORT_CFG4_REG_RESERVED_MASK);
	rv = anx7483_i2c_write(ANX7483_URX2_PORT_CFG4_REG, expected);
	zexpect_ok(rv);
	rv = anx7483_i2c_read(ANX7483_URX2_PORT_CFG4_REG, &val);
	zexpect_equal(val, expected);

	expected = (uint8_t)(ANX7483_DRX1_PORT_CFG4_REG_RESERVED_MASK &
			     ANX7483_DRX1_PORT_CFG4_REG_DEFAULT);
	expected |= (uint8_t)(~ANX7483_DRX1_PORT_CFG4_REG_RESERVED_MASK);
	rv = anx7483_i2c_write(ANX7483_DRX1_PORT_CFG4_REG, expected);
	zexpect_ok(rv);
	rv = anx7483_i2c_read(ANX7483_DRX1_PORT_CFG4_REG, &val);
	zexpect_equal(val, expected);

	expected = (uint8_t)(ANX7483_DRX2_PORT_CFG4_REG_RESERVED_MASK &
			     ANX7483_DRX2_PORT_CFG4_REG_DEFAULT);
	expected |= (uint8_t)(~ANX7483_DRX2_PORT_CFG4_REG_RESERVED_MASK);
	rv = anx7483_i2c_write(ANX7483_DRX2_PORT_CFG4_REG, expected);
	zexpect_ok(rv);
	rv = anx7483_i2c_read(ANX7483_DRX2_PORT_CFG4_REG, &val);
	zexpect_equal(val, expected);

	/* Ensure that reading/writing a non-existent register fails. */
	rv = anx7483_i2c_read(0xff, &val);
	zexpect_not_equal(rv, 0);
	rv = anx7483_i2c_write(0xff, 0xff);
	zexpect_not_equal(rv, 0);
}

/* Test that the ANX7483 driver correctly reports its state. */
ZTEST(anx7483, test_mux_state_get)
{
	int rv;
	uint8_t val;
	mux_state_t state;

	rv = anx7483_emul_test_get_reg(ANX7483_ANALOG_STATUS_CTRL_REG, &val);
	zexpect_ok(rv);
	val |= ANX7483_CTRL_USB_EN;
	val |= ANX7483_CTRL_DP_EN;
	val |= ANX7483_CTRL_FLIP_EN;
	rv = anx7483_emul_test_set_reg(ANX7483_ANALOG_STATUS_CTRL_REG, val);
	zexpect_ok(rv);

	rv = anx7483_get(&mux, &state);
	zexpect_ok(rv);
	zexpect_true(val & USB_PD_MUX_USB_ENABLED);
	zexpect_true(val & USB_PD_MUX_DP_ENABLED);
	zexpect_true(val & USB_PD_MUX_POLARITY_INVERTED);
}

/* Test that the ANX7483 driver correctly sets the mux state. */
ZTEST(anx7483, test_mux_state_set)
{
	int rv;
	uint8_t val;
	bool ack_required;

	rv = anx7483_set(&mux,
			 USB_PD_MUX_USB_ENABLED | USB_PD_MUX_DP_ENABLED |
				 USB_PD_MUX_POLARITY_INVERTED,
			 &ack_required);
	zexpect_ok(rv);
	rv = anx7483_emul_test_get_reg(ANX7483_ANALOG_STATUS_CTRL_REG, &val);
	zexpect_ok(rv);
	zexpect_true(val & ANX7483_CTRL_REG_EN);
	zexpect_true(val & ANX7483_CTRL_USB_EN);
	zexpect_true(val & ANX7483_CTRL_DP_EN);
	zexpect_true(val & ANX7483_CTRL_FLIP_EN);
}

/* Validates that writing to a reserved register returns an error. */
ZTEST(anx7483, test_emul_reserved)
{
	int rv;

	rv = anx7483_emul_test_set_reg(ANX7483_LFPS_TIMER_REG,
				       ANX7483_LFPS_TIMER_REG_RESERVED_MASK);
	zexpect_not_equal(rv, 0);

	rv = anx7483_emul_test_set_reg(
		ANX7483_ANALOG_STATUS_CTRL_REG,
		ANX7483_ANALOG_STATUS_CTRL_REG_RESERVED_MASK);
	zexpect_not_equal(rv, 0);

	rv = anx7483_emul_test_set_reg(
		ANX7483_ENABLE_EQ_FLAT_SWING_REG,
		ANX7483_ENABLE_EQ_FLAT_SWING_REG_RESERVED_MASK);
	zexpect_not_equal(rv, 0);

	rv = anx7483_emul_test_set_reg(
		ANX7483_AUX_SNOOPING_CTRL_REG,
		ANX7483_AUX_SNOOPING_CTRL_REG_RESERVED_MASK);
	zexpect_not_equal(rv, 0);

	/* CFG0 */
	rv = anx7483_emul_test_set_reg(
		ANX7483_UTX1_PORT_CFG0_REG,
		ANX7483_UTX1_PORT_CFG0_REG_RESERVED_MASK);
	zexpect_not_equal(rv, 0);

	rv = anx7483_emul_test_set_reg(
		ANX7483_UTX2_PORT_CFG0_REG,
		ANX7483_UTX2_PORT_CFG0_REG_RESERVED_MASK);
	zexpect_not_equal(rv, 0);

	rv = anx7483_emul_test_set_reg(
		ANX7483_URX1_PORT_CFG0_REG,
		ANX7483_URX1_PORT_CFG0_REG_RESERVED_MASK);
	zexpect_not_equal(rv, 0);

	rv = anx7483_emul_test_set_reg(
		ANX7483_URX2_PORT_CFG0_REG,
		ANX7483_URX2_PORT_CFG0_REG_RESERVED_MASK);
	zexpect_not_equal(rv, 0);

	rv = anx7483_emul_test_set_reg(
		ANX7483_DRX1_PORT_CFG0_REG,
		ANX7483_DRX1_PORT_CFG0_REG_RESERVED_MASK);
	zexpect_not_equal(rv, 0);

	rv = anx7483_emul_test_set_reg(
		ANX7483_DRX2_PORT_CFG0_REG,
		ANX7483_DRX2_PORT_CFG0_REG_RESERVED_MASK);
	zexpect_not_equal(rv, 0);

	/* CFG2 */
	rv = anx7483_emul_test_set_reg(
		ANX7483_UTX1_PORT_CFG2_REG,
		ANX7483_UTX1_PORT_CFG2_REG_RESERVED_MASK);
	zexpect_not_equal(rv, 0);

	rv = anx7483_emul_test_set_reg(
		ANX7483_UTX2_PORT_CFG2_REG,
		ANX7483_UTX2_PORT_CFG2_REG_RESERVED_MASK);
	zexpect_not_equal(rv, 0);

	rv = anx7483_emul_test_set_reg(
		ANX7483_URX1_PORT_CFG2_REG,
		ANX7483_URX1_PORT_CFG2_REG_RESERVED_MASK);
	zexpect_not_equal(rv, 0);

	rv = anx7483_emul_test_set_reg(
		ANX7483_URX2_PORT_CFG2_REG,
		ANX7483_URX2_PORT_CFG2_REG_RESERVED_MASK);
	zexpect_not_equal(rv, 0);

	rv = anx7483_emul_test_set_reg(
		ANX7483_DRX1_PORT_CFG2_REG,
		ANX7483_DRX1_PORT_CFG2_REG_RESERVED_MASK);
	zexpect_not_equal(rv, 0);

	rv = anx7483_emul_test_set_reg(
		ANX7483_DRX2_PORT_CFG2_REG,
		ANX7483_DRX2_PORT_CFG2_REG_RESERVED_MASK);
	zexpect_not_equal(rv, 0);

	/* CFG4 */
	rv = anx7483_emul_test_set_reg(
		ANX7483_UTX1_PORT_CFG4_REG,
		ANX7483_UTX1_PORT_CFG4_REG_RESERVED_MASK);
	zexpect_not_equal(rv, 0);

	rv = anx7483_emul_test_set_reg(
		ANX7483_UTX2_PORT_CFG4_REG,
		ANX7483_UTX2_PORT_CFG4_REG_RESERVED_MASK);
	zexpect_not_equal(rv, 0);

	rv = anx7483_emul_test_set_reg(
		ANX7483_URX1_PORT_CFG4_REG,
		ANX7483_URX1_PORT_CFG4_REG_RESERVED_MASK);
	zexpect_not_equal(rv, 0);

	rv = anx7483_emul_test_set_reg(
		ANX7483_URX2_PORT_CFG4_REG,
		ANX7483_URX2_PORT_CFG4_REG_RESERVED_MASK);
	zexpect_not_equal(rv, 0);

	rv = anx7483_emul_test_set_reg(
		ANX7483_DRX1_PORT_CFG4_REG,
		ANX7483_DRX1_PORT_CFG4_REG_RESERVED_MASK);
	zexpect_not_equal(rv, 0);

	rv = anx7483_emul_test_set_reg(
		ANX7483_DRX2_PORT_CFG4_REG,
		ANX7483_DRX2_PORT_CFG4_REG_RESERVED_MASK);
	zexpect_not_equal(rv, 0);
}

/*
 * Verify that we can make changes to the reserved masks to allow board-specific
 * tunings to be tested.
 */
ZTEST(anx7483, test_emul_update_reserved)
{
	int rv;
	uint8_t antimask;

	antimask = (uint8_t)(~ANX7483_LFPS_TIMER_REG_RESERVED_MASK);
	rv = anx7483_emul_test_set_mask(ANX7483_LFPS_TIMER_REG, antimask, 0);
	zexpect_ok(rv);
	rv = anx7483_emul_test_set_reg(ANX7483_LFPS_TIMER_REG, ~antimask);
	zexpect_ok(rv);

	antimask = (uint8_t)(~ANX7483_ANALOG_STATUS_CTRL_REG_RESERVED_MASK);
	rv = anx7483_emul_test_set_mask(ANX7483_ANALOG_STATUS_CTRL_REG,
					antimask, 0);
	zexpect_ok(rv);
	rv = anx7483_emul_test_set_reg(ANX7483_ANALOG_STATUS_CTRL_REG,
				       ~antimask);
	zexpect_ok(rv);

	antimask = (uint8_t)(~ANX7483_ENABLE_EQ_FLAT_SWING_REG_RESERVED_MASK);
	rv = anx7483_emul_test_set_mask(ANX7483_ENABLE_EQ_FLAT_SWING_REG,
					antimask, 0);
	zexpect_ok(rv);
	rv = anx7483_emul_test_set_reg(ANX7483_ENABLE_EQ_FLAT_SWING_REG,
				       ~antimask);
	zexpect_ok(rv);

	antimask = (uint8_t)(~ANX7483_AUX_SNOOPING_CTRL_REG_RESERVED_MASK);
	rv = anx7483_emul_test_set_mask(ANX7483_AUX_SNOOPING_CTRL_REG, antimask,
					0);
	zexpect_ok(rv);
	rv = anx7483_emul_test_set_reg(ANX7483_AUX_SNOOPING_CTRL_REG,
				       ~antimask);
	zexpect_ok(rv);

	/* CFG0 */
	antimask = (uint8_t)(~ANX7483_UTX1_PORT_CFG0_REG_RESERVED_MASK);
	rv = anx7483_emul_test_set_mask(ANX7483_UTX1_PORT_CFG0_REG, antimask,
					0);
	zexpect_ok(rv);
	rv = anx7483_emul_test_set_reg(ANX7483_UTX1_PORT_CFG0_REG, ~antimask);
	zexpect_ok(rv);

	antimask = (uint8_t)(~ANX7483_UTX2_PORT_CFG0_REG_RESERVED_MASK);
	rv = anx7483_emul_test_set_mask(ANX7483_UTX2_PORT_CFG0_REG, antimask,
					0);
	zexpect_ok(rv);
	rv = anx7483_emul_test_set_reg(ANX7483_UTX2_PORT_CFG0_REG, ~antimask);
	zexpect_ok(rv);

	antimask = (uint8_t)(~ANX7483_URX1_PORT_CFG0_REG_RESERVED_MASK);
	rv = anx7483_emul_test_set_mask(ANX7483_URX1_PORT_CFG0_REG, antimask,
					0);
	zexpect_ok(rv);
	rv = anx7483_emul_test_set_reg(ANX7483_URX1_PORT_CFG0_REG, ~antimask);
	zexpect_ok(rv);

	antimask = (uint8_t)(~ANX7483_URX2_PORT_CFG0_REG_RESERVED_MASK);
	rv = anx7483_emul_test_set_mask(ANX7483_URX2_PORT_CFG0_REG, antimask,
					0);
	zexpect_ok(rv);
	rv = anx7483_emul_test_set_reg(ANX7483_URX2_PORT_CFG0_REG, ~antimask);
	zexpect_ok(rv);

	antimask = (uint8_t)(~ANX7483_DRX1_PORT_CFG0_REG_RESERVED_MASK);
	rv = anx7483_emul_test_set_mask(ANX7483_DRX1_PORT_CFG0_REG, antimask,
					0);
	zexpect_ok(rv);
	rv = anx7483_emul_test_set_reg(ANX7483_DRX1_PORT_CFG0_REG, ~antimask);
	zexpect_ok(rv);

	antimask = (uint8_t)(~ANX7483_DRX2_PORT_CFG0_REG_RESERVED_MASK);
	rv = anx7483_emul_test_set_mask(ANX7483_DRX2_PORT_CFG0_REG, antimask,
					0);
	zexpect_ok(rv);
	rv = anx7483_emul_test_set_reg(ANX7483_DRX2_PORT_CFG0_REG, ~antimask);
	zexpect_ok(rv);

	/* CFG2 */
	antimask = (uint8_t)(~ANX7483_UTX1_PORT_CFG2_REG_RESERVED_MASK);
	rv = anx7483_emul_test_set_mask(ANX7483_UTX1_PORT_CFG2_REG, antimask,
					0);
	zexpect_ok(rv);
	rv = anx7483_emul_test_set_reg(ANX7483_UTX1_PORT_CFG2_REG, ~antimask);
	zexpect_ok(rv);

	antimask = (uint8_t)(~ANX7483_UTX2_PORT_CFG2_REG_RESERVED_MASK);
	rv = anx7483_emul_test_set_mask(ANX7483_UTX2_PORT_CFG2_REG, antimask,
					0);
	zexpect_ok(rv);
	rv = anx7483_emul_test_set_reg(ANX7483_UTX2_PORT_CFG2_REG, ~antimask);
	zexpect_ok(rv);

	antimask = (uint8_t)(~ANX7483_URX1_PORT_CFG2_REG_RESERVED_MASK);
	rv = anx7483_emul_test_set_mask(ANX7483_URX1_PORT_CFG2_REG, antimask,
					0);
	zexpect_ok(rv);
	rv = anx7483_emul_test_set_reg(ANX7483_URX1_PORT_CFG2_REG, ~antimask);
	zexpect_ok(rv);

	antimask = (uint8_t)(~ANX7483_URX2_PORT_CFG2_REG_RESERVED_MASK);
	rv = anx7483_emul_test_set_mask(ANX7483_URX2_PORT_CFG2_REG, antimask,
					0);
	zexpect_ok(rv);
	rv = anx7483_emul_test_set_reg(ANX7483_URX2_PORT_CFG2_REG, ~antimask);
	zexpect_ok(rv);

	antimask = (uint8_t)(~ANX7483_DRX1_PORT_CFG2_REG_RESERVED_MASK);
	rv = anx7483_emul_test_set_mask(ANX7483_DRX1_PORT_CFG2_REG, antimask,
					0);
	zexpect_ok(rv);
	rv = anx7483_emul_test_set_reg(ANX7483_DRX1_PORT_CFG2_REG, ~antimask);
	zexpect_ok(rv);

	antimask = (uint8_t)(~ANX7483_DRX2_PORT_CFG2_REG_RESERVED_MASK);
	rv = anx7483_emul_test_set_mask(ANX7483_DRX2_PORT_CFG2_REG, antimask,
					0);
	zexpect_ok(rv);
	rv = anx7483_emul_test_set_reg(ANX7483_DRX2_PORT_CFG2_REG, ~antimask);
	zexpect_ok(rv);

	/* CFG4 */
	antimask = (uint8_t)(~ANX7483_UTX1_PORT_CFG4_REG_RESERVED_MASK);
	rv = anx7483_emul_test_set_mask(ANX7483_UTX1_PORT_CFG4_REG, antimask,
					0);
	zexpect_ok(rv);
	rv = anx7483_emul_test_set_reg(ANX7483_UTX1_PORT_CFG4_REG, ~antimask);
	zexpect_ok(rv);

	antimask = (uint8_t)(~ANX7483_UTX2_PORT_CFG4_REG_RESERVED_MASK);
	rv = anx7483_emul_test_set_mask(ANX7483_UTX2_PORT_CFG4_REG, antimask,
					0);
	zexpect_ok(rv);
	rv = anx7483_emul_test_set_reg(ANX7483_UTX2_PORT_CFG4_REG, ~antimask);
	zexpect_ok(rv);

	antimask = (uint8_t)(~ANX7483_URX1_PORT_CFG4_REG_RESERVED_MASK);
	rv = anx7483_emul_test_set_mask(ANX7483_URX1_PORT_CFG4_REG, antimask,
					0);
	zexpect_ok(rv);
	rv = anx7483_emul_test_set_reg(ANX7483_URX1_PORT_CFG4_REG, ~antimask);
	zexpect_ok(rv);

	antimask = (uint8_t)(~ANX7483_URX2_PORT_CFG4_REG_RESERVED_MASK);
	rv = anx7483_emul_test_set_mask(ANX7483_URX2_PORT_CFG4_REG, antimask,
					0);
	zexpect_ok(rv);
	rv = anx7483_emul_test_set_reg(ANX7483_URX2_PORT_CFG4_REG, ~antimask);
	zexpect_ok(rv);

	antimask = (uint8_t)(~ANX7483_DRX1_PORT_CFG4_REG_RESERVED_MASK);
	rv = anx7483_emul_test_set_mask(ANX7483_DRX1_PORT_CFG4_REG, antimask,
					0);
	zexpect_ok(rv);
	rv = anx7483_emul_test_set_reg(ANX7483_DRX1_PORT_CFG4_REG, ~antimask);
	zexpect_ok(rv);

	antimask = (uint8_t)(~ANX7483_DRX2_PORT_CFG4_REG_RESERVED_MASK);
	rv = anx7483_emul_test_set_mask(ANX7483_DRX2_PORT_CFG4_REG, antimask,
					0);
	zexpect_ok(rv);
	rv = anx7483_emul_test_set_reg(ANX7483_DRX2_PORT_CFG4_REG, ~antimask);
	zexpect_ok(rv);
}

/*
 * Tests that the ANX7483 driver correctly configures the default tuning for
 * USB.
 */
ZTEST(anx7483, test_tuning_usb_AA)
{
	/*
	 * Vendor defined tuning settings, these should match those in the
	 * anx7483_usb_enabled struct within the driver.
	 */
	const struct anx7483_tuning_set usb_enabled[] = {
		{ ANX7483_URX1_PORT_CFG2_REG, ANX7483_CFG2_DEF },
		{ ANX7483_URX2_PORT_CFG2_REG, ANX7483_CFG2_DEF },
		{ ANX7483_DRX1_PORT_CFG2_REG, ANX7483_CFG2_DEF },
		{ ANX7483_DRX2_PORT_CFG2_REG, ANX7483_CFG2_DEF },

		{ ANX7483_URX1_PORT_CFG0_REG, ANX7483_CFG0_DEF },
		{ ANX7483_URX2_PORT_CFG0_REG, ANX7483_CFG0_DEF },
		{ ANX7483_DRX1_PORT_CFG0_REG, ANX7483_CFG0_DEF },
		{ ANX7483_DRX2_PORT_CFG0_REG, ANX7483_CFG0_DEF },

		{ ANX7483_URX1_PORT_CFG4_REG, ANX7483_CFG4_TERM_ENABLE },
		{ ANX7483_URX2_PORT_CFG4_REG, ANX7483_CFG4_TERM_ENABLE },
		{ ANX7483_DRX1_PORT_CFG4_REG, ANX7483_CFG4_TERM_ENABLE },
		{ ANX7483_DRX2_PORT_CFG4_REG, ANX7483_CFG4_TERM_ENABLE },

		{ ANX7483_UTX1_PORT_CFG4_REG, ANX7483_CFG4_TERM_DISABLE },
		{ ANX7483_UTX2_PORT_CFG4_REG, ANX7483_CFG4_TERM_DISABLE },
		{ ANX7483_DTX1_PORT_CFG4_REG, ANX7483_CFG4_TERM_DISABLE },
		{ ANX7483_DTX2_PORT_CFG4_REG, ANX7483_CFG4_TERM_DISABLE },

		{ ANX7483_URX1_PORT_CFG1_REG, ANX7483_CFG1_DEF },
		{ ANX7483_URX2_PORT_CFG1_REG, ANX7483_CFG1_DEF },
		{ ANX7483_DRX1_PORT_CFG1_REG, ANX7483_CFG1_DEF },
		{ ANX7483_DRX2_PORT_CFG1_REG, ANX7483_CFG1_DEF },

		{ ANX7483_URX1_PORT_CFG3_REG, ANX7483_CFG3_90Ohm_OUT },
		{ ANX7483_URX2_PORT_CFG3_REG, ANX7483_CFG3_90Ohm_OUT },
		{ ANX7483_DRX1_PORT_CFG3_REG, ANX7483_CFG3_90Ohm_OUT },
		{ ANX7483_DRX2_PORT_CFG3_REG, ANX7483_CFG3_90Ohm_OUT },

		{ ANX7483_UTX1_PORT_CFG3_REG, ANX7483_CFG3_90Ohm_IN },
		{ ANX7483_UTX2_PORT_CFG3_REG, ANX7483_CFG3_90Ohm_IN },
		{ ANX7483_DTX1_PORT_CFG3_REG, ANX7483_CFG3_90Ohm_IN },
		{ ANX7483_DTX2_PORT_CFG3_REG, ANX7483_CFG3_90Ohm_IN },
	};
	int rv;

	rv = anx7483_emul_test_set_reg(ANX7483_CHIP_ID,
				       ANX7483_CHIP_ID_DEFAULT);
	zexpect_ok(rv);

	rv = anx7483_set_default_tuning(&mux, USB_PD_MUX_USB_ENABLED);
	zexpect_ok(rv);

	zassert_equal(ARRAY_SIZE(usb_enabled),
		      anx7483_usb_enabled_count + anx7483_AA_usb_count);
	rv = anx7483_emul_validate_tuning(ANX7483_EMUL, usb_enabled,
					  anx7483_usb_enabled_count +
						  anx7483_AA_usb_count);
	zexpect_ok(rv);
}

ZTEST(anx7483, test_tuning_usb_BA)
{
	/*
	 * Vendor defined tuning settings, these should match those in the
	 * anx7483_usb_enabled struct within the driver.
	 */
	const struct anx7483_tuning_set usb_enabled[] = {
		{ ANX7483_URX1_PORT_CFG2_REG, ANX7483_CFG2_DEF },
		{ ANX7483_URX2_PORT_CFG2_REG, ANX7483_CFG2_DEF },
		{ ANX7483_DRX1_PORT_CFG2_REG, ANX7483_CFG2_DEF },
		{ ANX7483_DRX2_PORT_CFG2_REG, ANX7483_CFG2_DEF },

		{ ANX7483_URX1_PORT_CFG0_REG, ANX7483_CFG0_DEF },
		{ ANX7483_URX2_PORT_CFG0_REG, ANX7483_CFG0_DEF },
		{ ANX7483_DRX1_PORT_CFG0_REG, ANX7483_CFG0_DEF },
		{ ANX7483_DRX2_PORT_CFG0_REG, ANX7483_CFG0_DEF },

		{ ANX7483_URX1_PORT_CFG4_REG, ANX7483_CFG4_TERM_ENABLE },
		{ ANX7483_URX2_PORT_CFG4_REG, ANX7483_CFG4_TERM_ENABLE },
		{ ANX7483_DRX1_PORT_CFG4_REG, ANX7483_CFG4_TERM_ENABLE },
		{ ANX7483_DRX2_PORT_CFG4_REG, ANX7483_CFG4_TERM_ENABLE },

		{ ANX7483_UTX1_PORT_CFG4_REG, ANX7483_CFG4_TERM_DISABLE },
		{ ANX7483_UTX2_PORT_CFG4_REG, ANX7483_CFG4_TERM_DISABLE },
		{ ANX7483_DTX1_PORT_CFG4_REG, ANX7483_CFG4_TERM_DISABLE },
		{ ANX7483_DTX2_PORT_CFG4_REG, ANX7483_CFG4_TERM_DISABLE },

		{ ANX7483_URX1_PORT_CFG1_REG, ANX7483_CFG1_DEF },
		{ ANX7483_URX2_PORT_CFG1_REG, ANX7483_CFG1_DEF },
		{ ANX7483_DRX1_PORT_CFG1_REG, ANX7483_CFG1_DEF },
		{ ANX7483_DRX2_PORT_CFG1_REG, ANX7483_CFG1_DEF },

		{ ANX7483_URX1_PORT_CFG3_REG, ANX7483_BA_CFG3_90Ohm_OUT },
		{ ANX7483_URX2_PORT_CFG3_REG, ANX7483_BA_CFG3_90Ohm_OUT },
		{ ANX7483_DRX1_PORT_CFG3_REG, ANX7483_BA_CFG3_90Ohm_OUT },
		{ ANX7483_DRX2_PORT_CFG3_REG, ANX7483_BA_CFG3_90Ohm_OUT },

		{ ANX7483_UTX1_PORT_CFG3_REG, ANX7483_BA_CFG3_90Ohm_IN },
		{ ANX7483_UTX2_PORT_CFG3_REG, ANX7483_BA_CFG3_90Ohm_IN },
		{ ANX7483_DTX1_PORT_CFG3_REG, ANX7483_BA_CFG3_90Ohm_IN },
		{ ANX7483_DTX2_PORT_CFG3_REG, ANX7483_BA_CFG3_90Ohm_IN },
	};
	int rv;

	rv = anx7483_emul_test_set_reg(ANX7483_CHIP_ID, ANX7483_BA);
	zexpect_ok(rv);
	rv = anx7483_set_default_tuning(&mux, USB_PD_MUX_USB_ENABLED);
	zexpect_ok(rv);

	zassert_equal(ARRAY_SIZE(usb_enabled),
		      anx7483_usb_enabled_count + anx7483_BA_usb_count);
	rv = anx7483_emul_validate_tuning(ANX7483_EMUL, usb_enabled,
					  anx7483_usb_enabled_count +
						  anx7483_BA_usb_count);
	zexpect_ok(rv);
}

/*
 * Tests that the ANX7483 driver correctly configures the default tuning for
 * DisplayPort.
 */
ZTEST(anx7483, test_tuning_dp_AA)
{
	/*
	 * Vendor defined tuning settings, these should match those in the
	 * anx7483_dp_enabled struct within the driver.
	 */
	const struct anx7483_tuning_set dp_enabled[] = {
		{ ANX7483_AUX_SNOOPING_CTRL_REG, ANX7483_AUX_SNOOPING_DEF },

		{ ANX7483_URX1_PORT_CFG2_REG, ANX7483_CFG2_DEF },
		{ ANX7483_URX2_PORT_CFG2_REG, ANX7483_CFG2_DEF },
		{ ANX7483_UTX1_PORT_CFG2_REG, ANX7483_CFG2_DEF },
		{ ANX7483_UTX2_PORT_CFG2_REG, ANX7483_CFG2_DEF },

		{ ANX7483_URX1_PORT_CFG0_REG, ANX7483_CFG0_DEF },
		{ ANX7483_URX2_PORT_CFG0_REG, ANX7483_CFG0_DEF },
		{ ANX7483_UTX1_PORT_CFG0_REG, ANX7483_CFG0_DEF },
		{ ANX7483_UTX2_PORT_CFG0_REG, ANX7483_CFG0_DEF },

		{ ANX7483_URX1_PORT_CFG4_REG, ANX7483_CFG4_TERM_DISABLE },
		{ ANX7483_URX2_PORT_CFG4_REG, ANX7483_CFG4_TERM_DISABLE },
		{ ANX7483_UTX1_PORT_CFG4_REG, ANX7483_CFG4_TERM_DISABLE },
		{ ANX7483_UTX2_PORT_CFG4_REG, ANX7483_CFG4_TERM_DISABLE },
		{ ANX7483_DRX1_PORT_CFG4_REG, ANX7483_CFG4_TERM_DISABLE },
		{ ANX7483_DRX2_PORT_CFG4_REG, ANX7483_CFG4_TERM_DISABLE },
		{ ANX7483_DTX1_PORT_CFG4_REG, ANX7483_CFG4_TERM_DISABLE },
		{ ANX7483_DTX2_PORT_CFG4_REG, ANX7483_CFG4_TERM_DISABLE },

		{ ANX7483_URX1_PORT_CFG1_REG, ANX7483_CFG1_DEF },
		{ ANX7483_URX2_PORT_CFG1_REG, ANX7483_CFG1_DEF },
		{ ANX7483_UTX1_PORT_CFG1_REG, ANX7483_CFG1_DEF },
		{ ANX7483_UTX2_PORT_CFG1_REG, ANX7483_CFG1_DEF },

		{ ANX7483_URX1_PORT_CFG3_REG, ANX7483_CFG3_100Ohm_IN },
		{ ANX7483_URX2_PORT_CFG3_REG, ANX7483_CFG3_100Ohm_IN },
		{ ANX7483_UTX1_PORT_CFG3_REG, ANX7483_CFG3_100Ohm_IN },
		{ ANX7483_UTX2_PORT_CFG3_REG, ANX7483_CFG3_100Ohm_IN },
		{ ANX7483_DRX1_PORT_CFG3_REG, ANX7483_CFG3_100Ohm_IN },
		{ ANX7483_DRX2_PORT_CFG3_REG, ANX7483_CFG3_100Ohm_IN },
		{ ANX7483_DTX1_PORT_CFG3_REG, ANX7483_CFG3_100Ohm_IN },
		{ ANX7483_DTX2_PORT_CFG3_REG, ANX7483_CFG3_100Ohm_IN },
	};
	int rv;

	rv = anx7483_emul_test_set_reg(ANX7483_CHIP_ID,
				       ANX7483_CHIP_ID_DEFAULT);
	zexpect_ok(rv);

	rv = anx7483_set_default_tuning(&mux, USB_PD_MUX_DP_ENABLED);
	zexpect_ok(rv);

	zassert_equal(ARRAY_SIZE(dp_enabled),
		      anx7483_dp_enabled_count + anx7483_AA_dp_count);
	rv = anx7483_emul_validate_tuning(ANX7483_EMUL, dp_enabled,
					  anx7483_dp_enabled_count +
						  anx7483_AA_dp_count);
	zexpect_ok(rv);
}

ZTEST(anx7483, test_tuning_dp_BA)
{
	/*
	 * Vendor defined tuning settings, these should match those in the
	 * anx7483_usb_enabled struct within the driver.
	 */
	const struct anx7483_tuning_set dp_enabled[] = {
		{ ANX7483_AUX_SNOOPING_CTRL_REG, ANX7483_AUX_SNOOPING_DEF },

		{ ANX7483_URX1_PORT_CFG2_REG, ANX7483_CFG2_DEF },
		{ ANX7483_URX2_PORT_CFG2_REG, ANX7483_CFG2_DEF },
		{ ANX7483_UTX1_PORT_CFG2_REG, ANX7483_CFG2_DEF },
		{ ANX7483_UTX2_PORT_CFG2_REG, ANX7483_CFG2_DEF },

		{ ANX7483_URX1_PORT_CFG0_REG, ANX7483_CFG0_DEF },
		{ ANX7483_URX2_PORT_CFG0_REG, ANX7483_CFG0_DEF },
		{ ANX7483_UTX1_PORT_CFG0_REG, ANX7483_CFG0_DEF },
		{ ANX7483_UTX2_PORT_CFG0_REG, ANX7483_CFG0_DEF },

		{ ANX7483_URX1_PORT_CFG4_REG, ANX7483_CFG4_TERM_DISABLE },
		{ ANX7483_URX2_PORT_CFG4_REG, ANX7483_CFG4_TERM_DISABLE },
		{ ANX7483_UTX1_PORT_CFG4_REG, ANX7483_CFG4_TERM_DISABLE },
		{ ANX7483_UTX2_PORT_CFG4_REG, ANX7483_CFG4_TERM_DISABLE },
		{ ANX7483_DRX1_PORT_CFG4_REG, ANX7483_CFG4_TERM_DISABLE },
		{ ANX7483_DRX2_PORT_CFG4_REG, ANX7483_CFG4_TERM_DISABLE },
		{ ANX7483_DTX1_PORT_CFG4_REG, ANX7483_CFG4_TERM_DISABLE },
		{ ANX7483_DTX2_PORT_CFG4_REG, ANX7483_CFG4_TERM_DISABLE },

		{ ANX7483_URX1_PORT_CFG1_REG, ANX7483_CFG1_DEF },
		{ ANX7483_URX2_PORT_CFG1_REG, ANX7483_CFG1_DEF },
		{ ANX7483_UTX1_PORT_CFG1_REG, ANX7483_CFG1_DEF },
		{ ANX7483_UTX2_PORT_CFG1_REG, ANX7483_CFG1_DEF },

		{ ANX7483_URX1_PORT_CFG3_REG, ANX7483_BA_CFG3_90Ohm_OUT },
		{ ANX7483_URX2_PORT_CFG3_REG, ANX7483_BA_CFG3_90Ohm_OUT },
		{ ANX7483_UTX1_PORT_CFG3_REG, ANX7483_BA_CFG3_90Ohm_OUT },
		{ ANX7483_UTX2_PORT_CFG3_REG, ANX7483_BA_CFG3_90Ohm_OUT },
		{ ANX7483_DRX1_PORT_CFG3_REG, ANX7483_BA_CFG3_90Ohm_IN },
		{ ANX7483_DRX2_PORT_CFG3_REG, ANX7483_BA_CFG3_90Ohm_IN },
		{ ANX7483_DTX1_PORT_CFG3_REG, ANX7483_BA_CFG3_90Ohm_IN },
		{ ANX7483_DTX2_PORT_CFG3_REG, ANX7483_BA_CFG3_90Ohm_IN },

		{ ANX7483_AUX_CFG_1, ANX7483_AUX_CFG_1_REPLY },
		{ ANX7483_AUX_CFG_0, ANX7483_AUX_CFG_0_REPLY },

	};
	int rv;

	rv = anx7483_emul_test_set_reg(ANX7483_CHIP_ID, ANX7483_BA);
	zexpect_ok(rv);

	rv = anx7483_set_default_tuning(&mux, USB_PD_MUX_DP_ENABLED);
	zexpect_ok(rv);

	zassert_equal(ARRAY_SIZE(dp_enabled),
		      anx7483_dp_enabled_count + anx7483_BA_dp_count);
	rv = anx7483_emul_validate_tuning(ANX7483_EMUL, dp_enabled,
					  anx7483_dp_enabled_count +
						  anx7483_BA_dp_count);
	zexpect_ok(rv);
}

/*
 * Tests that the ANX7483 driver correctly configures the default tuning for
 * dock mode in a non-flipped state.
 */
ZTEST(anx7483, test_tuning_dock_noflip_AA)
{
	/*
	 * Vendor defined tuning settings, these should match those in the
	 * anx7483_dock_noflip struct within the driver.
	 */
	const test_export_static struct anx7483_tuning_set dock_noflip[] = {
		{ ANX7483_AUX_SNOOPING_CTRL_REG, ANX7483_AUX_SNOOPING_DEF },

		{ ANX7483_URX1_PORT_CFG2_REG, ANX7483_CFG2_DEF },
		{ ANX7483_DRX1_PORT_CFG2_REG, ANX7483_CFG2_DEF },
		{ ANX7483_URX2_PORT_CFG2_REG, ANX7483_CFG2_DEF },
		{ ANX7483_UTX2_PORT_CFG2_REG, ANX7483_CFG2_DEF },

		{ ANX7483_URX1_PORT_CFG0_REG, ANX7483_CFG0_DEF },
		{ ANX7483_DRX1_PORT_CFG0_REG, ANX7483_CFG0_DEF },
		{ ANX7483_URX2_PORT_CFG0_REG, ANX7483_CFG0_DEF },
		{ ANX7483_UTX2_PORT_CFG0_REG, ANX7483_CFG0_DEF },

		{ ANX7483_URX1_PORT_CFG4_REG, ANX7483_CFG4_TERM_ENABLE },
		{ ANX7483_DRX1_PORT_CFG4_REG, ANX7483_CFG4_TERM_ENABLE },

		{ ANX7483_URX2_PORT_CFG4_REG, ANX7483_CFG4_TERM_DISABLE },
		{ ANX7483_UTX2_PORT_CFG4_REG, ANX7483_CFG4_TERM_DISABLE },
		{ ANX7483_UTX1_PORT_CFG4_REG, ANX7483_CFG4_TERM_DISABLE },
		{ ANX7483_DTX1_PORT_CFG4_REG, ANX7483_CFG4_TERM_DISABLE },
		{ ANX7483_DRX2_PORT_CFG4_REG, ANX7483_CFG4_TERM_DISABLE },
		{ ANX7483_DTX2_PORT_CFG4_REG, ANX7483_CFG4_TERM_DISABLE },

		{ ANX7483_URX1_PORT_CFG1_REG, ANX7483_CFG1_DEF },
		{ ANX7483_DRX1_PORT_CFG1_REG, ANX7483_CFG1_DEF },
		{ ANX7483_URX2_PORT_CFG1_REG, ANX7483_CFG1_DEF },
		{ ANX7483_UTX2_PORT_CFG1_REG, ANX7483_CFG1_DEF },

		{ ANX7483_URX1_PORT_CFG3_REG, ANX7483_CFG3_90Ohm_IN },
		{ ANX7483_URX2_PORT_CFG3_REG, ANX7483_CFG3_100Ohm_IN },
		{ ANX7483_UTX1_PORT_CFG3_REG, ANX7483_CFG3_90Ohm_IN },
		{ ANX7483_UTX2_PORT_CFG3_REG, ANX7483_CFG3_100Ohm_IN },
		{ ANX7483_DRX1_PORT_CFG3_REG, ANX7483_CFG3_90Ohm_IN },
		{ ANX7483_DRX2_PORT_CFG3_REG, ANX7483_CFG3_100Ohm_IN },
		{ ANX7483_DTX1_PORT_CFG3_REG, ANX7483_CFG3_90Ohm_IN },
		{ ANX7483_DTX2_PORT_CFG3_REG, ANX7483_CFG3_100Ohm_IN },
	};
	int rv;

	rv = anx7483_emul_test_set_reg(ANX7483_CHIP_ID,
				       ANX7483_CHIP_ID_DEFAULT);
	zexpect_ok(rv);

	rv = anx7483_set_default_tuning(&mux, USB_PD_MUX_DOCK);
	zexpect_ok(rv);

	zassert_equal(ARRAY_SIZE(dock_noflip),
		      anx7483_dock_noflip_count + anx7483_AA_dock_noflip_count);
	rv = anx7483_emul_validate_tuning(ANX7483_EMUL, dock_noflip,
					  anx7483_dock_noflip_count +
						  anx7483_AA_dock_noflip_count);

	zexpect_ok(rv);
}

ZTEST(anx7483, test_tuning_dock_noflip_BA)
{
	/*
	 * Vendor defined tuning settings, these should match those in the
	 * anx7483_dock_noflip struct within the driver.
	 */
	const test_export_static struct anx7483_tuning_set dock_noflip[] = {
		{ ANX7483_AUX_SNOOPING_CTRL_REG, ANX7483_AUX_SNOOPING_DEF },

		{ ANX7483_URX1_PORT_CFG2_REG, ANX7483_CFG2_DEF },
		{ ANX7483_DRX1_PORT_CFG2_REG, ANX7483_CFG2_DEF },
		{ ANX7483_URX2_PORT_CFG2_REG, ANX7483_CFG2_DEF },
		{ ANX7483_UTX2_PORT_CFG2_REG, ANX7483_CFG2_DEF },

		{ ANX7483_URX1_PORT_CFG0_REG, ANX7483_CFG0_DEF },
		{ ANX7483_DRX1_PORT_CFG0_REG, ANX7483_CFG0_DEF },
		{ ANX7483_URX2_PORT_CFG0_REG, ANX7483_CFG0_DEF },
		{ ANX7483_UTX2_PORT_CFG0_REG, ANX7483_CFG0_DEF },

		{ ANX7483_URX1_PORT_CFG4_REG, ANX7483_CFG4_TERM_ENABLE },
		{ ANX7483_DRX1_PORT_CFG4_REG, ANX7483_CFG4_TERM_ENABLE },

		{ ANX7483_URX2_PORT_CFG4_REG, ANX7483_CFG4_TERM_DISABLE },
		{ ANX7483_UTX2_PORT_CFG4_REG, ANX7483_CFG4_TERM_DISABLE },
		{ ANX7483_UTX1_PORT_CFG4_REG, ANX7483_CFG4_TERM_DISABLE },
		{ ANX7483_DTX1_PORT_CFG4_REG, ANX7483_CFG4_TERM_DISABLE },
		{ ANX7483_DRX2_PORT_CFG4_REG, ANX7483_CFG4_TERM_DISABLE },
		{ ANX7483_DTX2_PORT_CFG4_REG, ANX7483_CFG4_TERM_DISABLE },

		{ ANX7483_URX1_PORT_CFG1_REG, ANX7483_CFG1_DEF },
		{ ANX7483_DRX1_PORT_CFG1_REG, ANX7483_CFG1_DEF },
		{ ANX7483_URX2_PORT_CFG1_REG, ANX7483_CFG1_DEF },
		{ ANX7483_UTX2_PORT_CFG1_REG, ANX7483_CFG1_DEF },

		{ ANX7483_URX1_PORT_CFG3_REG, ANX7483_BA_CFG3_90Ohm_OUT },
		{ ANX7483_URX2_PORT_CFG3_REG, ANX7483_BA_CFG3_90Ohm_OUT },
		{ ANX7483_UTX2_PORT_CFG3_REG, ANX7483_BA_CFG3_90Ohm_OUT },
		{ ANX7483_DRX1_PORT_CFG3_REG, ANX7483_BA_CFG3_90Ohm_OUT },
		{ ANX7483_UTX1_PORT_CFG3_REG, ANX7483_BA_CFG3_90Ohm_IN },
		{ ANX7483_DRX2_PORT_CFG3_REG, ANX7483_BA_CFG3_90Ohm_IN },
		{ ANX7483_DTX1_PORT_CFG3_REG, ANX7483_BA_CFG3_90Ohm_IN },
		{ ANX7483_DTX2_PORT_CFG3_REG, ANX7483_BA_CFG3_90Ohm_IN },

		{ ANX7483_AUX_CFG_1, ANX7483_AUX_CFG_1_REPLY },
		{ ANX7483_AUX_CFG_0, ANX7483_AUX_CFG_0_REPLY },
	};
	int rv;

	rv = anx7483_emul_test_set_reg(ANX7483_CHIP_ID, ANX7483_BA);
	zexpect_ok(rv);

	rv = anx7483_set_default_tuning(&mux, USB_PD_MUX_DOCK);
	zexpect_ok(rv);

	zassert_equal(ARRAY_SIZE(dock_noflip),
		      anx7483_dock_noflip_count + anx7483_BA_dock_noflip_count);
	rv = anx7483_emul_validate_tuning(ANX7483_EMUL, dock_noflip,
					  anx7483_dock_noflip_count +
						  anx7483_BA_dock_noflip_count);
	zexpect_ok(rv);
}

/*
 * Tests that the ANX7483 driver correctly configures the default tuning for
 * dock mode in a flipped state.
 */
ZTEST(anx7483, test_tuning_dock_flip_AA)
{
	/*
	 * Vendor defined tuning settings, these should match those in the
	 * anx7483_dock_flip struct within the driver.
	 */
	const test_export_static struct anx7483_tuning_set dock_flip[] = {
		{ ANX7483_AUX_SNOOPING_CTRL_REG, ANX7483_AUX_SNOOPING_DEF },

		{ ANX7483_URX2_PORT_CFG2_REG, ANX7483_CFG2_DEF },
		{ ANX7483_DRX2_PORT_CFG2_REG, ANX7483_CFG2_DEF },
		{ ANX7483_URX1_PORT_CFG2_REG, ANX7483_CFG2_DEF },
		{ ANX7483_UTX1_PORT_CFG2_REG, ANX7483_CFG2_DEF },

		{ ANX7483_URX2_PORT_CFG0_REG, ANX7483_CFG0_DEF },
		{ ANX7483_DRX2_PORT_CFG0_REG, ANX7483_CFG0_DEF },
		{ ANX7483_URX1_PORT_CFG0_REG, ANX7483_CFG0_DEF },
		{ ANX7483_UTX1_PORT_CFG0_REG, ANX7483_CFG0_DEF },

		{ ANX7483_URX2_PORT_CFG4_REG, ANX7483_CFG4_TERM_ENABLE },
		{ ANX7483_DRX2_PORT_CFG4_REG, ANX7483_CFG4_TERM_ENABLE },

		{ ANX7483_URX1_PORT_CFG4_REG, ANX7483_CFG4_TERM_DISABLE },
		{ ANX7483_UTX1_PORT_CFG4_REG, ANX7483_CFG4_TERM_DISABLE },
		{ ANX7483_UTX2_PORT_CFG4_REG, ANX7483_CFG4_TERM_DISABLE },
		{ ANX7483_DTX2_PORT_CFG4_REG, ANX7483_CFG4_TERM_DISABLE },
		{ ANX7483_DTX1_PORT_CFG4_REG, ANX7483_CFG4_TERM_DISABLE },
		{ ANX7483_DRX1_PORT_CFG4_REG, ANX7483_CFG4_TERM_DISABLE },

		{ ANX7483_URX1_PORT_CFG1_REG, ANX7483_CFG1_DEF },
		{ ANX7483_UTX1_PORT_CFG1_REG, ANX7483_CFG1_DEF },
		{ ANX7483_URX2_PORT_CFG1_REG, ANX7483_CFG1_DEF },
		{ ANX7483_DRX2_PORT_CFG1_REG, ANX7483_CFG1_DEF },

		{ ANX7483_URX1_PORT_CFG3_REG, ANX7483_CFG3_100Ohm_IN },
		{ ANX7483_URX2_PORT_CFG3_REG, ANX7483_CFG3_90Ohm_IN },
		{ ANX7483_UTX1_PORT_CFG3_REG, ANX7483_CFG3_100Ohm_IN },
		{ ANX7483_UTX2_PORT_CFG3_REG, ANX7483_CFG3_90Ohm_IN },
		{ ANX7483_DRX1_PORT_CFG3_REG, ANX7483_CFG3_100Ohm_IN },
		{ ANX7483_DRX2_PORT_CFG3_REG, ANX7483_CFG3_90Ohm_IN },
		{ ANX7483_DTX1_PORT_CFG3_REG, ANX7483_CFG3_100Ohm_IN },
		{ ANX7483_DTX2_PORT_CFG3_REG, ANX7483_CFG3_90Ohm_IN },
	};
	int rv;

	rv = anx7483_emul_test_set_reg(ANX7483_CHIP_ID,
				       ANX7483_CHIP_ID_DEFAULT);
	zexpect_ok(rv);

	rv = anx7483_set_default_tuning(
		&mux, USB_PD_MUX_DOCK | USB_PD_MUX_POLARITY_INVERTED);
	zexpect_ok(rv);

	zassert_equal(ARRAY_SIZE(dock_flip),
		      anx7483_dock_flip_count + anx7483_AA_dock_flip_count);

	rv = anx7483_emul_validate_tuning(ANX7483_EMUL, dock_flip,
					  anx7483_dock_flip_count +
						  anx7483_AA_dock_flip_count);

	zexpect_ok(rv);
}

/*
 * Tests that the ANX7483 driver correctly configures the default tuning for
 * dock mode in a flipped state.
 */
ZTEST(anx7483, test_tuning_dock_flip_BA)
{
	/*
	 * Vendor defined tuning settings, these should match those in the
	 * anx7483_dock_flip struct within the driver.
	 */
	const test_export_static struct anx7483_tuning_set dock_flip[] = {
		{ ANX7483_AUX_SNOOPING_CTRL_REG, ANX7483_AUX_SNOOPING_DEF },

		{ ANX7483_URX2_PORT_CFG2_REG, ANX7483_CFG2_DEF },
		{ ANX7483_DRX2_PORT_CFG2_REG, ANX7483_CFG2_DEF },
		{ ANX7483_URX1_PORT_CFG2_REG, ANX7483_CFG2_DEF },
		{ ANX7483_UTX1_PORT_CFG2_REG, ANX7483_CFG2_DEF },

		{ ANX7483_URX2_PORT_CFG0_REG, ANX7483_CFG0_DEF },
		{ ANX7483_DRX2_PORT_CFG0_REG, ANX7483_CFG0_DEF },
		{ ANX7483_URX1_PORT_CFG0_REG, ANX7483_CFG0_DEF },
		{ ANX7483_UTX1_PORT_CFG0_REG, ANX7483_CFG0_DEF },

		{ ANX7483_URX2_PORT_CFG4_REG, ANX7483_CFG4_TERM_ENABLE },
		{ ANX7483_DRX2_PORT_CFG4_REG, ANX7483_CFG4_TERM_ENABLE },

		{ ANX7483_URX1_PORT_CFG4_REG, ANX7483_CFG4_TERM_DISABLE },
		{ ANX7483_UTX1_PORT_CFG4_REG, ANX7483_CFG4_TERM_DISABLE },
		{ ANX7483_UTX2_PORT_CFG4_REG, ANX7483_CFG4_TERM_DISABLE },
		{ ANX7483_DTX2_PORT_CFG4_REG, ANX7483_CFG4_TERM_DISABLE },
		{ ANX7483_DTX1_PORT_CFG4_REG, ANX7483_CFG4_TERM_DISABLE },
		{ ANX7483_DRX1_PORT_CFG4_REG, ANX7483_CFG4_TERM_DISABLE },

		{ ANX7483_URX1_PORT_CFG1_REG, ANX7483_CFG1_DEF },
		{ ANX7483_UTX1_PORT_CFG1_REG, ANX7483_CFG1_DEF },
		{ ANX7483_URX2_PORT_CFG1_REG, ANX7483_CFG1_DEF },
		{ ANX7483_DRX2_PORT_CFG1_REG, ANX7483_CFG1_DEF },

		{ ANX7483_URX1_PORT_CFG3_REG, ANX7483_BA_CFG3_90Ohm_OUT },
		{ ANX7483_URX2_PORT_CFG3_REG, ANX7483_BA_CFG3_90Ohm_OUT },
		{ ANX7483_UTX1_PORT_CFG3_REG, ANX7483_BA_CFG3_90Ohm_OUT },
		{ ANX7483_DRX2_PORT_CFG3_REG, ANX7483_BA_CFG3_90Ohm_OUT },
		{ ANX7483_UTX2_PORT_CFG3_REG, ANX7483_BA_CFG3_90Ohm_IN },
		{ ANX7483_DRX1_PORT_CFG3_REG, ANX7483_BA_CFG3_90Ohm_IN },
		{ ANX7483_DTX1_PORT_CFG3_REG, ANX7483_BA_CFG3_90Ohm_IN },
		{ ANX7483_DTX2_PORT_CFG3_REG, ANX7483_BA_CFG3_90Ohm_IN },

		{ ANX7483_AUX_CFG_1, ANX7483_AUX_CFG_1_REPLY },
		{ ANX7483_AUX_CFG_0, ANX7483_AUX_CFG_0_REPLY },
	};
	int rv;

	rv = anx7483_emul_test_set_reg(ANX7483_CHIP_ID, ANX7483_BA);
	zexpect_ok(rv);

	rv = anx7483_set_default_tuning(
		&mux, USB_PD_MUX_DOCK | USB_PD_MUX_POLARITY_INVERTED);
	zexpect_ok(rv);

	zassert_equal(ARRAY_SIZE(dock_flip),
		      anx7483_dock_flip_count + anx7483_BA_dock_flip_count);

	rv = anx7483_emul_validate_tuning(ANX7483_EMUL, dock_flip,
					  anx7483_dock_flip_count +
						  anx7483_BA_dock_flip_count);
	zexpect_ok(rv);
}
