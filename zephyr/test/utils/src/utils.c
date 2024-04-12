/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "shared_mem.h"
#include "system.h"
#include "timer.h"
#include "util.h"
#include "watchdog.h"

#ifndef USE_BUILTIN_STDLIB
#include <malloc.h>
#endif
#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

ZTEST_SUITE(utils, NULL, NULL, NULL, NULL, NULL);

ZTEST(utils, test_uint64divmod_0)
{
	uint64_t n = 8567106442584750ULL;
	int d = 54870071;
	int r = uint64divmod(&n, d);

	zassert_true(r == 5991285 && n == 156134415ULL);
}

ZTEST(utils, test_uint64divmod_1)
{
	uint64_t n = 8567106442584750ULL;
	int d = 2;
	int r = uint64divmod(&n, d);

	zassert_true(r == 0 && n == 4283553221292375ULL);
}

ZTEST(utils, test_uint64divmod_2)
{
	uint64_t n = 8567106442584750ULL;
	int d = 0;
	int r = uint64divmod(&n, d);

	zassert_true(r == 0 && n == 0ULL);
}

ZTEST(utils, test_get_next_bit)
{
	uint32_t mask = 0x10001010;

	zassert_true(get_next_bit(&mask) == 28);
	zassert_true(mask == 0x1010);
	zassert_true(get_next_bit(&mask) == 12);
	zassert_true(mask == 0x10);
	zassert_true(get_next_bit(&mask) == 4);
	zassert_true(mask == 0x0);
}

ZTEST(utils, test_shared_mem)
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

	zassert_equal(shared_mem_acquire(sz, &mem1), EC_SUCCESS);
#ifdef USE_BUILTIN_STDLIB
	zassert_equal(shared_mem_acquire(sz, &mem2), EC_ERROR_BUSY);
#endif

	for (i = 0; i < 256; ++i) {
		memset(mem1, i, sz);
		for (int j = 0; j < sz; j++) {
			zassert_equal(
				mem1[j], (char)i,
				"i: %d, mem1[j]: %d, i: 0x%08x, mem1[j]: 0x%08x",
				i, mem1[j], i, mem1[j]);
		}
		if ((i & 0xf) == 0) {
			ccprintf("Yielding after %d iterations\n", i + 1);
			k_msleep(1); /* Yield to other tasks */
		}
	}

	shared_mem_release(mem1);
}

ZTEST(utils, test_shared_mem_release_null)
{
	/* This should be a no-op. We can't do much to test it directly. */
	shared_mem_release(NULL);
}

#ifndef CONFIG_BOARD_NATIVE_SIM
ZTEST(utils, test_scratchpad)
{
	uint32_t scratchpad_value = 0;

	system_set_scratchpad(0xfeed);
	zassert_equal(system_get_scratchpad(&scratchpad_value), EC_SUCCESS);
	zassert_equal(scratchpad_value, 0xfeed);
}
#endif

ZTEST(utils, test_cond_t)
{
	cond_t c;

	/* one-shot? */
	cond_init_false(&c);
	cond_set_true(&c);
	zassert_true(cond_went_true(&c));
	zassert_false(cond_went_true(&c));
	zassert_false(cond_went_true(&c));
	cond_set_false(&c);
	zassert_true(cond_went_false(&c));
	zassert_false(cond_went_false(&c));
	zassert_false(cond_went_false(&c));

	/* one-shot when initially true? */
	cond_init_true(&c);
	cond_set_false(&c);
	zassert_true(cond_went_false(&c));
	zassert_false(cond_went_false(&c));
	zassert_false(cond_went_false(&c));
	cond_set_true(&c);
	zassert_true(cond_went_true(&c));
	zassert_false(cond_went_true(&c));
	zassert_false(cond_went_true(&c));

	/* still one-shot even if set multiple times? */
	cond_init_false(&c);
	cond_set_true(&c);
	cond_set_true(&c);
	cond_set_true(&c);
	cond_set_true(&c);
	cond_set_true(&c);
	cond_set_true(&c);
	zassert_true(cond_went_true(&c));
	zassert_false(cond_went_true(&c));
	zassert_false(cond_went_true(&c));
	cond_set_true(&c);
	cond_set_false(&c);
	cond_set_false(&c);
	cond_set_false(&c);
	cond_set_false(&c);
	cond_set_false(&c);
	zassert_true(cond_went_false(&c));
	zassert_false(cond_went_false(&c));
	zassert_false(cond_went_false(&c));

	/* only the detected transition direction resets it */
	cond_set_true(&c);
	zassert_false(cond_went_false(&c));
	zassert_true(cond_went_true(&c));
	zassert_false(cond_went_false(&c));
	zassert_false(cond_went_true(&c));
	cond_set_false(&c);
	zassert_false(cond_went_true(&c));
	zassert_false(cond_went_true(&c));
	zassert_true(cond_went_false(&c));
	zassert_false(cond_went_false(&c));
	zassert_false(cond_went_false(&c));
	zassert_false(cond_went_true(&c));
	zassert_false(cond_went_true(&c));

	/* multiple transitions between checks should notice both edges */
	cond_set_true(&c);
	cond_set_false(&c);
	cond_set_true(&c);
	cond_set_false(&c);
	cond_set_true(&c);
	cond_set_false(&c);
	zassert_true(cond_went_true(&c));
	zassert_false(cond_went_true(&c));
	zassert_false(cond_went_true(&c));
	zassert_false(cond_went_true(&c));
	zassert_true(cond_went_false(&c));
	zassert_false(cond_went_false(&c));
	zassert_false(cond_went_false(&c));
	zassert_false(cond_went_false(&c));
	zassert_false(cond_went_true(&c));
	zassert_false(cond_went_true(&c));
	zassert_false(cond_went_false(&c));

	/* Still has last value? */
	cond_set_true(&c);
	cond_set_false(&c);
	cond_set_true(&c);
	cond_set_false(&c);
	zassert_true(cond_is_false(&c));
	cond_set_false(&c);
	cond_set_true(&c);
	cond_set_false(&c);
	cond_set_true(&c);
	zassert_true(cond_is_true(&c));
}

