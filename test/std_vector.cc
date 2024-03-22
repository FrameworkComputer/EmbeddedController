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

test_static int stack_init_elements()
{
	std::vector<int32_t> vec{ 10, 11, 12, 13, 14 };

	TEST_EQ(static_cast<int32_t>(vec.size()), 5, "%d");
	TEST_EQ(vec[0], 10, "%d");
	TEST_EQ(vec[1], 11, "%d");
	TEST_EQ(vec[2], 12, "%d");
	TEST_EQ(vec[3], 13, "%d");
	TEST_EQ(vec[4], 14, "%d");

	return EC_SUCCESS;
}

test_static int static_init_elements()
{
	static std::vector<int32_t> vec{ 20, 21, 22, 23, 24 };

	TEST_EQ(static_cast<int32_t>(vec.size()), 5, "%d");
	TEST_EQ(vec[0], 20, "%d");
	TEST_EQ(vec[1], 21, "%d");
	TEST_EQ(vec[2], 22, "%d");
	TEST_EQ(vec[3], 23, "%d");
	TEST_EQ(vec[4], 24, "%d");

	return EC_SUCCESS;
}

std::vector<int32_t> global_vec{ 30, 31, 32, 33, 34 };
test_static int global_init_elements()
{
	TEST_EQ(static_cast<int32_t>(global_vec.size()), 5, "%d");
	TEST_EQ(global_vec[0], 30, "%d");
	TEST_EQ(global_vec[1], 31, "%d");
	TEST_EQ(global_vec[2], 32, "%d");
	TEST_EQ(global_vec[3], 33, "%d");
	TEST_EQ(global_vec[4], 34, "%d");

	return EC_SUCCESS;
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
	// This test allocates 8kB of memory in total in a single std::vector
	constexpr int num_elements = 2 * 1024;
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
		TEST_EQ(static_cast<int>(vec.size()), num_elements, "%d");
		for (int i = 0; i < num_elements; ++i) {
			TEST_ASSERT(vec[i] == i);
		}
	}

	return EC_SUCCESS;
}

test_static int create_and_destroy_two_vectors()
{
	// This allocates 8kB of memory twice.
	// The first vector is declared in a local scope and the memory is
	// free'd at the end of the block.
	constexpr int num_elements = 2 * 1024;
	{
		std::vector<int32_t> vec;
		for (int i = 0; i < num_elements; ++i)
			vec.push_back(i);

		TEST_EQ(static_cast<int>(vec.size()), num_elements, "%d");
		for (int i = 0; i < num_elements; ++i) {
			TEST_ASSERT(vec[i] == i);
		}
	}

	std::vector<int32_t> vec;
	for (int i = 0; i < num_elements; ++i)
		vec.push_back(i);

	TEST_EQ(static_cast<int>(vec.size()), num_elements, "%d");
	for (int i = 0; i < num_elements; ++i) {
		TEST_ASSERT(vec[i] == i);
	}

	return EC_SUCCESS;
}

extern "C" void run_test(int argc, const char **argv)
{
	test_reset();

	RUN_TEST(stack_init_elements);
	RUN_TEST(static_init_elements);
	RUN_TEST(global_init_elements);
	RUN_TEST(push_back_elements);
	RUN_TEST(fill_one_vector);
	RUN_TEST(fill_multiple_vectors);
	RUN_TEST(create_and_destroy_two_vectors);

	test_print_result();
}
