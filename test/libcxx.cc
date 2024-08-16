/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/**
 * @brief Tests for functions in libc++ (libcxx).
 */

#include "test_util.h"
#include "timer.h"
#include "util.h"

#include <chrono>

static int test_system_clock()
{
	auto start = std::chrono::system_clock::now();
	udelay(0.5 * SECOND);
	auto end = std::chrono::system_clock::now();
	auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
				  end - start)
				  .count();
	TEST_NEAR(elapsed_ms, 500ULL, 5ULL, "%llu");

	return EC_SUCCESS;
}

test_static int test_aligned_allocation()
{
	constexpr std::size_t kAlignment = 16;
	int *aligned = new (std::align_val_t(kAlignment)) int;
	TEST_ASSERT(aligned != nullptr);
	TEST_EQ(is_aligned(reinterpret_cast<uint32_t>(aligned), kAlignment),
		true, "%d");
	delete aligned;
	return EC_SUCCESS;
}

void run_test(int, const char **)
{
	test_reset();
	RUN_TEST(test_system_clock);
	RUN_TEST(test_aligned_allocation);
	test_print_result();
}
