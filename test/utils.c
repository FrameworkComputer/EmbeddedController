/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
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

static int test_isalpha(void)
{
	TEST_CHECK(isalpha('a') && isalpha('z') && isalpha('A') &&
		   isalpha('Z') && !isalpha('0') && !isalpha('~') &&
		   !isalpha(' ') && !isalpha('\0') && !isalpha('\n'));
}

static int test_isprint(void)
{
	TEST_CHECK(isprint('a') && isprint('z') && isprint('A') &&
		   isprint('Z') && isprint('0') && isprint('~') &&
		   isprint(' ') && !isprint('\0') && !isprint('\n'));
}

static int test_strtoi(void)
{
	char *e;

	TEST_ASSERT(strtoi("10", &e, 0) == 10);
	TEST_ASSERT(e && (*e == '\0'));
	TEST_ASSERT(strtoi("0x1f z", &e, 0) == 31);
	TEST_ASSERT(e && (*e == ' '));
	TEST_ASSERT(strtoi("10a", &e, 16) == 266);
	TEST_ASSERT(e && (*e == '\0'));
	TEST_ASSERT(strtoi("0x02C", &e, 16) == 44);
	TEST_ASSERT(e && (*e == '\0'));
	TEST_ASSERT(strtoi("   -12", &e, 0) == -12);
	TEST_ASSERT(e && (*e == '\0'));
	TEST_ASSERT(strtoi("!", &e, 0) == 0);
	TEST_ASSERT(e && (*e == '!'));

	return EC_SUCCESS;
}

static int test_parse_bool(void)
{
	int v;

	TEST_ASSERT(parse_bool("on", &v) == 1);
	TEST_ASSERT(v == 1);
	TEST_ASSERT(parse_bool("off", &v) == 1);
	TEST_ASSERT(v == 0);
	TEST_ASSERT(parse_bool("enable", &v) == 1);
	TEST_ASSERT(v == 1);
	TEST_ASSERT(parse_bool("disable", &v) == 1);
	TEST_ASSERT(v == 0);
	TEST_ASSERT(parse_bool("di", &v) == 0);
	TEST_ASSERT(parse_bool("en", &v) == 0);
	TEST_ASSERT(parse_bool("of", &v) == 0);

	return EC_SUCCESS;
}

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
	ccprintf(" (speed gain: %d ->", t1.val-t0.val);

	t2 = get_time();
	for (i = 0; i < iteration; ++i)
		memmove(buf + 100, buf, len);	  /* aligned */
	t3 = get_time();
	ccprintf(" %d us) ", t3.val-t2.val);
	TEST_ASSERT_ARRAY_EQ(buf + 100, buf, len);

	/* Expected about 4x speed gain. Use 3x because it fluctuates */
	TEST_ASSERT((t1.val-t0.val) > (t3.val-t2.val) * 3);

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
	ccprintf(" (speed gain: %d ->", t1.val-t0.val);

	t2 = get_time();
	for (i = 0; i < iteration; ++i)
		memcpy(buf + dest_offset, buf, len);	  /* aligned */
	t3 = get_time();
	ccprintf(" %d us) ", t3.val-t2.val);
	TEST_ASSERT_ARRAY_EQ(buf + dest_offset, buf, len);

	/* Expected about 4x speed gain. Use 3x because it fluctuates */
	TEST_ASSERT((t1.val-t0.val) > (t3.val-t2.val) * 3);

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
	ccprintf(" (speed gain: %d ->", t1.val-t0.val);

	t2 = get_time();
	for (i = 0; i < iteration; ++i)
		memset(buf, 1, len);
	t3 = get_time();
	TEST_ASSERT_MEMSET(buf, (char)1, len);
	ccprintf(" %d us) ", t3.val-t2.val);

	/* Expected about 4x speed gain. Use 3x because it fluctuates */
	TEST_ASSERT((t1.val-t0.val) > (t3.val-t2.val) * 3);

	memset(buf, 128, len);
	TEST_ASSERT_MEMSET(buf, (char)128, len);

	memset(buf, -2, len);
	TEST_ASSERT_MEMSET(buf, (char)-2, len);

	memset(buf + 1, 1, len - 2);
	TEST_ASSERT_MEMSET(buf + 1, (char)1, len - 2);

	shared_mem_release(buf);
	return EC_SUCCESS;
}

static int test_strzcpy(void)
{
	char dest[10];

	strzcpy(dest, "test", 10);
	TEST_ASSERT_ARRAY_EQ("test", dest, 5);
	strzcpy(dest, "testtesttest", 10);
	TEST_ASSERT_ARRAY_EQ("testtestt", dest, 10);
	strzcpy(dest, "aaaa", -1);
	TEST_ASSERT_ARRAY_EQ("testtestt", dest, 10);

	return EC_SUCCESS;
}

static int test_strlen(void)
{
	TEST_CHECK(strlen("this is a string") == 16);
}

static int test_strcasecmp(void)
{
	TEST_CHECK((strcasecmp("test string", "TEST strIng") == 0) &&
		   (strcasecmp("test123!@#", "TesT123!@#") == 0) &&
		   (strcasecmp("lower", "UPPER") != 0));
}

static int test_strncasecmp(void)
{
	TEST_CHECK((strncasecmp("test string", "TEST str", 4) == 0) &&
		   (strncasecmp("test string", "TEST str", 8) == 0) &&
		   (strncasecmp("test123!@#", "TesT321!@#", 5) != 0) &&
		   (strncasecmp("test123!@#", "TesT321!@#", 4) == 0) &&
		   (strncasecmp("1test123!@#", "1TesT321!@#", 5) == 0) &&
		   (strncasecmp("1test123", "teststr", 0) == 0));
}

static int test_atoi(void)
{
	TEST_CHECK((atoi("  901") == 901) &&
		   (atoi("-12c") == -12) &&
		   (atoi("   0  ") == 0) &&
		   (atoi("\t111") == 111));
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
	char *mem;

	TEST_ASSERT(shared_mem_acquire(sz, &mem) == EC_SUCCESS);
	TEST_ASSERT(shared_mem_acquire(sz, &mem) == EC_ERROR_BUSY);

	for (i = 0; i < 256; ++i) {
		memset(mem, i, sz);
		TEST_ASSERT_MEMSET(mem, (char)i, sz);
		if ((i & 0xf) == 0)
			msleep(20); /* Yield to other tasks */
	}

	shared_mem_release(mem);

	return EC_SUCCESS;
}

static int test_scratchpad(void)
{
	system_set_scratchpad(0xfeedfeed);
	TEST_ASSERT(system_get_scratchpad() == 0xfeedfeed);

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

void run_test(void)
{
	test_reset();

	RUN_TEST(test_isalpha);
	RUN_TEST(test_isprint);
	RUN_TEST(test_strtoi);
	RUN_TEST(test_parse_bool);
	RUN_TEST(test_memmove);
	RUN_TEST(test_memcpy);
	RUN_TEST(test_memset);
	RUN_TEST(test_strzcpy);
	RUN_TEST(test_strlen);
	RUN_TEST(test_strcasecmp);
	RUN_TEST(test_strncasecmp);
	RUN_TEST(test_atoi);
	RUN_TEST(test_uint64divmod_0);
	RUN_TEST(test_uint64divmod_1);
	RUN_TEST(test_uint64divmod_2);
	RUN_TEST(test_get_next_bit);
	RUN_TEST(test_shared_mem);
	RUN_TEST(test_scratchpad);
	RUN_TEST(test_cond_t);

	test_print_result();
}
