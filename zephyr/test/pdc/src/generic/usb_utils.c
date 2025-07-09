/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "include/usb_common.h"
#include "usb_pd.h"

#include <zephyr/fff.h>
#include <zephyr/ztest.h>

FAKE_VALUE_FUNC(int, chipset_in_state, int);

ZTEST(pdc_usb_utils, test_pd_extract_pdo_power_unclamped)
{
	uint32_t pdo, ma, max_mv, min_mv;

	pdo = PDO_FIXED(5000 /*mV*/, 3000 /*mA*/, 0);
	pd_extract_pdo_power_unclamped(pdo, &ma, &max_mv, &min_mv);
	zassert_equal(5000, min_mv);
	zassert_equal(5000, max_mv);
	zassert_equal(3000, ma);

	/* 0-volt PDO */
	pdo = PDO_FIXED(0 /*mV*/, 3000 /*mA*/, 0);
	pd_extract_pdo_power_unclamped(pdo, &ma, &max_mv, &min_mv);
	zassert_equal(0, min_mv);
	zassert_equal(0, max_mv);
	zassert_equal(0, ma);

	/* PDO in excess of board limits -- should not be clamped */
	pdo = PDO_FIXED(20000 /*mV*/,
			CONFIG_PLATFORM_EC_USB_PD_MAX_CURRENT_MA + 1000 /*mA*/,
			0);
	pd_extract_pdo_power_unclamped(pdo, &ma, &max_mv, &min_mv);
	zassert_equal(20000, min_mv);
	zassert_equal(20000, max_mv);
	zassert_equal(CONFIG_PLATFORM_EC_USB_PD_MAX_CURRENT_MA + 1000, ma);

	pdo = PDO_AUG(9000 /*mV*/, 15000 /*mV*/, 2000 /*mA*/);
	pd_extract_pdo_power_unclamped(pdo, &ma, &max_mv, &min_mv);
	zassert_equal(9000, min_mv, "%d", min_mv);
	zassert_equal(15000, max_mv);
	zassert_equal(2000, ma);

	pdo = PDO_VAR(5000 /*mV*/, 20000 /*mV*/, 1500 /*mA*/);
	pd_extract_pdo_power_unclamped(pdo, &ma, &max_mv, &min_mv);
	zassert_equal(5000, min_mv);
	zassert_equal(20000, max_mv);
	zassert_equal(1500, ma);

	pdo = PDO_BATT(5000 /*mV*/, 20000 /*mV*/, 10000 /*mW*/);
	pd_extract_pdo_power_unclamped(pdo, &ma, &max_mv, &min_mv);
	zassert_equal(5000, min_mv);
	zassert_equal(20000, max_mv);
	zassert_equal(2000, ma);
}

ZTEST_SUITE(pdc_usb_utils, NULL, NULL, NULL, NULL, NULL);
