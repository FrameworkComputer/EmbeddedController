/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/**
 * Test if unaligned access works properly
 */

#include "panic.h"

#include <zephyr/ztest.h>

#include <array>
#include <cstdio>
#include <cstring>

ZTEST_SUITE(unaligned_access, NULL, NULL, NULL, NULL, NULL);

ZTEST(unaligned_access, test_unaligned_access)
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
		zassert_equal(*test_array_ptr, expected_results[i]);
	}
}

ZTEST(unaligned_access,
      test_crash_unaligned_disabled_if_unaligned_access_allowed)
{
	const char *crash_unaligned[2] = { "crash", "unaligned" };

	zassert_equal(test_command_crash(2, crash_unaligned), EC_ERROR_PARAM1);
}
