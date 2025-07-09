/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "include/usb_common.h"
#include "usb_pd.h"

#include <zephyr/fff.h>
#include <zephyr/logging/log.h>
#include <zephyr/ztest.h>

LOG_MODULE_REGISTER(usb_utils_clamped_test);

FAKE_VALUE_FUNC(int, chipset_in_state, int);

#define PDO_FIXED_FLAGS \
	(PDO_FIXED_DUAL_ROLE | PDO_FIXED_DATA_SWAP | PDO_FIXED_COMM_CAP)

ZTEST_SUITE(pd_select_best_pdo_clamped, NULL, NULL, NULL, NULL, NULL);

__override int pd_is_valid_input_voltage(int mv)
{
	/* Don't allow charging from 15V. */
	if (mv == 15000) {
		return false;
	}

	return true;
}

/* Note - pd_extract_pdo_power() verified by legacy test/usb_common_test.c */

ZTEST(pd_select_best_pdo_clamped, test_board_values)
{
	LOG_INF("PD Power limits: %d mV, %d mA, %d mW",
		CONFIG_PLATFORM_EC_USB_PD_MAX_VOLTAGE_MV,
		CONFIG_PLATFORM_EC_USB_PD_MAX_CURRENT_MA,
		CONFIG_PLATFORM_EC_USB_PD_MAX_POWER_MW);

	/* This test suite assumes that the following board PD limits: */
	zassert_equal(30000, CONFIG_PLATFORM_EC_USB_PD_MAX_POWER_MW);
	zassert_equal(2000, CONFIG_PLATFORM_EC_USB_PD_MAX_CURRENT_MA);
	zassert_equal(15000, CONFIG_PLATFORM_EC_USB_PD_MAX_VOLTAGE_MV);
}

ZTEST(pd_select_best_pdo_clamped, test_max_voltage)
{
	uint32_t partner_src_pdo[] = {
		PDO_FIXED(5000, 1500, 0),
		PDO_FIXED(9000, 3000, 0),
		PDO_FIXED(12000, 4000, 0),
		PDO_FIXED(20000, 4000, 0),
	};
	uint32_t pdo;
	uint32_t ma, max_mv, min_mv;

	/* Should select PDO array index 3 despite having a higher wattage than
	 * needed.
	 */
	zassert_equal(pd_select_best_pdo(ARRAY_SIZE(partner_src_pdo),
					 partner_src_pdo,
					 CONFIG_USB_PD_MAX_VOLTAGE_MV, &pdo),
		      2, "Failed to select 15V/4A fixed PDO");

	pd_extract_pdo_power_unclamped(pdo, &ma, &max_mv, &min_mv);
	zassert_true(max_mv <= CONFIG_PLATFORM_EC_USB_PD_MAX_VOLTAGE_MV);
}

ZTEST(pd_select_best_pdo_clamped, test_valid_voltage)
{
	uint32_t partner_src_pdo[] = {
		PDO_FIXED(5000, 1500, 0),
		PDO_FIXED(9000, 3000, 0),
		PDO_FIXED(15000, 2000, 0),
		PDO_FIXED(20000, 3000, 0),
	};
	uint32_t pdo;

	/* Board code disallows charging from 15V, so the 9V PDO should be
	 * selected.
	 */
	zassert_equal(
		pd_select_best_pdo(ARRAY_SIZE(partner_src_pdo), partner_src_pdo,
				   CONFIG_USB_PD_MAX_VOLTAGE_MV, &pdo),
		1, "Failed to select 3V/3A fixed PDO because 15V disallowed");
}
