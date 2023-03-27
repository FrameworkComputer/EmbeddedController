/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "shared_mem.h"
#include "stdlib.h"
#include "test_util.h"

#include <malloc.h>

struct malloc_data {
	uint32_t size;
	uint8_t val;
	uint8_t *data;
};

static struct malloc_data test_data[] = {
	{ .size = 15, .val = 1, .data = NULL },
	{ .size = 1024, .val = 2, .data = NULL },
	{ .size = 86096, .val = 3, .data = NULL }
};

test_static int test_malloc_different_sizes(void)
{
	/* Trim to make sure that previous tests haven't fragmented the heap. */
	malloc_trim(0);

	for (int i = 0; i < ARRAY_SIZE(test_data); i++) {
		uint8_t *volatile ptr = malloc(test_data[i].size);
		TEST_NE(ptr, NULL, "%p");
		test_data[i].data = ptr;
		for (int j = 0; j < test_data[i].size; j++) {
			ptr[j] = test_data[i].val;
		}
	}

	for (int i = 0; i < ARRAY_SIZE(test_data); i++) {
		uint8_t *ptr = test_data[i].data;
		/* Using TEST_EQ results in too much logging. */
		for (int j = 0; j < test_data[i].size; j++) {
			if (ptr[j] != test_data[i].val) {
				TEST_ASSERT(false);
			}
		}
	}

	for (int i = 0; i < ARRAY_SIZE(test_data); i++) {
		free(test_data[i].data);
	}

	return EC_SUCCESS;
}

test_static int test_free_null(void)
{
	free(NULL);
	return EC_SUCCESS;
}

test_static int test_malloc_large(void)
{
	/* Trim to make sure that previous tests haven't fragmented the heap. */
	malloc_trim(0);
	uint8_t *volatile ptr = malloc(shared_mem_size() * 0.8);
	TEST_NE(ptr, NULL, "%p");
	free(ptr);
	return EC_SUCCESS;
}

test_static int test_malloc_too_large(void)
{
	/* Trim to make sure that previous tests haven't fragmented the heap. */
	malloc_trim(0);
	uint8_t *volatile ptr = malloc(shared_mem_size() + 1);
	TEST_EQ(ptr, NULL, "%p");
	free(ptr);
	return EC_SUCCESS;
}

void run_test(int argc, const char **argv)
{
	test_reset();

	RUN_TEST(test_free_null);
	RUN_TEST(test_malloc_different_sizes);
	RUN_TEST(test_malloc_large);

	if (!IS_ENABLED(BOARD_HOST))
		RUN_TEST(test_malloc_too_large);

	test_print_result();
}
