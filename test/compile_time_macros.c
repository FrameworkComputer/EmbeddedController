/* Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Test compile_time_macros.h
 */

#include "stdbool.h"
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

static int test_WRITE_BIT(void)
{
	uint8_t r8 __maybe_unused;
	uint16_t r16 __maybe_unused;
	uint32_t r32 __maybe_unused;

	r8 = 0;
	WRITE_BIT(r8, 0, true);
	TEST_EQ(r8, 0x01, "%u");
	WRITE_BIT(r8, 1, true);
	TEST_EQ(r8, 0x03, "%u");
	WRITE_BIT(r8, 5, true);
	TEST_EQ(r8, 0x23, "%u");
	WRITE_BIT(r8, 0, false);
	TEST_EQ(r8, 0x22, "%u");

	r16 = 0;
	WRITE_BIT(r16, 0, true);
	TEST_EQ(r16, 0x0001, "%u");
	WRITE_BIT(r16, 9, true);
	TEST_EQ(r16, 0x0201, "%u");
	WRITE_BIT(r16, 15, true);
	TEST_EQ(r16, 0x8201, "%u");
	WRITE_BIT(r16, 0, false);
	TEST_EQ(r16, 0x8200, "%u");

	r32 = 0;
	WRITE_BIT(r32, 0, true);
	TEST_EQ(r32, 0x00000001, "%u");
	WRITE_BIT(r32, 25, true);
	TEST_EQ(r32, 0x02000001, "%u");
	WRITE_BIT(r32, 31, true);
	TEST_EQ(r32, 0x82000001, "%u");
	WRITE_BIT(r32, 0, false);
	TEST_EQ(r32, 0x82000000, "%u");

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

test_static int test_IS_ARRAY(void)
{
	int array[5];
	int *pointer = array;

	TEST_EQ(_IS_ARRAY(array), true, "%d");
	TEST_EQ(_IS_ARRAY(pointer), false, "%d");

	return EC_SUCCESS;
}

void run_test(int argc, char **argv)
{
	test_reset();

	RUN_TEST(test_BIT);
	RUN_TEST(test_BIT_ULL);
	RUN_TEST(test_WRITE_BIT);
	RUN_TEST(test_GENMASK);
	RUN_TEST(test_GENMASK_ULL);
	RUN_TEST(test_IS_ARRAY);

	test_print_result();
}
