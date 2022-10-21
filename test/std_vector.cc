/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Basic test of std::vector and dynamic memory allocation.
 */

#include <array>
#include <vector>

extern "C" {
#include "common.h"
#include "console.h"
#include "test_util.h"
}

test_static int push_back_elements()
{
	std::vector<int32_t> vec;

	vec.push_back(0);
	vec.push_back(1);
	vec.push_back(2);
	vec.push_back(3);

	TEST_EQ(static_cast<int32_t>(vec.size()), 4, "%d");
	TEST_EQ(vec[0], 0, "%d");
	TEST_EQ(vec[1], 1, "%d");
	TEST_EQ(vec[2], 2, "%d");
	TEST_EQ(vec[3], 3, "%d");

	return EC_SUCCESS;
}

test_static int fill_one_vector()
{
	// This test allocates 64kB of memory in total in a single std::vector
	constexpr int num_elements = 16 * 1024;
	std::vector<int32_t> vec;

	for (int i = 0; i < num_elements; ++i)
		vec.push_back(i);

	TEST_EQ(static_cast<int>(vec.size()), num_elements, "%d");
	for (int i = 0; i < num_elements; ++i) {
		TEST_ASSERT(vec[i] == i);
		// Using TEST_EQ floods the console and trigger the watchdog
		// TEST_EQ(vec[i], i, "%d");
		// cflush();
	}

	return EC_SUCCESS;
}

test_static int fill_multiple_vectors()
{
	// This test allocates 64kB of memory in total split in 8 std::vectors
	constexpr int num_elements = 2 * 1024;
	std::array<std::vector<int32_t>, 8> vecs;

	for (int i = 0; i < num_elements; ++i)
		for (auto &vec : vecs)
			vec.push_back(i);

	for (auto &vec : vecs) {
		TEST_EQ(static_cast<int>(vec.size()), num_elements, "%d");
		for (int i = 0; i < num_elements; ++i) {
			TEST_ASSERT(vec[i] == i);
		}
	}

	return EC_SUCCESS;
}

extern "C" void run_test(int argc, const char **argv)
{
	test_reset();

	RUN_TEST(push_back_elements);
	RUN_TEST(fill_one_vector);
	RUN_TEST(fill_multiple_vectors);

	test_print_result();
}
