/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Test standard library functions.
 */

#include "common.h"
#include "compiler.h"
#include "console.h"
#include "printf.h"
#include "shared_mem.h"
#include "system.h"
#include "test_util.h"
#include "timer.h"
#include "util.h"

#ifndef USE_BUILTIN_STDLIB
/* This is ugly, but we want to test the functions in builtin/stdlib.c while
 * still depending on the system stdlib.c
 */
#define snprintf TESTED_snprintf
#include "../builtin/stdlib.c"
#endif

static int test_isalpha(void)
{
	TEST_ASSERT(isalpha('a'));
	TEST_ASSERT(isalpha('z'));
	TEST_ASSERT(isalpha('A'));
	TEST_ASSERT(isalpha('Z'));
	TEST_ASSERT(!isalpha('0'));
	TEST_ASSERT(!isalpha('~'));
	TEST_ASSERT(!isalpha(' '));
	TEST_ASSERT(!isalpha('\0'));
	TEST_ASSERT(!isalpha('\n'));
	return EC_SUCCESS;
}

static int test_isupper(void)
{
	TEST_ASSERT(!isupper('a'));
	TEST_ASSERT(!isupper('z'));
	TEST_ASSERT(isupper('A'));
	TEST_ASSERT(isupper('Z'));
	TEST_ASSERT(!isupper('0'));
	TEST_ASSERT(!isupper('~'));
	TEST_ASSERT(!isupper(' '));
	TEST_ASSERT(!isupper('\0'));
	TEST_ASSERT(!isupper('\n'));
	return EC_SUCCESS;
}

static int test_isprint(void)
{
	TEST_ASSERT(isprint('a'));
	TEST_ASSERT(isprint('z'));
	TEST_ASSERT(isprint('A'));
	TEST_ASSERT(isprint('Z'));
	TEST_ASSERT(isprint('0'));
	TEST_ASSERT(isprint('~'));
	TEST_ASSERT(isprint(' '));
	TEST_ASSERT(!isprint('\0'));
	TEST_ASSERT(!isprint('\n'));
	return EC_SUCCESS;
}

static int test_strstr(void)
{
	const char s1[] = "abcde";

	TEST_ASSERT(strstr(s1, "ab") == s1);
	/*
	 * TODO(http://b/243192369): This is incorrect and should be
	 * fixed.
	 *
	 * From the man page: If needle is the empty string, the return
	 * value is always haystack itself.
	 * TEST_ASSERT(strstr(s1, "") == s1);
	 */
	TEST_ASSERT(strstr(s1, "") == NULL);
	TEST_ASSERT(strstr("", "ab") == NULL);
	TEST_ASSERT(strstr("", "x") == NULL);
	TEST_ASSERT(strstr(s1, "de") == &s1[3]);
	TEST_ASSERT(strstr(s1, "def") == NULL);

	return EC_SUCCESS;
}

