/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/**
 * Test if unaligned access works properly
 */

#include "common.h"
#include "console.h"
#include "test_util.h"

extern "C" {
#include "shared_mem.h"
#include "timer.h"
}

#include <array>
#include <cstdio>
#include <cstring>

test_static int test_unaligned_access()
{
	/* This is equivalent to {0xff, 0x09, 0x04, 0x06, 0x04, 0x06, 0x07,
	 * 0xed, 0x0a, 0x0b, 0x0d, 0x38, 0xbd, 0x57, 0x59} */
	alignas(int32_t) constexpr std::array<int8_t, 15> test_array = {
		-1, 9, 4, 6, 4, 6, 7, -19, 10, 11, 13, 56, -67, 87, 89
	};

	constexpr std::array<int32_t, 12> expected_results = {
		0x060409ff,
		0x04060409,
		0x06040604,
		0x07060406,
		static_cast<int32_t>(0xed070604),
		0x0aed0706,
		0x0b0aed07,
		0x0d0b0aed,
		0x380d0b0a,
		static_cast<int32_t>(0xbd380d0b),
		0x57bd380d,
		0x5957bd38
	};

	/* If i % 4 = 0, we have an aligned access. Otherwise, it is
	   unaligned access. */
	for (int i = 0; i < expected_results.size(); ++i) {
		const int32_t *test_array_ptr =
			reinterpret_cast<const int32_t *>(test_array.data() +
							  i);

		TEST_EQ(*test_array_ptr, expected_results[i], "0x%08x");
	}

	return EC_SUCCESS;
}

test_static int benchmark_unaligned_access_memcpy()
{
	int i;
	timestamp_t t0, t1, t2, t3;
	char *buf;
	const int buf_size = 1000;
	const int len = 400;
	const int dest_offset = 500;
	const int iteration = 1000;

	TEST_ASSERT(shared_mem_acquire(buf_size, &buf) == EC_SUCCESS);

	for (i = 0; i < len; ++i)
		buf[i] = i & 0x7f;
	for (i = len; i < buf_size; ++i)
		buf[i] = 0;

	t0 = get_time();
	for (i = 0; i < iteration; ++i) {
		memcpy(buf + dest_offset + 1, buf, len); /* unaligned */
	}
	t1 = get_time();
	TEST_ASSERT_ARRAY_EQ(buf + dest_offset + 1, buf, len);
	ccprintf(" (speed gain: %" PRId64 " ->", t1.val - t0.val);

	t2 = get_time();
	for (i = 0; i < iteration; ++i) {
		memcpy(buf + dest_offset, buf, len); /* aligned */
	}
	t3 = get_time();
	ccprintf(" %" PRId64 " us) ", t3.val - t2.val);
	TEST_ASSERT_ARRAY_EQ(buf + dest_offset, buf, len);
	return EC_SUCCESS;
}

test_static int benchmark_unaligned_access_array()
{
	timestamp_t t0, t1, t2, t3;
	const int iteration = 1000;

	alignas(int32_t) std::array<int8_t, 100> test_array_1;
	std::array<int32_t, 20> test_array_2;
	constexpr std::array<int32_t, 20> test_array_3 = {
		67305985,   134678021,	202050057,  269422093,	336794129,
		404166165,  471538201,	538910237,  606282273,	673654309,
		741026345,  808398381,	875770417,  943142453,	1010514489,
		1077886525, 1145258561, 1212630597, 1280002633, 1347374669
	};
	constexpr std::array<int32_t, 20> test_array_4 = {
		50462976,   117835012,	185207048,  252579084,	319951120,
		387323156,  454695192,	522067228,  589439264,	656811300,
		724183336,  791555372,	858927408,  926299444,	993671480,
		1061043516, 1128415552, 1195787588, 1263159624, 1330531660
	};

	for (int i = 0; i < test_array_1.size(); ++i) {
		test_array_1[i] = static_cast<int8_t>(i);
	}

	t0 = get_time();
	for (int t = 0; t < iteration; ++t) {
		const int32_t *test_array_1_ptr =
			reinterpret_cast<const int32_t *>(test_array_1.data() +
							  1);

		for (int i = 0; i < test_array_2.size(); ++i) {
			test_array_2[i] = (*test_array_1_ptr++); /* unaligned */
		}
		TEST_ASSERT_ARRAY_EQ(test_array_2.data(), test_array_3.data(),
				     test_array_2.size());
	}
	t1 = get_time();
	ccprintf(" (speed gain: %" PRId64 " ->", t1.val - t0.val);

	t2 = get_time();
	for (int t = 0; t < iteration; ++t) {
		const int32_t *test_array_1_ptr =
			reinterpret_cast<const int32_t *>(test_array_1.data());

		for (int i = 0; i < test_array_2.size(); ++i) {
			test_array_2[i] = (*test_array_1_ptr++); /* aligned */
		}
		TEST_ASSERT_ARRAY_EQ(test_array_2.data(), test_array_4.data(),
				     test_array_2.size());
	}
	t3 = get_time();
	ccprintf(" %" PRId64 " us) ", t3.val - t2.val);

	return EC_SUCCESS;
}

extern "C" void run_test(int, const char **)
{
	test_reset();
	RUN_TEST(test_unaligned_access);
	RUN_TEST(benchmark_unaligned_access_memcpy);
	RUN_TEST(benchmark_unaligned_access_array);
	test_print_result();
}
