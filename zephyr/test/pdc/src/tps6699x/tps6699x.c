/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "drivers/pdc.h"
#include "drivers/ucsi_v3.h"
#include "emul/emul_pdc.h"
#include "emul/emul_tps6699x.h"
#include "pdc_trace_msg.h"
#include "tps6699x_cmd.h"

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/emul.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/fff.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/ztest.h>

LOG_MODULE_REGISTER(test_tps6699x, LOG_LEVEL_DBG);
#define SLEEP_MS 200

#define TPS6699X_NODE DT_NODELABEL(pdc_emul1)

enum port_control_access {
	ACCESS_OK,
	ACCESS_READ_FAIL,
	ACCESS_WRITE_FAIL,
};

FAKE_VALUE_FUNC(int, tps_rw_port_control, const struct i2c_dt_spec *,
		union reg_port_control *, int);
test_mockable_static int tps_xfer_reg(const struct i2c_dt_spec *i2c,
				      enum tps6699x_reg reg, uint8_t *buf,
				      uint8_t len, int flag);

static const struct emul *emul = EMUL_DT_GET(TPS6699X_NODE);
static const struct device *dev = DEVICE_DT_GET(TPS6699X_NODE);
static enum port_control_access access;

static void tps6699x_before_test(void *data)
{
	access = ACCESS_OK;
	RESET_FAKE(tps_rw_port_control);
	emul_pdc_reset(emul);
	emul_pdc_set_response_delay(emul, 0);
	if (IS_ENABLED(CONFIG_TEST_PDC_MESSAGE_TRACING)) {
		set_pdc_trace_msg_mocks();
	}

	zassert_ok(emul_pdc_idle_wait(emul));
}

static int custom_fake_tps_rw_port_control(const struct i2c_dt_spec *i2c,
					   union reg_port_control *buf,
					   int flag)
{
	if (access == ACCESS_OK) {
		return tps_xfer_reg(i2c, REG_PORT_CONTROL, buf->raw_value,
				    sizeof(union reg_port_control), flag);
	} else if (access == ACCESS_READ_FAIL && (flag & I2C_MSG_READ)) {
		return -EIO;
	} else if (access == ACCESS_WRITE_FAIL && !(flag & I2C_MSG_READ)) {
		return -EIO;
	}

	return 0;
}

ZTEST_SUITE(tps6699x, NULL, NULL, tps6699x_before_test, NULL, NULL);

/* Driver should keep returning cached connector status bits until they are
 * acked via ACK_CC_CI.
 */
ZTEST_USER(tps6699x, test_connector_status_caching)
{
	union connector_status_t in = { 0 }, out = { 0 };
	union conn_status_change_bits_t in_status_change_bits = { 0 },
					out_status_change_bits = { 0 };

	in_status_change_bits.raw_value = 0;
	out_status_change_bits.raw_value = 0;

	/* First check that connector status change bits are seen. */
	in_status_change_bits.connect_change = 1;
	in.raw_conn_status_change_bits = in_status_change_bits.raw_value;

	zassert_ok(emul_pdc_set_connector_status(emul, &in));
	zassert_ok(pdc_get_connector_status(dev, &out));
	k_sleep(K_MSEC(SLEEP_MS));

	out_status_change_bits.raw_value = out.raw_conn_status_change_bits;

	zassert_equal(out_status_change_bits.connect_change,
		      in_status_change_bits.connect_change);
	zassert_equal(out_status_change_bits.external_supply_change,
		      in_status_change_bits.external_supply_change);

	/* Now make sure that the change bits are cached until acked. */
	in_status_change_bits.connect_change = 0;
	in_status_change_bits.external_supply_change = 1;
	in.raw_conn_status_change_bits = in_status_change_bits.raw_value;

	zassert_ok(emul_pdc_set_connector_status(emul, &in));
	zassert_ok(pdc_get_connector_status(dev, &out));
	k_sleep(K_MSEC(SLEEP_MS));
	out_status_change_bits.raw_value = out.raw_conn_status_change_bits;

	zassert_not_equal(out_status_change_bits.connect_change,
			  in_status_change_bits.connect_change);
	zassert_equal(out_status_change_bits.external_supply_change,
		      in_status_change_bits.external_supply_change);

	/* Ack away the change bits and confirm they're zero'd. */
	in_status_change_bits.connect_change = 1;
	in_status_change_bits.external_supply_change = 1;

	zassert_ok(pdc_ack_cc_ci(dev, in_status_change_bits, /*cc=*/false,
				 /*vendor_defined=*/0));
	k_sleep(K_MSEC(SLEEP_MS));
	zassert_ok(pdc_get_connector_status(dev, &out));
	k_sleep(K_MSEC(SLEEP_MS));
	out_status_change_bits.raw_value = out.raw_conn_status_change_bits;

	zassert_equal(out_status_change_bits.connect_change, 0);
	zassert_equal(out_status_change_bits.external_supply_change, 0);
}

