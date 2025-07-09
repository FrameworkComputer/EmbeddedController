/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "include/usb_common.h"
#include "usb_pd.h"

#include <zephyr/logging/log.h>
#include <zephyr/ztest.h>

LOG_MODULE_REGISTER(select_best_pdo);

#define PDO_FIXED_FLAGS \
	(PDO_FIXED_DUAL_ROLE | PDO_FIXED_DATA_SWAP | PDO_FIXED_COMM_CAP)

ZTEST_SUITE(pd_select_best_pdo, NULL, NULL, NULL, NULL, NULL);

/* Test that a non-fixed PDO will never be selected by pd_select_best_pdo. */
ZTEST(pd_select_best_pdo, test_fixed_only)
{
	/* Note - this set of PDOs violates the PD specification. */
	const uint32_t partner_src_pdo[] = {
		PDO_FIXED(5000, 500, PDO_FIXED_FLAGS),
		PDO_VAR(4750, CONFIG_USB_PD_MAX_VOLTAGE_MV,
			CONFIG_USB_PD_MAX_CURRENT_MA),
		PDO_BATT(4750, CONFIG_USB_PD_MAX_VOLTAGE_MV,
			 CONFIG_USB_PD_MAX_POWER_MW),
		PDO_FIXED(9000, 3000, PDO_FIXED_FLAGS),
		PDO_FIXED(12000, 3000, PDO_FIXED_FLAGS),
		PDO_FIXED(20000, 3000, PDO_FIXED_FLAGS),
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
			.max_mv = 9000,
			.expected_index = 3,
		},
		{
			.max_mv = 10000,
			.expected_index = 3,
		},
		{
			.max_mv = 12000,
			.expected_index = 4,
		},
		{
			.max_mv = 15000,
			.expected_index = 4,
		},
		{
			.max_mv = 20000,
			.expected_index = 5,
		},
	};

	if (!IS_ENABLED(CONFIG_PLATFORM_EC_USB_PD_ONLY_FIXED_PDOS)) {
		ztest_test_skip();
		return;
	}

	for (int i = 0; i < ARRAY_SIZE(test); i++) {
		LOG_INF("Find best PDO, max %d mV, expect %d mV",
			test[i].max_mv,
			PDO_FIXED_VOLTAGE(
				partner_src_pdo[test[i].expected_index]));
		zassert_equal(pd_select_best_pdo(ARRAY_SIZE(partner_src_pdo),
						 partner_src_pdo,
						 test[i].max_mv, &pdo),
			      test[i].expected_index,
			      "Max %d mV - failed to select fixed PDO index %d",
			      test[i].max_mv, test[i].expected_index);
	}
}

ZTEST(pd_select_best_pdo, test_board_values)
{
	LOG_INF("PD Power limits: %d mV, %d mA, %d mW",
		CONFIG_PLATFORM_EC_USB_PD_MAX_VOLTAGE_MV,
		CONFIG_PLATFORM_EC_USB_PD_MAX_CURRENT_MA,
		CONFIG_PLATFORM_EC_USB_PD_MAX_POWER_MW);

	/* This test suite assumes that the following board PD limits: */
	zassert_equal(60000, CONFIG_PLATFORM_EC_USB_PD_MAX_POWER_MW);
	zassert_equal(3000, CONFIG_PLATFORM_EC_USB_PD_MAX_CURRENT_MA);
	zassert_equal(20000, CONFIG_PLATFORM_EC_USB_PD_MAX_VOLTAGE_MV);
}

ZTEST(pd_select_best_pdo, test_fixed_batt_var_pdo)
{
	/* The USB PD Spec indicates that the capabilities
	 * message is sent in the following order:
	 *   vSafe5V Fixed PDO
	 *   Fixed PDOs (if present)
	 *   Battery PDOs (if present)
	 *   Variable PDOs (if present)
	 */
	const uint32_t partner_src_pdo[] = {
		PDO_FIXED(5000, 500, PDO_FIXED_FLAGS),
		PDO_FIXED(9000, 3000, PDO_FIXED_FLAGS),
		PDO_FIXED(12000, 3000, PDO_FIXED_FLAGS),
		PDO_BATT(14000, 18000, 36000),
		PDO_VAR(19000, 20000, 3000),
	};
	uint32_t pdo;
	uint32_t ma, max_mv, min_mv;

	struct {
		int max_mv;
		int expected_index;
	} test[] = {
		{
			.max_mv = 5000,
			.expected_index = 0,
		},
		{
			.max_mv = 6000,
			.expected_index = 0,
		},
		{
			.max_mv = 10000,
			.expected_index = 1,
		},
		{
			.max_mv = 12000,
			.expected_index = 2,
		},
		{
			.max_mv = 15000,
			.expected_index = 2,
		},
		{
			.max_mv = 18000,
			.expected_index = 3,
		},
		{
			.max_mv = 20000,
			.expected_index = 4,
		},
	};

	/* This test verifies non-fixed PDOs. */
	if (IS_ENABLED(CONFIG_PLATFORM_EC_USB_PD_ONLY_FIXED_PDOS)) {
		ztest_test_skip();
		return;
	}

	for (int i = 0; i < ARRAY_SIZE(test); i++) {
		LOG_INF("Find best PDO, max %d mV, expect index %d",
			test[i].max_mv, test[i].expected_index);
		zassert_equal(pd_select_best_pdo(ARRAY_SIZE(partner_src_pdo),
						 partner_src_pdo,
						 test[i].max_mv, &pdo),
			      test[i].expected_index,
			      "Max %d mV - failed to select fixed PDO index %d",
			      test[i].max_mv, test[i].expected_index);

		/* Additional check that the selected PDO's max
		 * voltage doesn't exceed the configured maximum
		 * voltage.
		 */
		pd_extract_pdo_power_unclamped(pdo, &ma, &max_mv, &min_mv);
		zassert_true(max_mv <= test[i].max_mv);
	}
}

