/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Measure performance of the hardware True Random Number Generator (TRNG)
 * compared to the std::rand().
 */

#include "benchmark.h"
#include "console.h"
#include "trng.h"

#include <zephyr/ztest.h>

#include <array>
#include <cstdint>
#include <cstdlib>

ZTEST_SUITE(rng_benchmark, NULL, NULL, NULL, NULL, NULL);

ZTEST(rng_benchmark, test_trng_rand_bytes)
{
	constexpr int num_iterations = 100;
	Benchmark benchmark({ .num_iterations = num_iterations });
	std::array<uint32_t, num_iterations> trng_out;

	// Try the hardware true random number generator
	trng_out.fill(0);

	trng_init();
	auto result = benchmark.run("trng", [&trng_out]() {
		static int i = 0;
		trng_rand_bytes(&trng_out[i++], sizeof(uint32_t));
	});
	trng_exit();

	zassert_true(result.has_value());
	for (int i = 0; i < trng_out.size() - 1; ++i) {
		zassert_not_equal(trng_out[i], trng_out[i + 1]);
		cflush();
	}

	benchmark.print_results();
}

ZTEST(rng_benchmark, test_std_rand)
{
	constexpr int num_iterations = 100;
	Benchmark benchmark({ .num_iterations = num_iterations });
	std::array<int, num_iterations> rand_out;

	// Repeat the test using std::rand() for comparison
	rand_out.fill(0);
	auto result = benchmark.run("std::rand", [&rand_out]() {
		static int i = 0;
		rand_out[i++] = std::rand();
	});

	zassert_true(result.has_value());
	for (int i = 0; i < rand_out.size() - 1; ++i) {
		zassert_not_equal(rand_out[i], rand_out[i + 1]);
		cflush();
	}

	benchmark.print_results();
}
