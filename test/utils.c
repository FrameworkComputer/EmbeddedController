/* Copyright 2013 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Test common utilities.
 */

#include "common.h"
#include "console.h"
#include "shared_mem.h"
#include "system.h"
#include "test_util.h"
#include "timer.h"
#include "util.h"
#include "watchdog.h"

#ifndef USE_BUILTIN_STDLIB
#include <malloc.h>
#endif

static int test_uint64divmod_0(void)
{
	uint64_t n = 8567106442584750ULL;
	int d = 54870071;
	int r = uint64divmod(&n, d);

	TEST_ASSERT(r == 5991285 && n == 156134415ULL);
	return EC_SUCCESS;
}

static int test_uint64divmod_1(void)
{
	uint64_t n = 8567106442584750ULL;
	int d = 2;
	int r = uint64divmod(&n, d);

	TEST_ASSERT(r == 0 && n == 4283553221292375ULL);
	return EC_SUCCESS;
}

static int test_uint64divmod_2(void)
{
	uint64_t n = 8567106442584750ULL;
	int d = 0;
	int r = uint64divmod(&n, d);

	TEST_ASSERT(r == 0 && n == 0ULL);
	return EC_SUCCESS;
}

static int test_get_next_bit(void)
{
	uint32_t mask = 0x10001010;

	TEST_ASSERT(get_next_bit(&mask) == 28);
	TEST_ASSERT(mask == 0x1010);
	TEST_ASSERT(get_next_bit(&mask) == 12);
	TEST_ASSERT(mask == 0x10);
	TEST_ASSERT(get_next_bit(&mask) == 4);
	TEST_ASSERT(mask == 0x0);

	return EC_SUCCESS;
}

static int test_shared_mem(void)
{
	int i;
	int sz = shared_mem_size();
	char *mem1;
#ifdef USE_BUILTIN_STDLIB
	char *mem2;
#endif

#ifndef USE_BUILTIN_STDLIB
	/* Trim to make sure that other tests haven't fragmented the heap. */
	malloc_trim(0);

	/*
	 * When using malloc() we can't allocate the full shared_mem_size() due
	 * to the overhead of malloc's memory tracking. See test/malloc.c.
	 */
	sz *= 0.8;
#endif

	TEST_EQ(shared_mem_acquire(sz, &mem1), EC_SUCCESS, "%d");
#ifdef USE_BUILTIN_STDLIB
	TEST_EQ(shared_mem_acquire(sz, &mem2), EC_ERROR_BUSY, "%d");
#endif

	for (i = 0; i < 256; ++i) {
		memset(mem1, i, sz);
		TEST_ASSERT_MEMSET(mem1, (char)i, sz);
		if ((i & 0xf) == 0)
			crec_msleep(20); /* Yield to other tasks */
	}

	shared_mem_release(mem1);

	return EC_SUCCESS;
}

test_static int test_shared_mem_release_null(void)
{
	/* This should be a no-op. We can't do much to test it directly. */
	shared_mem_release(NULL);

	return EC_SUCCESS;
}

static int test_scratchpad(void)
{
	uint32_t scratchpad_value;

	system_set_scratchpad(0xfeed);
	TEST_EQ(system_get_scratchpad(&scratchpad_value), EC_SUCCESS, "%d");
	TEST_EQ(scratchpad_value, 0xfeed, "%d");

	return EC_SUCCESS;
}