ZTEST(pd_select_best_pdo, test_augmented_pdo)
{
	uint32_t partner_src_pdo[] = {
		PDO_FIXED(5000, 500, 0),
		PDO_AUG(5000, 20000, 3000),
	};
	uint32_t pdo;

	/* The augmented PDO should be skipped and the vSafe5V fixed PDO
	 * should be selected.
	 */
	zassert_equal(
		pd_select_best_pdo(ARRAY_SIZE(partner_src_pdo), partner_src_pdo,
				   CONFIG_USB_PD_MAX_VOLTAGE_MV, &pdo),
		0, "Failed to select fixed PDO when augmented PDO present");
}

/* PD sources are permitted to offer no capabilities. In this case the source
 * still provides a fixed PDO with the voltage set to 5V and the maximum
 * current set to 0mA.
 *
 * Verify that the selection logic still picks the vSafe5V PDO.
 */
ZTEST(pd_select_best_pdo, test_fixed_5v_0a)
{
	const uint32_t partner_src_pdo[] = {
		PDO_FIXED(5000, 0, PDO_FIXED_FLAGS),
	};
	uint32_t pdo;

	zassert_equal(pd_select_best_pdo(ARRAY_SIZE(partner_src_pdo),
					 partner_src_pdo,
					 CONFIG_USB_PD_MAX_VOLTAGE_MV, &pdo),
		      0, "Failed to select 5V/0mA fixed PDO");
}

ZTEST(pd_select_best_pdo, test_fixed_0v)
{
	const uint32_t partner_src_pdo[] = {
		PDO_FIXED(5000, 500, PDO_FIXED_FLAGS),
		PDO_FIXED(0, 3000, PDO_FIXED_FLAGS),
		PDO_BATT(0, 0, 10000),
		PDO_VAR(0, 0, 2000),
		PDO_VAR(19000, 20000, 2000),
	};
	uint32_t pdo;
	int expected_index;
	int selected_index;

	if (IS_ENABLED(CONFIG_PLATFORM_EC_USB_PD_ONLY_FIXED_PDOS)) {
		expected_index = 0;
	} else {
		expected_index = 4;
	}
	selected_index = pd_select_best_pdo(ARRAY_SIZE(partner_src_pdo),
					    partner_src_pdo,
					    CONFIG_USB_PD_MAX_VOLTAGE_MV, &pdo);
	zassert_equal(
		selected_index, expected_index,
		"Incorrect PDO (%d) selected when 0V PDOs present, expected %d",
		selected_index, expected_index);
}

ZTEST(pd_select_best_pdo, test_select_higher_wattage)
{
	uint32_t partner_src_pdo[] = {
		PDO_FIXED(5000, 1500, 0),  PDO_FIXED(9000, 3000, 0),
		PDO_FIXED(15000, 4000, 0), PDO_FIXED(20000, 4000, 0),
		PDO_FIXED(21000, 5000, 0),
	};
	uint32_t pdo;

	/* Should select PDO array index 3 despite having a higher wattage than
	 * needed.
	 */
	zassert_equal(pd_select_best_pdo(ARRAY_SIZE(partner_src_pdo),
					 partner_src_pdo,
					 CONFIG_USB_PD_MAX_VOLTAGE_MV, &pdo),
		      3, "Failed to select 20V/4A fixed PDO");
}

ZTEST(pd_select_best_pdo, test_low_high_preference)
{
	uint32_t partner_src_pdo[] = {
		PDO_FIXED(5000, 500, 0),
		PDO_FIXED(15000, 3000, 0),
		PDO_FIXED(20000, 2250, 0),
	};
	uint32_t pdo;
	int expected_index;

	if (IS_ENABLED(CONFIG_PLATFORM_EC_USB_PD_PREFER_LOW_VOLTAGE)) {
		LOG_INF("Lower voltage PDO is preferred");
		expected_index = 1;
	} else if (IS_ENABLED(CONFIG_PLATFORM_EC_USB_PD_PREFER_HIGH_VOLTAGE)) {
		LOG_INF("Higher voltage PDO is preferred");
		expected_index = 2;
	} else {
		LOG_INF("Default PDO selection policy");
		/* First PDO with equivalent wattage should be picked */
		expected_index = 1;
	}

	zassert_equal(
		pd_select_best_pdo(ARRAY_SIZE(partner_src_pdo), partner_src_pdo,
				   CONFIG_USB_PD_MAX_VOLTAGE_MV, &pdo),
		expected_index, "Failed to select fixed PDO based on policy");

	/* This isn't valid - Fixed PDOs are supposed to be ordered by voltage
	 * but this ensures we have full code coverage.
	 */
	partner_src_pdo[1] = PDO_FIXED(20000, 2250, 0);
	partner_src_pdo[2] = PDO_FIXED(15000, 3000, 0);

	if (IS_ENABLED(CONFIG_PLATFORM_EC_USB_PD_PREFER_LOW_VOLTAGE)) {
		expected_index = 2;
	} else if (IS_ENABLED(CONFIG_PLATFORM_EC_USB_PD_PREFER_HIGH_VOLTAGE)) {
		expected_index = 1;
	} else {
		/* First PDO with equivalent wattage should be picked */
		expected_index = 1;
	}

	zassert_equal(
		pd_select_best_pdo(ARRAY_SIZE(partner_src_pdo), partner_src_pdo,
				   CONFIG_USB_PD_MAX_VOLTAGE_MV, &pdo),
		expected_index,
		"Failed to select fixed PDO based on policy with out of order PDOs");
}
