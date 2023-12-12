/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "driver/usb_mux/ps8743.h"
#include "emul/emul_common_i2c.h"
#include "emul/emul_ps8743.h"
#include "hooks.h"
#include "test/drivers/stubs.h"
#include "test/drivers/test_state.h"
#include "usb_mux.h"
#include "usbc/ps8743_usb_mux.h"
#include "usbc/usb_muxes.h"

#include <zephyr/drivers/emul.h>
#include <zephyr/ztest.h>
#include <zephyr/ztest_assert.h>

#define PS8743_NODE DT_NODELABEL(ps8743_mux_0)
static const struct emul *emul = EMUL_DT_GET(PS8743_NODE);
static const struct usb_mux *mux;

struct ps8743_fixture {
	struct i2c_common_emul_data *common;
};

static void ps8743_before(void *state)
{
	struct ps8743_fixture *f = state;

	ps8743_emul_reset_regs(emul);
	i2c_common_emul_set_read_fail_reg(f->common,
					  I2C_COMMON_EMUL_NO_FAIL_REG);
	i2c_common_emul_set_write_fail_reg(f->common,
					   I2C_COMMON_EMUL_NO_FAIL_REG);
}

static void *ps8743_setup(void)
{
	static struct ps8743_fixture fix;
	const struct usb_mux_chain *chain = &usb_muxes[0];

	fix.common = ps8743_get_i2c_common_data(emul);

	while (chain) {
		if (chain->mux->driver == &ps8743_usb_mux_driver) {
			mux = chain->mux;
			break;
		}
		chain = chain->next;
	}

	__ASSERT(mux != NULL, "mux not found");
	__ASSERT(mux->driver == &ps8743_usb_mux_driver, "driver not found");

	return &fix;
}

ZTEST_F(ps8743, test_mux_config)
{
	zassert_equal(mux->driver, &ps8743_usb_mux_driver);
}

ZTEST_F(ps8743, test_init)
{
	zassert_ok(mux->driver->init(mux));

	zassert_equal(ps8743_emul_peek_reg(emul, PS8743_REG_MODE),
		      PS8743_MODE_POWER_DOWN);

	ps8743_emul_reset_regs(emul);

	i2c_common_emul_set_write_fail_reg(fixture->common, PS8743_REG_MODE);
	zassert_ok(!mux->driver->init(mux));
	i2c_common_emul_set_write_fail_reg(fixture->common,
					   I2C_COMMON_EMUL_NO_FAIL_REG);

	i2c_common_emul_set_read_fail_reg(fixture->common,
					  PS8743_REG_REVISION_ID1);
	zassert_ok(!mux->driver->init(mux));
	i2c_common_emul_set_write_fail_reg(fixture->common,
					   I2C_COMMON_EMUL_NO_FAIL_REG);

	i2c_common_emul_set_read_fail_reg(fixture->common,
					  PS8743_REG_REVISION_ID2);
	zassert_ok(!mux->driver->init(mux));
	i2c_common_emul_set_read_fail_reg(fixture->common,
					  I2C_COMMON_EMUL_NO_FAIL_REG);

	i2c_common_emul_set_read_fail_reg(fixture->common, PS8743_REG_CHIP_ID1);
	zassert_ok(!mux->driver->init(mux));
	i2c_common_emul_set_read_fail_reg(fixture->common,
					  I2C_COMMON_EMUL_NO_FAIL_REG);

	i2c_common_emul_set_read_fail_reg(fixture->common, PS8743_REG_CHIP_ID2);
	zassert_ok(!mux->driver->init(mux));
	i2c_common_emul_set_read_fail_reg(fixture->common,
					  I2C_COMMON_EMUL_NO_FAIL_REG);
}