static int test_cond_t(void)
{
	cond_t c;

	/* one-shot? */
	cond_init_false(&c);
	cond_set_true(&c);
	TEST_ASSERT(cond_went_true(&c));
	TEST_ASSERT(!cond_went_true(&c));
	TEST_ASSERT(!cond_went_true(&c));
	cond_set_false(&c);
	TEST_ASSERT(cond_went_false(&c));
	TEST_ASSERT(!cond_went_false(&c));
	TEST_ASSERT(!cond_went_false(&c));

	/* one-shot when initially true? */
	cond_init_true(&c);
	cond_set_false(&c);
	TEST_ASSERT(cond_went_false(&c));
	TEST_ASSERT(!cond_went_false(&c));
	TEST_ASSERT(!cond_went_false(&c));
	cond_set_true(&c);
	TEST_ASSERT(cond_went_true(&c));
	TEST_ASSERT(!cond_went_true(&c));
	TEST_ASSERT(!cond_went_true(&c));

	/* still one-shot even if set multiple times? */
	cond_init_false(&c);
	cond_set_true(&c);
	cond_set_true(&c);
	cond_set_true(&c);
	cond_set_true(&c);
	cond_set_true(&c);
	cond_set_true(&c);
	TEST_ASSERT(cond_went_true(&c));
	TEST_ASSERT(!cond_went_true(&c));
	TEST_ASSERT(!cond_went_true(&c));
	cond_set_true(&c);
	cond_set_false(&c);
	cond_set_false(&c);
	cond_set_false(&c);
	cond_set_false(&c);
	cond_set_false(&c);
	TEST_ASSERT(cond_went_false(&c));
	TEST_ASSERT(!cond_went_false(&c));
	TEST_ASSERT(!cond_went_false(&c));

	/* only the detected transition direction resets it */
	cond_set_true(&c);
	TEST_ASSERT(!cond_went_false(&c));
	TEST_ASSERT(cond_went_true(&c));
	TEST_ASSERT(!cond_went_false(&c));
	TEST_ASSERT(!cond_went_true(&c));
	cond_set_false(&c);
	TEST_ASSERT(!cond_went_true(&c));
	TEST_ASSERT(!cond_went_true(&c));
	TEST_ASSERT(cond_went_false(&c));
	TEST_ASSERT(!cond_went_false(&c));
	TEST_ASSERT(!cond_went_false(&c));
	TEST_ASSERT(!cond_went_true(&c));
	TEST_ASSERT(!cond_went_true(&c));

	/* multiple transitions between checks should notice both edges */
	cond_set_true(&c);
	cond_set_false(&c);
	cond_set_true(&c);
	cond_set_false(&c);
	cond_set_true(&c);
	cond_set_false(&c);
	TEST_ASSERT(cond_went_true(&c));
	TEST_ASSERT(!cond_went_true(&c));
	TEST_ASSERT(!cond_went_true(&c));
	TEST_ASSERT(!cond_went_true(&c));
	TEST_ASSERT(cond_went_false(&c));
	TEST_ASSERT(!cond_went_false(&c));
	TEST_ASSERT(!cond_went_false(&c));
	TEST_ASSERT(!cond_went_false(&c));
	TEST_ASSERT(!cond_went_true(&c));
	TEST_ASSERT(!cond_went_true(&c));
	TEST_ASSERT(!cond_went_false(&c));

	/* Still has last value? */
	cond_set_true(&c);
	cond_set_false(&c);
	cond_set_true(&c);
	cond_set_false(&c);
	TEST_ASSERT(cond_is_false(&c));
	cond_set_false(&c);
	cond_set_true(&c);
	cond_set_false(&c);
	cond_set_true(&c);
	TEST_ASSERT(cond_is_true(&c));

	/* well okay then */
	return EC_SUCCESS;
}

static int test_mula32(void)
{
	uint64_t r = 0x0;
	uint64_t r2 = 0x0;
	uint32_t b = 0x1;
	uint32_t c = 0x1;
	uint32_t i;
	timestamp_t t0, t1;

	t0 = get_time();
	for (i = 0; i < 5000000; i++) {
		r = mula32(b, c, r + (r >> 32));
		r2 = mulaa32(b, c, r2 >> 32, r2);
		b = (b << 13) ^ (b >> 2) ^ i;
		c = (c << 16) ^ (c >> 7) ^ i;

		if (i % 100000 == 0)
			watchdog_reload();
	}
	t1 = get_time();

	ccprintf("After %d iterations, r=%08x%08x, r2=%08x%08x (time: %d)\n", i,
		 (uint32_t)(r >> 32), (uint32_t)r, (uint32_t)(r2 >> 32),
		 (uint32_t)r2, t1.le.lo - t0.le.lo);
	TEST_ASSERT(r == 0x9df59b9fb0ab9d96L);
	TEST_ASSERT(r2 == 0x9df59b9fb0beabd6L);

	/* well okay then */
	return EC_SUCCESS;
}