static int test_strtoull(void)
{
	char *e;

	TEST_ASSERT(strtoull("10", &e, 0) == 10);
	TEST_ASSERT(e && (*e == '\0'));
	TEST_ASSERT(strtoull("010", &e, 0) == 8);
	TEST_ASSERT(e && (*e == '\0'));
	TEST_ASSERT(strtoull("+010", &e, 0) == 8);
	TEST_ASSERT(e && (*e == '\0'));

	/*
	 * TODO(http://b/243192369): This is incorrect and should be
	 * fixed.
	 *
	 * From the man page: The strtoull() function returns either
	 * the result of the conversion or, if there was a leading
	 * minus sign, the negation of the result of the conversion
	 * represented as an unsigned value, unless the original
	 * (nonnegated) value would overflow
	 * TEST_ASSERT(strtoull("-010", &e, 0) == 0xFFFFFFFFFFFFFFF8);
	 */
	TEST_ASSERT(strtoull("-010", &e, 0) == 0);
	TEST_ASSERT(e && (*e == '-'));

	TEST_ASSERT(strtoull("0x1f z", &e, 0) == 31);
	TEST_ASSERT(e && (*e == ' '));
	TEST_ASSERT(strtoull("0X1f z", &e, 0) == 31);
	TEST_ASSERT(e && (*e == ' '));
	TEST_ASSERT(strtoull("10a", &e, 16) == 266);
	TEST_ASSERT(e && (*e == '\0'));
	TEST_ASSERT(strtoull("0x02C", &e, 16) == 44);
	TEST_ASSERT(e && (*e == '\0'));
	TEST_ASSERT(strtoull("+0x02C", &e, 16) == 44);
	TEST_ASSERT(e && (*e == '\0'));

	/*
	 * TODO(http://b/243192369): This is incorrect and should be
	 * fixed.
	 * TEST_ASSERT(strtoull("-0x02C", &e, 16) == 0xFFFFFFFFFFFFFFD4);
	 */
	TEST_ASSERT(strtoull("-0x02C", &e, 16) == 0);
	TEST_ASSERT(e && (*e == '-'));

	TEST_ASSERT(strtoull("0x02C", &e, 0) == 44);
	TEST_ASSERT(e && (*e == '\0'));
	TEST_ASSERT(strtoull("+0x02C", &e, 0) == 44);
	TEST_ASSERT(e && (*e == '\0'));

	/*
	 * TODO(http://b/243192369): This is incorrect and should be
	 * fixed.
	 * TEST_ASSERT(strtoull("-0x02C", &e, 0) == 0xFFFFFFFFFFFFFFD4);
	 */
	TEST_ASSERT(strtoull("-0x02C", &e, 0) == 0);
	TEST_ASSERT(e && (*e == '-'));

	TEST_ASSERT(strtoull("0X02C", &e, 16) == 44);
	TEST_ASSERT(e && (*e == '\0'));
	TEST_ASSERT(strtoull("+0X02C", &e, 16) == 44);
	TEST_ASSERT(e && (*e == '\0'));

	/*
	 * TODO(http://b/243192369): This is incorrect and should be
	 * fixed.
	 * TEST_ASSERT(strtoull("-0X02C", &e, 16) == 0xFFFFFFFFFFFFFFD4);
	 */
	TEST_ASSERT(strtoull("-0X02C", &e, 16) == 0);
	TEST_ASSERT(e && (*e == '-'));

	TEST_ASSERT(strtoull("0X02C", &e, 0) == 44);
	TEST_ASSERT(e && (*e == '\0'));
	TEST_ASSERT(strtoull("+0X02C", &e, 0) == 44);
	TEST_ASSERT(e && (*e == '\0'));

	/*
	 * TODO(http://b/243192369): This is incorrect and should be
	 * fixed.
	 * TEST_ASSERT(strtoull("-0X02C", &e, 0) == 0xFFFFFFFFFFFFFFD4);
	 */
	TEST_ASSERT(strtoull("-0X02C", &e, 0) == 0);
	TEST_ASSERT(e && (*e == '-'));

	/*
	 * TODO(http://b/243192369): This is incorrect and should be
	 * fixed.
	 * TEST_ASSERT(strtoull("   -12", &e, 0) == 0xFFFFFFFFFFFFFFF4);
	 */
	TEST_ASSERT(strtoull("   -12", &e, 0) == 0);
	TEST_ASSERT(e && (*e == '-'));

	TEST_ASSERT(strtoull("!", &e, 0) == 0);
	TEST_ASSERT(e && (*e == '!'));

	TEST_ASSERT(strtoull("+!", &e, 0) == 0);
	/*
	 * TODO(http://b/243192369): This is incorrect and should be
	 * fixed.
	 * TEST_ASSERT(e && (*e == '+'));
	 */
	TEST_ASSERT(e && (*e == '!'));

	TEST_ASSERT(strtoull("+0!", &e, 0) == 0);
	TEST_ASSERT(e && (*e == '!'));

	TEST_ASSERT(strtoull("+0x!", &e, 0) == 0);
	/*
	 * TODO(http://b/243192369): This is incorrect and should be
	 * fixed.
	 * TEST_ASSERT(e && (*e == '+'));
	 */
	TEST_ASSERT(e && (*e == '!'));

	TEST_ASSERT(strtoull("+0X!", &e, 0) == 0);
	/*
	 * TODO(http://b/243192369): This is incorrect and should be
	 * fixed.
	 * TEST_ASSERT(e && (*e == '+'));
	 */
	TEST_ASSERT(e && (*e == '!'));

	return EC_SUCCESS;
}

