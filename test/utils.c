/* Copyright 2013 The Chromium OS Authors. All rights reserved.
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

static int test_memmove(void)
{
	int i;
	timestamp_t t0, t1, t2, t3;
	char *buf;
	const int buf_size = 1000;
	const int len = 400;
	const int iteration = 1000;

	TEST_ASSERT(shared_mem_acquire(buf_size, &buf) == EC_SUCCESS);

	for (i = 0; i < len; ++i)
		buf[i] = i & 0x7f;
	for (i = len; i < buf_size; ++i)
		buf[i] = 0;

	t0 = get_time();
	for (i = 0; i < iteration; ++i)
		memmove(buf + 101, buf, len);  /* unaligned */
	t1 = get_time();
	TEST_ASSERT_ARRAY_EQ(buf + 101, buf, len);
	ccprintf(" (speed gain: %" PRId64 " ->", t1.val-t0.val);

	t2 = get_time();
	for (i = 0; i < iteration; ++i)
		memmove(buf + 100, buf, len);	  /* aligned */
	t3 = get_time();
	ccprintf(" %" PRId64 " us) ", t3.val-t2.val);
	TEST_ASSERT_ARRAY_EQ(buf + 100, buf, len);

	/* Expected about 4x speed gain. Use 3x because it fluctuates */
#ifndef EMU_BUILD
	/*
	 * The speed gain is too unpredictable on host, especially on
	 * buildbots. Skip it if we are running in the emulator.
	 */
	TEST_ASSERT((t1.val-t0.val) > (unsigned)(t3.val-t2.val) * 3);
#endif

	/* Test small moves */
	memmove(buf + 1, buf, 1);
	TEST_ASSERT_ARRAY_EQ(buf + 1, buf, 1);
	memmove(buf + 5, buf, 4);
	memmove(buf + 1, buf, 4);
	TEST_ASSERT_ARRAY_EQ(buf + 1, buf + 5, 4);

	shared_mem_release(buf);
	return EC_SUCCESS;
}

static int test_memcpy(void)
{
	int i;
	timestamp_t t0, t1, t2, t3;
	char *buf;
	const int buf_size = 1000;
	const int len = 400;
	const int dest_offset = 500;
	const int iteration = 1000;

	TEST_ASSERT(shared_mem_acquire(buf_size, &buf) == EC_SUCCESS);

	for (i = 0; i < len; ++i)
		buf[i] = i & 0x7f;
	for (i = len; i < buf_size; ++i)
		buf[i] = 0;

	t0 = get_time();
	for (i = 0; i < iteration; ++i)
		memcpy(buf + dest_offset + 1, buf, len);  /* unaligned */
	t1 = get_time();
	TEST_ASSERT_ARRAY_EQ(buf + dest_offset + 1, buf, len);
	ccprintf(" (speed gain: %" PRId64 " ->", t1.val-t0.val);

	t2 = get_time();
	for (i = 0; i < iteration; ++i)
		memcpy(buf + dest_offset, buf, len);	  /* aligned */
	t3 = get_time();
	ccprintf(" %" PRId64 " us) ", t3.val-t2.val);
	TEST_ASSERT_ARRAY_EQ(buf + dest_offset, buf, len);

	/* Expected about 4x speed gain. Use 3x because it fluctuates */
#ifndef EMU_BUILD
	/*
	 * The speed gain is too unpredictable on host, especially on
	 * buildbots. Skip it if we are running in the emulator.
	 */
	TEST_ASSERT((t1.val-t0.val) > (unsigned)(t3.val-t2.val) * 3);
#endif

	memcpy(buf + dest_offset + 1, buf + 1, len - 1);
	TEST_ASSERT_ARRAY_EQ(buf + dest_offset + 1, buf + 1, len - 1);

	/* Test small copies */
	memcpy(buf + dest_offset, buf, 1);
	TEST_ASSERT_ARRAY_EQ(buf + dest_offset, buf, 1);
	memcpy(buf + dest_offset, buf, 4);
	TEST_ASSERT_ARRAY_EQ(buf + dest_offset, buf, 4);
	memcpy(buf + dest_offset + 1, buf, 1);
	TEST_ASSERT_ARRAY_EQ(buf + dest_offset + 1, buf, 1);
	memcpy(buf + dest_offset + 1, buf, 4);
	TEST_ASSERT_ARRAY_EQ(buf + dest_offset + 1, buf, 4);

	shared_mem_release(buf);
	return EC_SUCCESS;
}

/* Plain memset, used as a reference to measure speed gain */
static void *dumb_memset(void *dest, int c, int len)
{
	char *d = (char *)dest;
	while (len > 0) {
		*(d++) = c;
		len--;
	}
	return dest;
}

