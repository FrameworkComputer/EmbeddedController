/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "include/usb_common.h"
#include "usb_pd.h"

#include <zephyr/fff.h>
#include <zephyr/logging/log.h>
#include <zephyr/ztest.h>

LOG_MODULE_REGISTER(select_best_pdo_variable);

FAKE_VALUE_FUNC(int, chipset_in_state, int);

#define PDO_FIXED_FLAGS \
	(PDO_FIXED_DUAL_ROLE | PDO_FIXED_DATA_SWAP | PDO_FIXED_COMM_CAP)

ZTEST_SUITE(pd_select_best_pdo_variable, NULL, NULL, NULL, NULL, NULL);

/* Test that we will select a variable PDO that offers the maximum
 * allowed voltage, and that a variable PDO that exceeds the maximum
 * allowed voltage in the PD specification is skipped.
 */
ZTEST(pd_select_best_pdo_variable, test_variable_max_mv)
{
	const uint32_t partner_src_pdo[] = {
		PDO_FIXED(5000, 500, PDO_FIXED_FLAGS),
		PDO_VAR(18000, 21000, 1870),
		PDO_VAR(27000, 28000, 1500),
	};
	uint32_t pdo;

	struct {
		int max_mv;
		int expected_index;
	} test[] = {
		{
			.max_mv = 5000,
			.expected_index = 0,
		},
		{
			.max_mv = 21000,
			.expected_index = 1,
		},
		{
			/* Variable PDO exceeding PD spec limit should be
			 * skipped.
			 */
			.max_mv = CONFIG_PLATFORM_EC_USB_PD_MAX_VOLTAGE_MV,
			.expected_index = 1,
		},
	};

	for (int i = 0; i < ARRAY_SIZE(test); i++) {
		LOG_INF("Find best PDO, max %d mV, expect PDO index %d",
			test[i].max_mv, test[i].expected_index);
		zassert_equal(pd_select_best_pdo(ARRAY_SIZE(partner_src_pdo),
						 partner_src_pdo,
						 test[i].max_mv, &pdo),
			      test[i].expected_index,
			      "Max %d mV - failed to select fixed PDO index %d",
			      test[i].max_mv, test[i].expected_index);
	}
}

ZTEST(pd_select_best_pdo_variable, test_board_values)
{
	LOG_INF("PD Power limits: %d mV, %d mA, %d mW",
		CONFIG_PLATFORM_EC_USB_PD_MAX_VOLTAGE_MV,
		CONFIG_PLATFORM_EC_USB_PD_MAX_CURRENT_MA,
		CONFIG_PLATFORM_EC_USB_PD_MAX_POWER_MW);

	/* This test suite assumes that the following board PD limits: */
	zassert_equal(65000, CONFIG_PLATFORM_EC_USB_PD_MAX_POWER_MW);
	zassert_equal(3000, CONFIG_PLATFORM_EC_USB_PD_MAX_CURRENT_MA);
	zassert_equal(28000, CONFIG_PLATFORM_EC_USB_PD_MAX_VOLTAGE_MV);
}
