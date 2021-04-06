/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Test USB common module.
 */
#include "test_util.h"
#include "usb_common.h"

#define PDO_FIXED_FLAGS \
	(PDO_FIXED_DUAL_ROLE | PDO_FIXED_DATA_SWAP | PDO_FIXED_COMM_CAP)

/* Test that a non-fixed PDO will never be selected by pd_find_pdo_index. */
test_static int test_pd_find_pdo_index(void)
{
	const uint32_t pd_snk_pdo[] = {
		PDO_FIXED(5000, 500, PDO_FIXED_FLAGS),
		PDO_VAR(4750, PD_MAX_VOLTAGE_MV, PD_MAX_CURRENT_MA),
		PDO_BATT(4750, PD_MAX_VOLTAGE_MV, PD_MAX_POWER_MW),
		PDO_FIXED(9000, 3000, PDO_FIXED_FLAGS),
		PDO_FIXED(12000, 3000, PDO_FIXED_FLAGS),
		PDO_FIXED(20000, 3000, PDO_FIXED_FLAGS),
	};
	const int pd_snk_pdo_cnt = ARRAY_SIZE(pd_snk_pdo);
	uint32_t pdo;

	TEST_EQ(pd_find_pdo_index(pd_snk_pdo_cnt, pd_snk_pdo, 5000, &pdo), 0,
		"%d");
	TEST_EQ(pd_find_pdo_index(pd_snk_pdo_cnt, pd_snk_pdo, 9000, &pdo), 3,
		"%d");
	TEST_EQ(pd_find_pdo_index(pd_snk_pdo_cnt, pd_snk_pdo, 10000, &pdo), 3,
		"%d");
	TEST_EQ(pd_find_pdo_index(pd_snk_pdo_cnt, pd_snk_pdo, 12000, &pdo), 4,
		"%d");
	TEST_EQ(pd_find_pdo_index(pd_snk_pdo_cnt, pd_snk_pdo, 15000, &pdo), 4,
		"%d");
	TEST_EQ(pd_find_pdo_index(pd_snk_pdo_cnt, pd_snk_pdo, 20000, &pdo), 5,
		"%d");
	return EC_SUCCESS;
}

void run_test(int argc, char **argv)
{
	RUN_TEST(test_pd_find_pdo_index);

	test_print_result();
}