static int test_bytes_are_trivial(void)
{
	static const uint8_t all0x00[] = { 0x00, 0x00, 0x00 };
	static const uint8_t all0xff[] = { 0xff, 0xff, 0xff, 0xff };
	static const uint8_t nontrivial1[] = { 0x00, 0x01, 0x02 };
	static const uint8_t nontrivial2[] = { 0xdd, 0xee, 0xff };
	static const uint8_t nontrivial3[] = { 0x00, 0x00, 0x00, 0xff };
	static const uint8_t nontrivial4[] = { 0xff, 0x00, 0x00, 0x00 };

	TEST_ASSERT(bytes_are_trivial(all0x00, sizeof(all0x00)));
	TEST_ASSERT(bytes_are_trivial(all0xff, sizeof(all0xff)));
	TEST_ASSERT(!bytes_are_trivial(nontrivial1, sizeof(nontrivial1)));
	TEST_ASSERT(!bytes_are_trivial(nontrivial2, sizeof(nontrivial2)));
	TEST_ASSERT(!bytes_are_trivial(nontrivial3, sizeof(nontrivial3)));
	TEST_ASSERT(!bytes_are_trivial(nontrivial4, sizeof(nontrivial4)));

	return EC_SUCCESS;
}

test_static int test_is_aligned(void)
{
	TEST_EQ(is_aligned(2, 0), false, "%d");
	TEST_EQ(is_aligned(2, 1), true, "%d");
	TEST_EQ(is_aligned(2, 2), true, "%d");
	TEST_EQ(is_aligned(2, 3), false, "%d");
	TEST_EQ(is_aligned(2, 4), false, "%d");

	TEST_EQ(is_aligned(3, 0), false, "%d");
	TEST_EQ(is_aligned(3, 1), true, "%d");
	TEST_EQ(is_aligned(3, 2), false, "%d");
	TEST_EQ(is_aligned(3, 3), false, "%d");
	TEST_EQ(is_aligned(3, 4), false, "%d");

	return EC_SUCCESS;
}

test_static int test_safe_memcmp(void)
{
	const char str1[] = "abc";
	const char str2[] = "def";
	const char str3[] = "abc";

	/* Verify that the compiler hasn't optimized str1 and str3 to point
	 * to the same underlying memory.
	 */
	TEST_NE(str1, str3, "%p");

	TEST_EQ(safe_memcmp(NULL, NULL, 0), 0, "%d");
	TEST_EQ(safe_memcmp(str1, str2, sizeof(str1)), 1, "%d");
	TEST_EQ(safe_memcmp(str1, str3, sizeof(str1)), 0, "%d");
	return EC_SUCCESS;
}

test_static int test_alignment_log2(void)
{
	TEST_EQ(alignment_log2(1), 0, "%d");
	TEST_EQ(alignment_log2(2), 1, "%d");
	TEST_EQ(alignment_log2(5), 0, "%d");
	TEST_EQ(alignment_log2(0x10070000), 16, "%d");
	TEST_EQ(alignment_log2(0x80000000), 31, "%d");
	return EC_SUCCESS;
}