ZTEST_F(ps8743, test_set_mux)
{
	bool ack;
	const uint32_t default_val =
		(PS8743_MODE_IN_HPD_CONTROL | PS8743_MODE_DP_REG_CONTROL |
		 PS8743_MODE_USB_REG_CONTROL | PS8743_MODE_FLIP_REG_CONTROL);

	i2c_common_emul_set_write_fail_reg(fixture->common,
					   I2C_COMMON_EMUL_NO_FAIL_REG);
	zassert_ok(mux->driver->init(mux));

	/* NONE */
	zassert_ok(mux->driver->set(mux, USB_PD_MUX_NONE, &ack));
	zassert_equal(ps8743_emul_peek_reg(emul, PS8743_REG_MODE), default_val);

	i2c_common_emul_set_write_fail_reg(fixture->common,
					   I2C_COMMON_EMUL_NO_FAIL_REG);

	/* SAFE */
	zassert_ok(mux->driver->set(mux, USB_PD_MUX_SAFE_MODE, &ack));
	zassert_equal(ps8743_emul_peek_reg(emul, PS8743_REG_MODE), default_val);

	/* SAFE */
	zassert_ok(mux->driver->set(mux,
				    USB_PD_MUX_USB_ENABLED |
					    USB_PD_MUX_DP_ENABLED |
					    USB_PD_MUX_POLARITY_INVERTED,
				    &ack));

	/* SAFE, USB, POLARITY, DP */
	zassert_equal(
		ps8743_emul_peek_reg(emul, PS8743_REG_MODE),
		default_val | PS8743_MODE_USB_ENABLE | PS8743_MODE_DP_ENABLE |
			PS8743_MODE_IN_HPD_ASSERT | PS8743_MODE_FLIP_ENABLE);

	/* Write fail */
	i2c_common_emul_set_write_fail_reg(fixture->common, PS8743_REG_MODE);
	zassert_ok(!mux->driver->set(mux, USB_PD_MUX_NONE, &ack));
	i2c_common_emul_set_write_fail_reg(fixture->common,
					   I2C_COMMON_EMUL_NO_FAIL_REG);
}

ZTEST_F(ps8743, test_get_mux)
{
	mux_state_t state = USB_PD_MUX_NONE;

	i2c_common_emul_set_write_fail_reg(fixture->common,
					   I2C_COMMON_EMUL_NO_FAIL_REG);
	zassert_ok(mux->driver->init(mux));

	/* NONE */
	zassert_ok(mux->driver->get(mux, &state));
	zassert_equal(state, USB_PD_MUX_NONE);

	/* USB, DP, POLARITY */
	ps8743_emul_set_reg(emul, PS8743_REG_STATUS,
			    PS8743_STATUS_USB_ENABLED |
				    PS8743_STATUS_DP_ENABLED |
				    PS8743_STATUS_POLARITY_INVERTED);
	zassert_ok(mux->driver->get(mux, &state));
	zassert_equal(state, USB_PD_MUX_USB_ENABLED | USB_PD_MUX_DP_ENABLED |
				     USB_PD_MUX_POLARITY_INVERTED);

	/* Read fail */
	i2c_common_emul_set_read_fail_reg(fixture->common, PS8743_REG_STATUS);
	zassert_ok(!mux->driver->get(mux, &state));
	i2c_common_emul_set_read_fail_reg(fixture->common,
					  I2C_COMMON_EMUL_NO_FAIL_REG);
}

ZTEST_F(ps8743, test_check_chip_id)
{
	int val = 0;

	zassert_ok(ps8743_check_chip_id(mux, &val));
	zassert_equal(val, 0x8741);

	/* fail read*/
	i2c_common_emul_set_read_fail_reg(fixture->common, PS8743_REG_CHIP_ID2);
	zassert_ok(!ps8743_check_chip_id(mux, &val));
	i2c_common_emul_set_read_fail_reg(fixture->common,
					  I2C_COMMON_EMUL_NO_FAIL_REG);

	i2c_common_emul_set_read_fail_reg(fixture->common, PS8743_REG_CHIP_ID1);
	zassert_ok(!ps8743_check_chip_id(mux, &val));
	i2c_common_emul_set_read_fail_reg(fixture->common,
					  I2C_COMMON_EMUL_NO_FAIL_REG);
}

ZTEST_F(ps8743, test_suspend_resume)
{
	/* set USB only mode */
	ps8743_emul_set_reg(emul, PS8743_MISC_HPD_DP_USB_FLIP,
			    PS8743_USB_MODE_STATUS);
	ps8743_emul_set_reg(emul, PS8743_REG_MODE, PS8743_MODE_USB_ENABLE);
	ps8743_emul_set_reg(emul, PS8743_MISC_DCI_SS_MODES,
			    PS8743_SSTX_SUSPEND_MODE);

	k_sleep(K_SECONDS(1));
	hook_notify(HOOK_CHIPSET_SUSPEND);

	k_sleep(K_SECONDS(1));
	zassert_equal(ps8743_emul_peek_reg(emul, PS8743_REG_MODE), 0);

	hook_notify(HOOK_CHIPSET_RESUME_INIT);
	k_sleep(K_SECONDS(1));
	zassert_equal(ps8743_emul_peek_reg(emul, PS8743_REG_MODE),
		      PS8743_MODE_USB_ENABLE);
}

ZTEST_F(ps8743, test_tune_usb_eq)
{
	zassert_ok(ps8743_tune_usb_eq(mux, 0, 0));
}

ZTEST_SUITE(ps8743, drivers_predicate_post_main, ps8743_setup, ps8743_before,
	    NULL, NULL);
