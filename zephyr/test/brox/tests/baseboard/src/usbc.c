/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "usb_pd.h"

#include <zephyr/device.h>
#include <zephyr/fff.h>
#include <zephyr/ztest.h>

ZTEST_SUITE(usbc, NULL, NULL, NULL, NULL, NULL);

ZTEST(usbc, test_board_get_pd_port_location)
{
	zassert_equal(board_get_pd_port_location(0),
		      EC_PD_PORT_LOCATION_LEFT_FRONT);
	zassert_equal(board_get_pd_port_location(1),
		      EC_PD_PORT_LOCATION_LEFT_BACK);
	zassert_equal(board_get_pd_port_location(2),
		      EC_PD_PORT_LOCATION_UNKNOWN);
}

/*
 * Runtime USB-C port config tests
 */

FAKE_VALUE_FUNC(int, cbi_get_sku_id, uint32_t *);

/** Used to configure the custom fake below */
static uint32_t fake_sku = 0;

/** Custom fake to output controlled SKU IDs from CBI */
static int cbi_get_sku_id_custom_fake(uint32_t *sku)
{
	*sku = fake_sku;

	return 0;
}

static void usbc_runtime_reset(void *f)
{
	ARG_UNUSED(f);

	fake_sku = 0;
	RESET_FAKE(cbi_get_sku_id);
}

ZTEST_SUITE(usbc_runtime, NULL, NULL, usbc_runtime_reset, usbc_runtime_reset,
	    NULL);

/** Brox-supplied port information function */
int board_get_pdc_for_port(int port, const struct device **dev);

/** Declare fake device structs to return */
const struct device DEVICE_DT_NAME_GET(DT_NODELABEL(pdc_power_p0));
const struct device DEVICE_DT_NAME_GET(DT_NODELABEL(pdc_power_p1));
const struct device DEVICE_DT_NAME_GET(DT_NODELABEL(pdc_power_p0_ti));
const struct device DEVICE_DT_NAME_GET(DT_NODELABEL(pdc_power_p1_ti));

ZTEST(usbc_runtime, test_board_get_pdc_for_port__null)
{
	int rv = board_get_pdc_for_port(0, NULL);

	zassert_equal(-EINVAL, rv, "Expected -EINVAL, got %d", rv);
}

ZTEST(usbc_runtime, test_board_get_pdc_for_port__bad_cbi)
{
	const struct device *dev;
	int rv;

	cbi_get_sku_id_fake.return_val = 1;

	rv = board_get_pdc_for_port(0, &dev);

	zassert_equal(-EIO, rv, "Expected -EIO, got %d", rv);
}

ZTEST(usbc_runtime, test_board_get_pdc_for_port__bad_sku)
{
	const struct device *dev;
	int rv;

	cbi_get_sku_id_fake.custom_fake = cbi_get_sku_id_custom_fake;
	fake_sku = 0xAABBCCDD;

	rv = board_get_pdc_for_port(0, &dev);

	zassert_equal(-ENOENT, rv, "Expected -ENOENT, got %d", rv);
}

ZTEST(usbc_runtime, test_board_get_pdc_for_port__rtk_skus)
{
	const struct device *dev;
	const uint32_t rtk_skus[] = { 0x01, 0x02, 0x03, 0x21, 0x22, 0x23 };
	int rv;

	cbi_get_sku_id_fake.custom_fake = cbi_get_sku_id_custom_fake;

	for (int i = 0; i < ARRAY_SIZE(rtk_skus); i++) {
		fake_sku = rtk_skus[i];

		rv = board_get_pdc_for_port(0, &dev);
		zassert_ok(rv, "Got non-zero result for port 0, sku 0x%0x: %d",
			   fake_sku, rv);
		zassert_equal(dev, DEVICE_DT_GET(DT_NODELABEL(pdc_power_p0)),
			      "Got %p, expected %d", dev,
			      DEVICE_DT_GET(DT_NODELABEL(pdc_power_p0)));

		rv = board_get_pdc_for_port(1, &dev);
		zassert_ok(rv, "Got non-zero result for port 1, sku 0x%0x: %d",
			   fake_sku, rv);
		zassert_equal(dev, DEVICE_DT_GET(DT_NODELABEL(pdc_power_p1)),
			      "Got %p, expected %d", dev,
			      DEVICE_DT_GET(DT_NODELABEL(pdc_power_p1)));

		rv = board_get_pdc_for_port(2, &dev);
		zassert_equal(-ENOENT, rv, "Expected -ENOENT, got %d", rv);
	}
}

ZTEST(usbc_runtime, test_board_get_pdc_for_port__ti_skus)
{
	const struct device *dev;
	int rv;

	cbi_get_sku_id_fake.custom_fake = cbi_get_sku_id_custom_fake;

	fake_sku = 0x24;

	rv = board_get_pdc_for_port(0, &dev);
	zassert_ok(rv, "Got non-zero result for port 0, sku 0x%0x: %d",
		   fake_sku, rv);
	zassert_equal(dev, DEVICE_DT_GET(DT_NODELABEL(pdc_power_p0_ti)),
		      "Got %p, expected %d", dev,
		      DEVICE_DT_GET(DT_NODELABEL(pdc_power_p0_ti)));

	rv = board_get_pdc_for_port(1, &dev);
	zassert_ok(rv, "Got non-zero result for port 1, sku 0x%0x: %d",
		   fake_sku, rv);
	zassert_equal(dev, DEVICE_DT_GET(DT_NODELABEL(pdc_power_p1_ti)),
		      "Got %p, expected %d", dev,
		      DEVICE_DT_GET(DT_NODELABEL(pdc_power_p1_ti)));

	rv = board_get_pdc_for_port(2, &dev);
	zassert_equal(-ENOENT, rv, "Expected -ENOENT, got %d", rv);
}