test_static int test_binary_first_base3_from_bits(void)
{
	int n0[] = { 0, 0, 0 }; /* LSB first */
	int n7[] = { 1, 1, 1 };
	int n8[] = { 2, 0, 0 };
	int n9[] = { 2, 1, 0 };
	int n10[] = { 0, 2, 0 };
	int n11[] = { 1, 2, 0 };
	int n18[] = { 0, 0, 2 };
	int n26[] = { 2, 2, 2 };
	int n38[] = { 1, 2, 0, 1 };

	TEST_EQ(binary_first_base3_from_bits(n0, ARRAY_SIZE(n0)), 0, "%d");
	TEST_EQ(binary_first_base3_from_bits(n7, ARRAY_SIZE(n7)), 7, "%d");
	TEST_EQ(binary_first_base3_from_bits(n8, ARRAY_SIZE(n8)), 8, "%d");
	TEST_EQ(binary_first_base3_from_bits(n9, ARRAY_SIZE(n9)), 9, "%d");
	TEST_EQ(binary_first_base3_from_bits(n10, ARRAY_SIZE(n10)), 10, "%d");
	TEST_EQ(binary_first_base3_from_bits(n11, ARRAY_SIZE(n11)), 11, "%d");
	TEST_EQ(binary_first_base3_from_bits(n18, ARRAY_SIZE(n18)), 18, "%d");
	TEST_EQ(binary_first_base3_from_bits(n26, ARRAY_SIZE(n26)), 26, "%d");
	TEST_EQ(binary_first_base3_from_bits(n38, ARRAY_SIZE(n38)), 38, "%d");
	return EC_SUCCESS;
}

test_static int test_parse_bool(void)
{
	int bool_val;
	int rv;

	/* False cases. */

	bool_val = 1;
	rv = parse_bool("off", &bool_val);
	TEST_EQ(rv, 1, "%d");
	TEST_EQ(bool_val, 0, "%d");

	bool_val = 1;
	rv = parse_bool("dis", &bool_val);
	TEST_EQ(rv, 1, "%d");
	TEST_EQ(bool_val, 0, "%d");

	bool_val = 1;
	rv = parse_bool("f", &bool_val);
	TEST_EQ(rv, 1, "%d");
	TEST_EQ(bool_val, 0, "%d");

	bool_val = 1;
	rv = parse_bool("n", &bool_val);
	TEST_EQ(rv, 1, "%d");
	TEST_EQ(bool_val, 0, "%d");

	/* True cases. */

	bool_val = 0;
	rv = parse_bool("on", &bool_val);
	TEST_EQ(rv, 1, "%d");
	TEST_EQ(bool_val, 1, "%d");

	bool_val = 0;
	rv = parse_bool("ena", &bool_val);
	TEST_EQ(rv, 1, "%d");
	TEST_EQ(bool_val, 1, "%d");

	bool_val = 0;
	rv = parse_bool("t", &bool_val);
	TEST_EQ(rv, 1, "%d");
	TEST_EQ(bool_val, 1, "%d");

	bool_val = 0;
	rv = parse_bool("y", &bool_val);
	TEST_EQ(rv, 1, "%d");
	TEST_EQ(bool_val, 1, "%d");

	/* Error case. */
	bool_val = -1;
	rv = parse_bool("a", &bool_val);
	TEST_EQ(rv, 0, "%d");
	TEST_EQ(bool_val, -1, "%d");

	return EC_SUCCESS;
}

void run_test(int argc, const char **argv)
{
	test_reset();

	RUN_TEST(test_uint64divmod_0);
	RUN_TEST(test_uint64divmod_1);
	RUN_TEST(test_uint64divmod_2);
	RUN_TEST(test_get_next_bit);
	RUN_TEST(test_shared_mem_release_null);
	RUN_TEST(test_shared_mem);
	RUN_TEST(test_scratchpad);
	RUN_TEST(test_cond_t);
	RUN_TEST(test_mula32);
	RUN_TEST(test_bytes_are_trivial);
	RUN_TEST(test_is_aligned);
	RUN_TEST(test_safe_memcmp);
	RUN_TEST(test_alignment_log2);
	RUN_TEST(test_binary_first_base3_from_bits);
	RUN_TEST(test_parse_bool);

	test_print_result();
}