static int test_memset(void)
{
	int i;
	timestamp_t t0, t1, t2, t3;
	char *buf;
	const int buf_size = 1000;
	const int len = 400;
	const int iteration = 1000;

	TEST_ASSERT(shared_mem_acquire(buf_size, &buf) == EC_SUCCESS);

	t0 = get_time();
	for (i = 0; i < iteration; ++i)
		dumb_memset(buf, 1, len);
	t1 = get_time();
	TEST_ASSERT_MEMSET(buf, (char)1, len);
	ccprintf(" (speed gain: %" PRId64 " ->", t1.val-t0.val);

	t2 = get_time();
	for (i = 0; i < iteration; ++i)
		memset(buf, 1, len);
	t3 = get_time();
	TEST_ASSERT_MEMSET(buf, (char)1, len);
	ccprintf(" %" PRId64 " us) ", t3.val-t2.val);

	/*
	 * Expected about 4x speed gain. Use smaller value since it
	 * fluctuates.
	 */
	if (!IS_ENABLED(EMU_BUILD)) {
		/*
		 * The speed gain is too unpredictable on host, especially on
		 * buildbots. Skip it if we are running in the emulator.
		 */
		int expected_speedup = 3;

		if (IS_ENABLED(CHIP_FAMILY_STM32F4))
			expected_speedup = 2;

		TEST_ASSERT((t1.val - t0.val) >
			    (unsigned int)(t3.val - t2.val) * expected_speedup);
	}

	memset(buf, 128, len);
	TEST_ASSERT_MEMSET(buf, (char)128, len);

	memset(buf, -2, len);
	TEST_ASSERT_MEMSET(buf, (char)-2, len);

	memset(buf + 1, 1, len - 2);
	TEST_ASSERT_MEMSET(buf + 1, (char)1, len - 2);

	shared_mem_release(buf);
	return EC_SUCCESS;
}

static int test_memchr(void)
{
	char *buf = "1234";

	TEST_ASSERT(memchr("123567890", '4', 8) == NULL);
	TEST_ASSERT(memchr("123", '3', 2) == NULL);
	TEST_ASSERT(memchr(buf, '3', 4) == buf + 2);
	TEST_ASSERT(memchr(buf, '4', 4) == buf + 3);
	return EC_SUCCESS;
}

static int test_uint64divmod_0(void)
{
	uint64_t n = 8567106442584750ULL;
	int d = 54870071;
	int r = uint64divmod(&n, d);

	TEST_CHECK(r == 5991285 && n == 156134415ULL);
}

static int test_uint64divmod_1(void)
{
	uint64_t n = 8567106442584750ULL;
	int d = 2;
	int r = uint64divmod(&n, d);

	TEST_CHECK(r == 0 && n == 4283553221292375ULL);
}

static int test_uint64divmod_2(void)
{
	uint64_t n = 8567106442584750ULL;
	int d = 0;
	int r = uint64divmod(&n, d);

	TEST_CHECK(r == 0 && n == 0ULL);
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
	char *mem1, *mem2;

	TEST_ASSERT(shared_mem_acquire(sz, &mem1) == EC_SUCCESS);
	TEST_ASSERT(shared_mem_acquire(sz, &mem2) == EC_ERROR_BUSY);

	for (i = 0; i < 256; ++i) {
		memset(mem1, i, sz);
		TEST_ASSERT_MEMSET(mem1, (char)i, sz);
		if ((i & 0xf) == 0)
			msleep(20); /* Yield to other tasks */
	}

	shared_mem_release(mem1);

	return EC_SUCCESS;
}

static int test_scratchpad(void)
{
	system_set_scratchpad(0xfeed);
	TEST_ASSERT(system_get_scratchpad() == 0xfeed);

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
		watchdog_reload();
	}
	t1 = get_time();

	ccprintf("After %d iterations, r=%08x%08x, r2=%08x%08x (time: %d)\n",
		i, (uint32_t)(r >> 32), (uint32_t)r,
		(uint32_t)(r2 >> 32), (uint32_t)r2, t1.le.lo-t0.le.lo);
	TEST_ASSERT(r  == 0x9df59b9fb0ab9d96L);
	TEST_ASSERT(r2 == 0x9df59b9fb0beabd6L);

	/* well okay then */
	return EC_SUCCESS;
}

#define SWAP_TEST_HARNESS(t, x, y) \
	do { \
		t a = x, b = y; \
		swap(a, b); \
		TEST_ASSERT(a == y); \
		TEST_ASSERT(b == x); \
	} while (0)


static int test_swap(void)
{
	SWAP_TEST_HARNESS(uint8_t, UINT8_MAX, 0);
	SWAP_TEST_HARNESS(uint16_t, UINT16_MAX, 0);
	SWAP_TEST_HARNESS(uint32_t, UINT32_MAX, 0);
	SWAP_TEST_HARNESS(float, 1, 0);
	SWAP_TEST_HARNESS(double, 1, 0);
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

	BUILD_ASSERT(str1 != str3);

	TEST_EQ(safe_memcmp(NULL, NULL, 0), 0, "%d");
	TEST_EQ(safe_memcmp(str1, str2, sizeof(str1)), 1, "%d");
	TEST_EQ(safe_memcmp(str1, str3, sizeof(str1)), 0, "%d");
	return EC_SUCCESS;
}

void run_test(int argc, char **argv)
{
	test_reset();

	RUN_TEST(test_memmove);
	RUN_TEST(test_memcpy);
	RUN_TEST(test_memset);
	RUN_TEST(test_memchr);
	RUN_TEST(test_uint64divmod_0);
	RUN_TEST(test_uint64divmod_1);
	RUN_TEST(test_uint64divmod_2);
	RUN_TEST(test_get_next_bit);
	RUN_TEST(test_shared_mem);
	RUN_TEST(test_scratchpad);
	RUN_TEST(test_cond_t);
	RUN_TEST(test_mula32);
	RUN_TEST(test_swap);
	RUN_TEST(test_bytes_are_trivial);
	RUN_TEST(test_is_aligned);
	RUN_TEST(test_safe_memcmp);

	test_print_result();
}