static int test_strncpy(void)
{
	char dest[10];

	strncpy(dest, "test", 10);
	TEST_ASSERT_ARRAY_EQ("test", dest, 5);
	strncpy(dest, "12345", 6);
	TEST_ASSERT_ARRAY_EQ("12345", dest, 6);
	/*
	 * gcc complains:
	 * error: ‘__builtin_strncpy’ output truncated copying 10 bytes from a
	 * string of length 12 [-Werror=stringop-truncation]
	 */
	DISABLE_GCC_WARNING("-Wstringop-truncation");
	strncpy(dest, "testtesttest", 10);
	ENABLE_GCC_WARNING("-Wstringop-truncation");
	TEST_ASSERT_ARRAY_EQ("testtestte", dest, 10);

	return EC_SUCCESS;
}

static int test_strncmp(void)
{
	TEST_ASSERT(strncmp("123", "123", 8) == 0);
	TEST_ASSERT(strncmp("789", "456", 8) > 0);
	TEST_ASSERT(strncmp("abc", "abd", 4) < 0);
	TEST_ASSERT(strncmp("abc", "abd", 2) == 0);
	return EC_SUCCESS;
}

static int test_memcmp(void)
{
	TEST_ASSERT(memcmp("12345678", "12345678", 8) == 0);
	TEST_ASSERT(memcmp("78945612", "45612378", 8) > 0);
	TEST_ASSERT(memcmp("abc", "abd", 4) < 0);
	TEST_ASSERT(memcmp("abc", "abd", 2) == 0);
	return EC_SUCCESS;
}

static int test_strlen(void)
{
	TEST_ASSERT(strlen("this is a string") == 16);
	return EC_SUCCESS;
}

static int test_strnlen(void)
{
	TEST_ASSERT(strnlen("this is a string", 17) == 16);
	TEST_ASSERT(strnlen("this is a string", 16) == 16);
	TEST_ASSERT(strnlen("this is a string", 5) == 5);

	return EC_SUCCESS;
}

static int test_strcasecmp(void)
{
	TEST_ASSERT(strcasecmp("test string", "TEST strIng") == 0);
	TEST_ASSERT(strcasecmp("test123!@#", "TesT123!@#") == 0);
	TEST_ASSERT(strcasecmp("lower", "UPPER") != 0);
	return EC_SUCCESS;
}

static int test_strncasecmp(void)
{
	TEST_ASSERT(strncasecmp("test string", "TEST str", 4) == 0);
	TEST_ASSERT(strncasecmp("test string", "TEST str", 8) == 0);
	TEST_ASSERT(strncasecmp("test123!@#", "TesT321!@#", 5) != 0);
	TEST_ASSERT(strncasecmp("test123!@#", "TesT321!@#", 4) == 0);
	TEST_ASSERT(strncasecmp("1test123!@#", "1TesT321!@#", 5) == 0);
	TEST_ASSERT(strncasecmp("1test123", "teststr", 0) == 0);
	return EC_SUCCESS;
}

static int test_atoi(void)
{
	TEST_ASSERT(atoi("  901") == 901);
	TEST_ASSERT(atoi("-12c") == -12);
	TEST_ASSERT(atoi("   0  ") == 0);
	TEST_ASSERT(atoi("\t111") == 111);
	return EC_SUCCESS;
}

static int test_snprintf(void)
{
	char buffer[32];

	TEST_ASSERT(snprintf(buffer, sizeof(buffer), "%u", 1234) == 4);
	TEST_ASSERT(strncmp(buffer, "1234", sizeof(buffer)) == 0);
	return EC_SUCCESS;
}