ZTEST(utils, test_mula32)
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
	zassert_true(r == 0x9df59b9fb0ab9d96L);
	zassert_true(r2 == 0x9df59b9fb0beabd6L);
}

ZTEST(utils, test_bytes_are_trivial)
{
	static const uint8_t all0x00[] = { 0x00, 0x00, 0x00 };
	static const uint8_t all0xff[] = { 0xff, 0xff, 0xff, 0xff };
	static const uint8_t nontrivial1[] = { 0x00, 0x01, 0x02 };
	static const uint8_t nontrivial2[] = { 0xdd, 0xee, 0xff };
	static const uint8_t nontrivial3[] = { 0x00, 0x00, 0x00, 0xff };
	static const uint8_t nontrivial4[] = { 0xff, 0x00, 0x00, 0x00 };

	zassert_true(bytes_are_trivial(all0x00, sizeof(all0x00)));
	zassert_true(bytes_are_trivial(all0xff, sizeof(all0xff)));
	zassert_false(bytes_are_trivial(nontrivial1, sizeof(nontrivial1)));
	zassert_false(bytes_are_trivial(nontrivial2, sizeof(nontrivial2)));
	zassert_false(bytes_are_trivial(nontrivial3, sizeof(nontrivial3)));
	zassert_false(bytes_are_trivial(nontrivial4, sizeof(nontrivial4)));
}

ZTEST(utils, test_is_aligned)
{
	zassert_false(is_aligned(2, 0));
	zassert_true(is_aligned(2, 1));
	zassert_true(is_aligned(2, 2));
	zassert_false(is_aligned(2, 3));
	zassert_false(is_aligned(2, 4));

	zassert_false(is_aligned(3, 0));
	zassert_true(is_aligned(3, 1));
	zassert_false(is_aligned(3, 2));
	zassert_false(is_aligned(3, 3));
	zassert_false(is_aligned(3, 4));
}

ZTEST(utils, test_safe_memcmp)
{
	const char str1[] = "abc";
	const char str2[] = "def";
	const char str3[] = "abc";

	/* Verify that the compiler hasn't optimized str1 and str3 to point
	 * to the same underlying memory.
	 */
	zassert_not_equal(str1, str3);

	zassert_equal(safe_memcmp(NULL, NULL, 0), 0);
	zassert_equal(safe_memcmp(str1, str2, sizeof(str1)), 1);
	zassert_equal(safe_memcmp(str1, str3, sizeof(str1)), 0);
}

ZTEST(utils, test_alignment_log2)
{
	zassert_equal(alignment_log2(1), 0);
	zassert_equal(alignment_log2(2), 1);
	zassert_equal(alignment_log2(5), 0);
	zassert_equal(alignment_log2(0x10070000), 16);
	zassert_equal(alignment_log2(0x80000000), 31);
}

ZTEST(utils, test_binary_first_base3_from_bits)
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

	zassert_equal(binary_first_base3_from_bits(n0, ARRAY_SIZE(n0)), 0);
	zassert_equal(binary_first_base3_from_bits(n7, ARRAY_SIZE(n7)), 7);
	zassert_equal(binary_first_base3_from_bits(n8, ARRAY_SIZE(n8)), 8);
	zassert_equal(binary_first_base3_from_bits(n9, ARRAY_SIZE(n9)), 9);
	zassert_equal(binary_first_base3_from_bits(n10, ARRAY_SIZE(n10)), 10);
	zassert_equal(binary_first_base3_from_bits(n11, ARRAY_SIZE(n11)), 11);
	zassert_equal(binary_first_base3_from_bits(n18, ARRAY_SIZE(n18)), 18);
	zassert_equal(binary_first_base3_from_bits(n26, ARRAY_SIZE(n26)), 26);
	zassert_equal(binary_first_base3_from_bits(n38, ARRAY_SIZE(n38)), 38);
}

ZTEST(utils, test_parse_bool)
{
	int bool_val;
	int rv;

	/* False cases. */

	bool_val = 1;
	rv = parse_bool("off", &bool_val);
	zassert_equal(rv, 1);
	zassert_equal(bool_val, 0);

	bool_val = 1;
	rv = parse_bool("dis", &bool_val);
	zassert_equal(rv, 1);
	zassert_equal(bool_val, 0);

	bool_val = 1;
	rv = parse_bool("f", &bool_val);
	zassert_equal(rv, 1);
	zassert_equal(bool_val, 0);

	bool_val = 1;
	rv = parse_bool("n", &bool_val);
	zassert_equal(rv, 1);
	zassert_equal(bool_val, 0);

	/* True cases. */

	bool_val = 0;
	rv = parse_bool("on", &bool_val);
	zassert_equal(rv, 1);
	zassert_equal(bool_val, 1);

	bool_val = 0;
	rv = parse_bool("ena", &bool_val);
	zassert_equal(rv, 1);
	zassert_equal(bool_val, 1);

	bool_val = 0;
	rv = parse_bool("t", &bool_val);
	zassert_equal(rv, 1);
	zassert_equal(bool_val, 1);

	bool_val = 0;
	rv = parse_bool("y", &bool_val);
	zassert_equal(rv, 1);
	zassert_equal(bool_val, 1);

	/* Error case. */
	bool_val = -1;
	rv = parse_bool("a", &bool_val);
	zassert_equal(rv, 0);
	zassert_equal(bool_val, -1);
}
