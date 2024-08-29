/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "drivers/pdc.h"
#include "drivers/ucsi_v3.h"
#include "emul/emul_pdc.h"
#include "emul/emul_tps6699x.h"
#include "pdc_trace_msg.h"

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/emul.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/ztest.h>

LOG_MODULE_REGISTER(test_tps6699x, LOG_LEVEL_DBG);
#define SLEEP_MS 200

#define TPS6699X_NODE DT_NODELABEL(pdc_emul1)

static const struct emul *emul = EMUL_DT_GET(TPS6699X_NODE);
static const struct device *dev = DEVICE_DT_GET(TPS6699X_NODE);

static void tps6699x_before_test(void *data)
{
	emul_pdc_reset(emul);
	emul_pdc_set_response_delay(emul, 0);
	if (IS_ENABLED(CONFIG_TEST_PDC_MESSAGE_TRACING)) {
		set_pdc_trace_msg_mocks();
	}

	zassert_ok(emul_pdc_idle_wait(emul));
}

ZTEST_SUITE(tps6699x, NULL, NULL, tps6699x_before_test, NULL, NULL);

/* Driver should keep returning cached connector status bits until they are
 * acked via ACK_CC_CI.
 */
ZTEST_USER(tps6699x, test_connector_status_caching)
{
	union connector_status_t in, out;
	union conn_status_change_bits_t in_status_change_bits,
		out_status_change_bits;

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
