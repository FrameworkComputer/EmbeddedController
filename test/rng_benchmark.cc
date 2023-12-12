/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Measure performance of the hardware True Random Number Generator (TRNG)
 * compared to the std::rand().
 */

#include "benchmark.h"
#include "console.h"

#include <array>
#include <cstdint>
#include <cstdlib>

extern "C" {
#include "test_util.h"
#include "trng.h"
}

test_static int test_trng_rand_bytes()
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

	TEST_ASSERT(result.has_value());
	for (int i = 0; i < trng_out.size() - 1; ++i) {
		TEST_NE(trng_out[i], trng_out[i + 1], "%d");
		cflush();
	}

	benchmark.print_results();
	return EC_SUCCESS;
}

test_static int test_trng_rand_bytes_toggle()
{
	constexpr int num_iterations = 10;
	Benchmark benchmark({ .num_iterations = num_iterations });
	std::array<uint32_t, num_iterations> trng_out;

	// Repeat the test by turning the TNRG on and off at each iteration
	trng_out.fill(0);
	auto result = benchmark.run("trng_on_off", [&trng_out]() {
		trng_init();
		static int i = 0;
		trng_rand_bytes(&trng_out[i++], sizeof(uint32_t));
		trng_exit();
	});

	TEST_ASSERT(result.has_value());
	for (int i = 0; i < trng_out.size() - 1; ++i) {
		TEST_NE(trng_out[i], trng_out[i + 1], "%d");
		cflush();
	}

	benchmark.print_results();
	return EC_SUCCESS;
}

test_static int test_std_rand()
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

	TEST_ASSERT(result.has_value());
	for (int i = 0; i < rand_out.size() - 1; ++i) {
		TEST_NE(rand_out[i], rand_out[i + 1], "%d");
		cflush();
	}

	benchmark.print_results();
	return EC_SUCCESS;
}

void run_test(int argc, const char **argv)
{
	test_reset();
	RUN_TEST(test_trng_rand_bytes);
	RUN_TEST(test_trng_rand_bytes_toggle);
	RUN_TEST(test_std_rand);
	test_print_result();
}