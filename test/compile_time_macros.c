/* Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Test compile_time_macros.h
 */

#include "common.h"
#include "test_util.h"


static int test_BIT(void)
{
	TEST_EQ(BIT(0),  0x00000001U, "%u");
	TEST_EQ(BIT(25), 0x02000000U, "%u");
	TEST_EQ(BIT(31), 0x80000000U, "%u");

	return EC_SUCCESS;
}

static int test_BIT_ULL(void)
{
	TEST_EQ(BIT_ULL(0),  0x0000000000000001ULL, "%Lu");
	TEST_EQ(BIT_ULL(25), 0x0000000002000000ULL, "%Lu");
	TEST_EQ(BIT_ULL(50), 0x0004000000000000ULL, "%Lu");
	TEST_EQ(BIT_ULL(63), 0x8000000000000000ULL, "%Lu");

	return EC_SUCCESS;
}

static int test_GENMASK(void)
{
	TEST_EQ(GENMASK(0, 0),   0x00000001U, "%u");
	TEST_EQ(GENMASK(31, 0),  0xFFFFFFFFU, "%u");
	TEST_EQ(GENMASK(4, 4),   0x00000010U, "%u");
	TEST_EQ(GENMASK(4, 0),   0x0000001FU, "%u");
	TEST_EQ(GENMASK(21, 21), 0x00200000U, "%u");
	TEST_EQ(GENMASK(31, 31), 0x80000000U, "%u");

	return EC_SUCCESS;
}

static int test_GENMASK_ULL(void)
{
	TEST_EQ(GENMASK_ULL(0, 0),   0x0000000000000001ULL, "%Lu");
	TEST_EQ(GENMASK_ULL(31, 0),  0x00000000FFFFFFFFULL, "%Lu");
	TEST_EQ(GENMASK_ULL(63, 0),  0xFFFFFFFFFFFFFFFFULL, "%Lu");
	TEST_EQ(GENMASK_ULL(4, 4),   0x0000000000000010ULL, "%Lu");
	TEST_EQ(GENMASK_ULL(4, 0),   0x000000000000001FULL, "%Lu");
	TEST_EQ(GENMASK_ULL(21, 21), 0x0000000000200000ULL, "%Lu");
	TEST_EQ(GENMASK_ULL(31, 31), 0x0000000080000000ULL, "%Lu");
	TEST_EQ(GENMASK_ULL(63, 63), 0x8000000000000000ULL, "%Lu");
	TEST_EQ(GENMASK_ULL(62, 60), 0x7000000000000000ULL, "%Lu");

	return EC_SUCCESS;
}

void run_test(int argc, char **argv)
{
	test_reset();

	RUN_TEST(test_BIT);
	RUN_TEST(test_BIT_ULL);
	RUN_TEST(test_GENMASK);
	RUN_TEST(test_GENMASK_ULL);

	test_print_result();
}
