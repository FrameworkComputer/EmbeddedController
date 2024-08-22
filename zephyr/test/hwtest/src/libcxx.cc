/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/**
 * @brief Tests for functions in libc++ (libcxx).
 */

#include "timer.h"

#include <zephyr/ztest.h>

#include <chrono>

ZTEST_SUITE(libcxx, NULL, NULL, NULL, NULL, NULL);

/* TODO(b/357798784): Upstream to Zephyr. */
ZTEST(libcxx, test_system_clock)
{
	auto start = std::chrono::system_clock::now();
	udelay(0.5 * SECOND);
	auto end = std::chrono::system_clock::now();
	auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
				  end - start)
				  .count();
	zassert_within(elapsed_ms, 500ULL, 5ULL);
}
