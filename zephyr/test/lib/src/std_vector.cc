/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Basic test of std::vector and dynamic memory allocation.
 */

#include "common.h"
#include "console.h"

#include <zephyr/ztest.h>

#include <array>
#include <vector>

/* TODO(b/357798784): Upstream to Zephyr. */
ZTEST_SUITE(std_vector, NULL, NULL, NULL, NULL, NULL);

ZTEST(std_vector, test_stack_init_elements)
{
	std::vector<int32_t> vec{ 10, 11, 12, 13, 14 };

	zassert_equal(static_cast<int32_t>(vec.size()), 5);
	zassert_equal(vec[0], 10);
	zassert_equal(vec[1], 11);
	zassert_equal(vec[2], 12);
	zassert_equal(vec[3], 13);
	zassert_equal(vec[4], 14);
}

ZTEST(std_vector, test_static_init_elements)
{
	static std::vector<int32_t> vec{ 20, 21, 22, 23, 24 };

	zassert_equal(static_cast<int32_t>(vec.size()), 5);
	zassert_equal(vec[0], 20);
	zassert_equal(vec[1], 21);
	zassert_equal(vec[2], 22);
	zassert_equal(vec[3], 23);
	zassert_equal(vec[4], 24);
}

std::vector<int32_t> global_vec{ 30, 31, 32, 33, 34 };
ZTEST(std_vector, test_global_init_elements)
{
	zassert_equal(static_cast<int32_t>(global_vec.size()), 5);
	zassert_equal(global_vec[0], 30);
	zassert_equal(global_vec[1], 31);
	zassert_equal(global_vec[2], 32);
	zassert_equal(global_vec[3], 33);
	zassert_equal(global_vec[4], 34);
}

ZTEST(std_vector, test_push_back_elements)
{
	std::vector<int32_t> vec;

	vec.push_back(0);
	vec.push_back(1);
	vec.push_back(2);
	vec.push_back(3);

	zassert_equal(static_cast<int32_t>(vec.size()), 4);
	zassert_equal(vec[0], 0);
	zassert_equal(vec[1], 1);
	zassert_equal(vec[2], 2);
	zassert_equal(vec[3], 3);
}

ZTEST(std_vector, test_fill_one_vector)
{
	// This test allocates 8kB of memory in total in a single std::vector
	constexpr int num_elements = 2 * 1024;
	std::vector<int32_t> vec;

	for (int i = 0; i < num_elements; ++i)
		vec.push_back(i);

	zassert_equal(static_cast<int>(vec.size()), num_elements);
	for (int i = 0; i < num_elements; ++i) {
		zassert_equal(vec[i], i);
	}
}

ZTEST(std_vector, test_fill_multiple_vectors)
{
	// This test allocates a large block of memory split in 8 std::vectors.
	// Since Helipilot has less available RAM, it will allocate 8KB RAM
	// (8*1KB), while other targets will allocate 16KB (8*2kB).
#ifdef BASEBOARD_HELIPILOT
	constexpr int num_elements = 1024;
#else
	constexpr int num_elements = 2 * 1024;
#endif

	std::array<std::vector<int32_t>, 8> vecs;

	for (int i = 0; i < num_elements; ++i)
		for (auto &vec : vecs)
			vec.push_back(i);

	for (auto &vec : vecs) {
		zassert_equal(static_cast<int>(vec.size()), num_elements);
		for (int i = 0; i < num_elements; ++i) {
			zassert_equal(vec[i], i);
		}
	}
}

ZTEST(std_vector, test_create_and_destroy_two_vectors)
{
	// This allocates 8kB of memory twice.
	// The first vector is declared in a local scope and the memory is
	// free'd at the end of the block.
	constexpr int num_elements = 2 * 1024;
	{
		std::vector<int32_t> vec;
		for (int i = 0; i < num_elements; ++i)
			vec.push_back(i);

		zassert_equal(static_cast<int>(vec.size()), num_elements);
		for (int i = 0; i < num_elements; ++i) {
			zassert_equal(vec[i], i);
		}
	}

	std::vector<int32_t> vec;
	for (int i = 0; i < num_elements; ++i)
		vec.push_back(i);

	zassert_equal(static_cast<int>(vec.size()), num_elements);
	for (int i = 0; i < num_elements; ++i) {
		zassert_equal(vec[i], i);
	}
}