static int test_strcspn(void)
{
	const char str1[] = "abc";
	const char str2[] = "This is a string\nwith newlines!";

	TEST_EQ(strcspn(str1, "a"), (size_t)0, "%zu");
	TEST_EQ(strcspn(str1, "b"), (size_t)1, "%zu");
	TEST_EQ(strcspn(str1, "c"), (size_t)2, "%zu");
	TEST_EQ(strcspn(str1, "ccc"), (size_t)2, "%zu");
	TEST_EQ(strcspn(str1, "cba"), (size_t)0, "%zu");
	TEST_EQ(strcspn(str1, "cb"), (size_t)1, "%zu");
	TEST_EQ(strcspn(str1, "bc"), (size_t)1, "%zu");
	TEST_EQ(strcspn(str1, "cbc"), (size_t)1, "%zu");
	TEST_EQ(strcspn(str1, "z"), strlen(str1), "%zu");
	TEST_EQ(strcspn(str1, "xyz"), strlen(str1), "%zu");
	TEST_EQ(strcspn(str1, ""), strlen(str1), "%zu");

	TEST_EQ(strcspn(str2, " "), (size_t)4, "%zu");
	TEST_EQ(strcspn(str2, "\n"), (size_t)16, "%zu");
	TEST_EQ(strcspn(str2, "\n "), (size_t)4, "%zu");
	TEST_EQ(strcspn(str2, "!"), strlen(str2) - 1, "%zu");
	TEST_EQ(strcspn(str2, "z"), strlen(str2), "%zu");
	TEST_EQ(strcspn(str2, "z!"), strlen(str2) - 1, "%zu");

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
		memmove(buf + 101, buf, len); /* unaligned */
	t1 = get_time();
	TEST_ASSERT_ARRAY_EQ(buf + 101, buf, len);
	ccprintf(" (speed gain: %" PRId64 " ->", t1.val - t0.val);

	t2 = get_time();
	for (i = 0; i < iteration; ++i)
		memmove(buf + 100, buf, len); /* aligned */
	t3 = get_time();
	ccprintf(" %" PRId64 " us) ", t3.val - t2.val);
	TEST_ASSERT_ARRAY_EQ(buf + 100, buf, len);

	if (!IS_ENABLED(EMU_BUILD))
		TEST_ASSERT((t1.val - t0.val) > (t3.val - t2.val));

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
		memcpy(buf + dest_offset + 1, buf, len); /* unaligned */
	t1 = get_time();
	TEST_ASSERT_ARRAY_EQ(buf + dest_offset + 1, buf, len);
	ccprintf(" (speed gain: %" PRId64 " ->", t1.val - t0.val);

	t2 = get_time();
	for (i = 0; i < iteration; ++i)
		memcpy(buf + dest_offset, buf, len); /* aligned */
	t3 = get_time();
	ccprintf(" %" PRId64 " us) ", t3.val - t2.val);
	TEST_ASSERT_ARRAY_EQ(buf + dest_offset, buf, len);

	if (!IS_ENABLED(EMU_BUILD))
		TEST_ASSERT((t1.val - t0.val) > (t3.val - t2.val));

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
	ccprintf(" (speed gain: %" PRId64 " ->", t1.val - t0.val);

	t2 = get_time();
	for (i = 0; i < iteration; ++i)
		memset(buf, 1, len);
	t3 = get_time();
	TEST_ASSERT_MEMSET(buf, (char)1, len);
	ccprintf(" %" PRId64 " us) ", t3.val - t2.val);

	if (!IS_ENABLED(EMU_BUILD))
		TEST_ASSERT((t1.val - t0.val) > (t3.val - t2.val));

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

void run_test(int argc, const char **argv)
{
	test_reset();

	RUN_TEST(test_isalpha);
	RUN_TEST(test_isupper);
	RUN_TEST(test_isprint);
	RUN_TEST(test_strstr);
	RUN_TEST(test_strtoull);
	RUN_TEST(test_strncpy);
	RUN_TEST(test_strncmp);
	RUN_TEST(test_strlen);
	RUN_TEST(test_strnlen);
	RUN_TEST(test_strcasecmp);
	RUN_TEST(test_strncasecmp);
	RUN_TEST(test_atoi);
	RUN_TEST(test_snprintf);
	RUN_TEST(test_strcspn);
	RUN_TEST(test_memmove);
	RUN_TEST(test_memcpy);
	RUN_TEST(test_memset);
	RUN_TEST(test_memchr);
	RUN_TEST(test_memcmp);

	test_print_result();
}