ZTEST_USER(tps6699x, test_get_bus_info)
{
	struct pdc_bus_info_t info;
	struct i2c_dt_spec i2c_spec = I2C_DT_SPEC_GET(TPS6699X_NODE);

	zassert_not_ok(pdc_get_bus_info(dev, NULL));

	zassert_ok(pdc_get_bus_info(dev, &info));
	zassert_equal(info.bus_type, PDC_BUS_TYPE_I2C);
	zassert_equal(info.i2c.bus, i2c_spec.bus);
	zassert_equal(info.i2c.addr, i2c_spec.addr);
}

ZTEST_USER(tps6699x, test_set_uor_tps)
{
	union uor_t in, out;
	int swap_to_dfp;
	int swap_to_ufp;

	in.raw_value = 0;
	out.raw_value = 0;

	in.accept_dr_swap = 1;
	in.swap_to_ufp = 1;
	in.connector_number = 1;

	access = ACCESS_OK;
	RESET_FAKE(tps_rw_port_control);
	tps_rw_port_control_fake.custom_fake = custom_fake_tps_rw_port_control;

	/* Test that data role preference is correctly set to swap_to_ufp */
	zassert_ok(pdc_set_uor(dev, in), "Failed to set uor");
	k_sleep(K_MSEC(SLEEP_MS));
	zassert_ok(emul_pdc_get_uor(emul, &out));
	emul_pdc_get_data_role_preference(emul, &swap_to_dfp, &swap_to_ufp);
	zassert_equal(out.raw_value, in.raw_value);
	zassert_equal(swap_to_ufp, 1);
	zassert_equal(swap_to_dfp, 0);

	/* Test that data role preference is correctly set to swap_to_dfp */
	in.swap_to_ufp = 0;
	in.swap_to_dfp = 1;
	zassert_ok(pdc_set_uor(dev, in), "Failed to set uor");
	k_sleep(K_MSEC(SLEEP_MS));
	zassert_ok(emul_pdc_get_uor(emul, &out));
	emul_pdc_get_data_role_preference(emul, &swap_to_dfp, &swap_to_ufp);
	zassert_equal(swap_to_ufp, 0);
	zassert_equal(swap_to_dfp, 1);

	/* Exercise tps_rw_port_control read failure */
	in.swap_to_ufp = 1;
	in.swap_to_dfp = 0;
	access = ACCESS_READ_FAIL;
	pdc_set_uor(dev, in);
	k_sleep(K_MSEC(SLEEP_MS));
	emul_pdc_get_data_role_preference(emul, &swap_to_dfp, &swap_to_ufp);
	zassert_equal(swap_to_ufp, 0);
	zassert_equal(swap_to_dfp, 1);

	/* Exercise tps_rw_port_control write failure */
	access = ACCESS_WRITE_FAIL;
	pdc_set_uor(dev, in);
	k_sleep(K_MSEC(SLEEP_MS));
	emul_pdc_get_data_role_preference(emul, &swap_to_dfp, &swap_to_ufp);
	zassert_equal(swap_to_ufp, 0);
	zassert_equal(swap_to_dfp, 1);
}
