/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "compiler.h"
#include "console.h"
#include "shared_mem.h"
#include "timer.h"

#include <ctype.h>
#include <stdlib.h>

#include <zephyr/ztest.h>

#include <strings.h>

#ifdef CONFIG_ARCH_POSIX
extern size_t strnlen(const char *, size_t);
#endif /* CONFIG_ARCH_POSIX */

/* TODO(b/349553642): Track upstreaming the stdlib tests. */
ZTEST_SUITE(stdlib, NULL, NULL, NULL, NULL, NULL);

__no_optimization static void test_isalpha(void)
{
	zassert_true(isalpha('a'));
	zassert_true(isalpha('z'));
	zassert_true(isalpha('A'));
	zassert_true(isalpha('Z'));
	zassert_true(!isalpha('0'));
	zassert_true(!isalpha('~'));
	zassert_true(!isalpha(' '));
	zassert_true(!isalpha('\0'));
	zassert_true(!isalpha('\n'));
}

ZTEST(stdlib, test_isalpha)
{
	test_isalpha();
}

__no_optimization static void test_isupper(void)
{
	zassert_true(!isupper('a'));
	zassert_true(!isupper('z'));
	zassert_true(isupper('A'));
	zassert_true(isupper('Z'));
	zassert_true(!isupper('0'));
	zassert_true(!isupper('~'));
	zassert_true(!isupper(' '));
	zassert_true(!isupper('\0'));
	zassert_true(!isupper('\n'));
}

ZTEST(stdlib, test_isupper)
{
	test_isupper();
}

__no_optimization static void test_isprint(void)
{
	zassert_true(isprint('a'));
	zassert_true(isprint('z'));
	zassert_true(isprint('A'));
	zassert_true(isprint('Z'));
	zassert_true(isprint('0'));
	zassert_true(isprint('~'));
	zassert_true(isprint(' '));
	zassert_true(!isprint('\0'));
	zassert_true(!isprint('\n'));
}

ZTEST(stdlib, test_isprint)
{
	test_isprint();
}

__no_optimization static void test_strstr(void)
{
	const char s1[] = "abcde";

	zassert_true(strstr(s1, "ab") == s1);
	zassert_true(strstr(s1, "") == s1);
	zassert_true(strstr("", "ab") == NULL);
	zassert_true(strstr("", "x") == NULL);
	zassert_true(strstr(s1, "de") == &s1[3]);
	zassert_true(strstr(s1, "def") == NULL);
}

ZTEST(stdlib, test_strstr)
{
	test_strstr();
}

__no_optimization static void test_strtoull(void)
{
	char *e;

	zassert_true(strtoull("10", &e, 0) == 10);
	zassert_true(e && (*e == '\0'));
	zassert_true(strtoull("010", &e, 0) == 8);
	zassert_true(e && (*e == '\0'));
	zassert_true(strtoull("+010", &e, 0) == 8);
	zassert_true(e && (*e == '\0'));

	zassert_true(strtoull("-010", &e, 0) == 0xFFFFFFFFFFFFFFF8);
	zassert_true(e && (*e == '\0'));

	zassert_true(strtoull("0x1f z", &e, 0) == 31);
	zassert_true(e && (*e == ' '));
	zassert_true(strtoull("0X1f z", &e, 0) == 31);
	zassert_true(e && (*e == ' '));
	zassert_true(strtoull("10a", &e, 16) == 266);
	zassert_true(e && (*e == '\0'));
	zassert_true(strtoull("0x02C", &e, 16) == 44);
	zassert_true(e && (*e == '\0'));
	zassert_true(strtoull("+0x02C", &e, 16) == 44);
	zassert_true(e && (*e == '\0'));

	zassert_true(strtoull("-0x02C", &e, 16) == 0xFFFFFFFFFFFFFFD4);
	zassert_true(e && (*e == '\0'));

	zassert_true(strtoull("0x02C", &e, 0) == 44);
	zassert_true(e && (*e == '\0'));
	zassert_true(strtoull("+0x02C", &e, 0) == 44);
	zassert_true(e && (*e == '\0'));

	zassert_true(strtoull("-0x02C", &e, 0) == 0xFFFFFFFFFFFFFFD4);
	zassert_true(e && (*e == '\0'));

	zassert_true(strtoull("0X02C", &e, 16) == 44);
	zassert_true(e && (*e == '\0'));
	zassert_true(strtoull("+0X02C", &e, 16) == 44);
	zassert_true(e && (*e == '\0'));

	zassert_true(strtoull("-0X02C", &e, 16) == 0xFFFFFFFFFFFFFFD4);
	zassert_true(e && (*e == '\0'));

	zassert_true(strtoull("0X02C", &e, 0) == 44);
	zassert_true(e && (*e == '\0'));
	zassert_true(strtoull("+0X02C", &e, 0) == 44);
	zassert_true(e && (*e == '\0'));

	zassert_true(strtoull("-0X02C", &e, 0) == 0xFFFFFFFFFFFFFFD4);
	zassert_true(e && (*e == '\0'));

	zassert_true(strtoull("   -12", &e, 0) == 0xFFFFFFFFFFFFFFF4);
	zassert_true(e && (*e == '\0'));

	zassert_true(strtoull("!", &e, 0) == 0);
	zassert_true(e && (*e == '!'));

	zassert_true(strtoull("+!", &e, 0) == 0);
	zassert_true(e && (*e == '+'));

	zassert_true(strtoull("+0!", &e, 0) == 0);
	zassert_true(e && (*e == '!'));

	zassert_true(strtoull("+0x!", &e, 0) == 0);
	/* TODO(b/354655290): This doesn't work as expected on posix. */
	if (!IS_ENABLED(CONFIG_ARCH_POSIX))
		zassert_true(e && (*e == '+'));

	zassert_true(strtoull("+0X!", &e, 0) == 0);
	/* TODO(b/354655290): This doesn't work as expected on posix. */
	if (!IS_ENABLED(CONFIG_ARCH_POSIX))
		zassert_true(e && (*e == '+'));
}

ZTEST(stdlib, test_strtoull)
{
	test_strtoull();
}

__no_optimization static void test_strncpy(void)
{
	char dest[10];

	strncpy(dest, "test", 10);
	zassert_mem_equal("test", dest, 5);
	strncpy(dest, "12345", 6);
	zassert_mem_equal("12345", dest, 6);
	/*
	 * gcc complains:
	 * error: ‘__builtin_strncpy’ output truncated copying 10 bytes from a
	 * string of length 12 [-Werror=stringop-truncation]
	 */
	DISABLE_GCC_WARNING("-Wstringop-truncation");
	strncpy(dest, "testtesttest", 10);
	ENABLE_GCC_WARNING("-Wstringop-truncation");
	zassert_mem_equal("testtestte", dest, 10);
}

ZTEST(stdlib, test_strncpy)
{
	test_strncpy();
}

__no_optimization static void test_strncmp(void)
{
	zassert_true(strncmp("123", "123", 8) == 0);
	zassert_true(strncmp("789", "456", 8) > 0);
	zassert_true(strncmp("abc", "abd", 4) < 0);
	zassert_true(strncmp("abc", "abd", 2) == 0);
}

ZTEST(stdlib, test_strncmp)
{
	test_strncmp();
}

__no_optimization static void test_memcmp(void)
{
	zassert_true(memcmp("12345678", "12345678", 8) == 0);
	zassert_true(memcmp("78945612", "45612378", 8) > 0);
	zassert_true(memcmp("abc", "abd", 4) < 0);
	zassert_true(memcmp("abc", "abd", 2) == 0);
}

ZTEST(stdlib, test_memcmp)
{
	test_memcmp();
}

__no_optimization static void test_strlen(void)
{
	zassert_true(strlen("this is a string") == 16);
}

ZTEST(stdlib, test_strlen)
{
	test_strlen();
}

__no_optimization static void test_strnlen(void)
{
	zassert_true(strnlen("this is a string", 17) == 16);
	zassert_true(strnlen("this is a string", 16) == 16);
	zassert_true(strnlen("this is a string", 5) == 5);
}

ZTEST(stdlib, test_strnlen)
{
	test_strnlen();
}

__no_optimization static void test_strcasecmp(void)
{
	zassert_true(strcasecmp("test string", "TEST strIng") == 0);
	zassert_true(strcasecmp("test123!@#", "TesT123!@#") == 0);
	zassert_true(strcasecmp("lower", "UPPER") != 0);
}

ZTEST(stdlib, test_strcasecmp)
{
	test_strcasecmp();
}

__no_optimization static void test_strncasecmp(void)
{
	zassert_true(strncasecmp("test string", "TEST str", 4) == 0);
	zassert_true(strncasecmp("test string", "TEST str", 8) == 0);
	zassert_true(strncasecmp("test123!@#", "TesT321!@#", 5) != 0);
	zassert_true(strncasecmp("test123!@#", "TesT321!@#", 4) == 0);
	zassert_true(strncasecmp("1test123!@#", "1TesT321!@#", 5) == 0);
	zassert_true(strncasecmp("1test123", "teststr", 0) == 0);
}

ZTEST(stdlib, test_strncasecmp)
{
	test_strncasecmp();
}

__no_optimization static void test_atoi(void)
{
	zassert_true(atoi("  901") == 901);
	zassert_true(atoi("-12c") == -12);
	zassert_true(atoi("   0  ") == 0);
	zassert_true(atoi("\t111") == 111);
}

ZTEST(stdlib, test_atoi)
{
	test_atoi();
}

__no_optimization static void test_snprintf(void)
{
	char buffer[32];

	zassert_true(snprintf(buffer, sizeof(buffer), "%u", 1234) == 4);
	zassert_true(strncmp(buffer, "1234", sizeof(buffer)) == 0);
}

ZTEST(stdlib, test_snprintf)
{
	test_snprintf();
}

__no_optimization static void test_strcspn(void)
{
	const char str1[] = "abc";
	const char str2[] = "This is a string\nwith newlines!";

	zassert_equal(strcspn(str1, "a"), (size_t)0);
	zassert_equal(strcspn(str1, "b"), (size_t)1);
	zassert_equal(strcspn(str1, "c"), (size_t)2);
	zassert_equal(strcspn(str1, "ccc"), (size_t)2);
	zassert_equal(strcspn(str1, "cba"), (size_t)0);
	zassert_equal(strcspn(str1, "cb"), (size_t)1);
	zassert_equal(strcspn(str1, "bc"), (size_t)1);
	zassert_equal(strcspn(str1, "cbc"), (size_t)1);
	zassert_equal(strcspn(str1, "z"), strlen(str1));
	zassert_equal(strcspn(str1, "xyz"), strlen(str1));
	zassert_equal(strcspn(str1, ""), strlen(str1));

	zassert_equal(strcspn(str2, " "), (size_t)4);
	zassert_equal(strcspn(str2, "\n"), (size_t)16);
	zassert_equal(strcspn(str2, "\n "), (size_t)4);
	zassert_equal(strcspn(str2, "!"), strlen(str2) - 1);
	zassert_equal(strcspn(str2, "z"), strlen(str2));
	zassert_equal(strcspn(str2, "z!"), strlen(str2) - 1);
}

ZTEST(stdlib, test_strcspn)
{
	test_strcspn();
}

ZTEST(stdlib, test_memmove)
{
	const int buf_size = 16;
	char buf[buf_size];

	for (int i = 0; i < buf_size; ++i)
		buf[i] = i & 0x7f;

	/* Test small moves */
	memmove(buf + 1, buf, 1);
	zassert_mem_equal(buf + 1, buf, 1);
	memmove(buf + 5, buf, 4);
	memmove(buf + 1, buf, 4);
	zassert_mem_equal(buf + 1, buf + 5, 4);
}

ZTEST(stdlib, test_memmove_overlap)
{
	const int buf_size = 120;
	const int len = 80;
	char buf[buf_size];
	char *buf_dst;
	char cmp_buf[len];

	for (int i = 0; i < len; ++i)
		buf[i] = i & 0x7f;
	for (int i = len; i < buf_size; ++i)
		buf[i] = 0;
	/* Store the original buffer. */
	memcpy(cmp_buf, buf, len);

	buf_dst = buf + (buf_size - len) - 1;
	memmove(buf_dst, buf, len); /* unaligned */
	zassert_mem_equal(buf_dst, cmp_buf, len);

	for (int i = 0; i < len; ++i)
		buf[i] = i & 0x7f;
	for (int i = len; i < buf_size; ++i)
		buf[i] = 0;
	buf_dst = buf + (buf_size - len);
	memmove(buf_dst, buf, len); /* aligned */
	zassert_mem_equal(buf_dst, cmp_buf, len);
}

ZTEST(stdlib, test_memmove_benchmark)
{
	int i;
	char *buf;
	const int buf_size = 1000;
	timestamp_t t0, t1, t2, t3;
	const int iteration = 1000;
	const int len = 400;

	zassert_true(shared_mem_acquire(buf_size, &buf) == EC_SUCCESS);

	for (int i = 0; i < buf_size; ++i)
		buf[i] = i & 0x7f;

	t0 = get_time();
	for (i = 0; i < iteration; ++i)
		memmove(buf + 101, buf, len); /* unaligned */
	t1 = get_time();

	t2 = get_time();
	for (i = 0; i < iteration; ++i)
		memmove(buf + 100, buf, len); /* aligned */
	t3 = get_time();

	shared_mem_release(buf);
	if (!IS_ENABLED(CONFIG_ARCH_POSIX)) {
		ccprintf("Unaligned memmove: %" PRId64 " us\n",
			 t1.val - t0.val);
		ccprintf("Aligned memmove  : %" PRId64 " us\n",
			 t3.val - t2.val);

		/* TODO(b/356094145): If memory overlaps, newlib performs byte
		 * by byte coping. If there is no overlap and memory is aligned,
		 * memmove is ~3x faster than unaligned, but it is just memcpy.
		 */
		if (!IS_ENABLED(CONFIG_NEWLIB_LIBC)) {
			zassert_true((t1.val - t0.val) > (t3.val - t2.val));
		}
	}
}

ZTEST(stdlib, test_memcpy)
{
	int i;
	timestamp_t t0, t1, t2, t3;
	char *buf;
	const int buf_size = 1000;
	const int len = 400;
	const int dest_offset = 500;
	const int iteration = 1000;

	zassert_true(shared_mem_acquire(buf_size, &buf) == EC_SUCCESS);

	for (i = 0; i < len; ++i)
		buf[i] = i & 0x7f;
	for (i = len; i < buf_size; ++i)
		buf[i] = 0;

	t0 = get_time();
	for (i = 0; i < iteration; ++i)
		memcpy(buf + dest_offset + 1, buf, len); /* unaligned */
	t1 = get_time();
	zassert_mem_equal(buf + dest_offset + 1, buf, len);

	t2 = get_time();
	for (i = 0; i < iteration; ++i)
		memcpy(buf + dest_offset, buf, len); /* aligned */
	t3 = get_time();
	zassert_mem_equal(buf + dest_offset, buf, len);

	if (!IS_ENABLED(CONFIG_ARCH_POSIX)) {
		ccprintf("Unaligned memcpy: %" PRId64 " us\n", t1.val - t0.val);
		ccprintf("Aligned memcpy  : %" PRId64 " us\n", t3.val - t2.val);

		zassert_true((t1.val - t0.val) > (t3.val - t2.val));
	}

	memcpy(buf + dest_offset + 1, buf + 1, len - 1);
	zassert_mem_equal(buf + dest_offset + 1, buf + 1, len - 1);

	/* Test small copies */
	memcpy(buf + dest_offset, buf, 1);
	zassert_mem_equal(buf + dest_offset, buf, 1);
	memcpy(buf + dest_offset, buf, 4);
	zassert_mem_equal(buf + dest_offset, buf, 4);
	memcpy(buf + dest_offset + 1, buf, 1);
	zassert_mem_equal(buf + dest_offset + 1, buf, 1);
	memcpy(buf + dest_offset + 1, buf, 4);
	zassert_mem_equal(buf + dest_offset + 1, buf, 4);

	shared_mem_release(buf);
}

/* Plain memset, used as a reference to measure speed gain */
static void *dumb_memset(void *dest, int c, int len)
{
	/* Use volatile to force per byte access. Otherwise this function is
	 * optimized to call the memset function.
	 */
	volatile char *d = (char *)dest;
	while (len > 0) {
		*(d++) = c;
		len--;
	}
	return dest;
}

ZTEST(stdlib, test_memset)
{
	int i;
	timestamp_t t0, t1, t2, t3;
	char *buf;
	const int buf_size = 1000;
	const int len = 400;
	const int iteration = 1000;

	zassert_true(shared_mem_acquire(buf_size, &buf) == EC_SUCCESS);

	t0 = get_time();
	for (i = 0; i < iteration; ++i)
		dumb_memset(buf, 1, len);
	t1 = get_time();
	for (int i = 0; i < len; i++) {
		zassert_equal(buf[i], (char)1);
	}
	ccprintf(" (speed gain: %" PRId64 " ->", t1.val - t0.val);

	t2 = get_time();
	for (i = 0; i < iteration; ++i)
		memset(buf, 1, len);
	t3 = get_time();
	for (int i = 0; i < len; i++) {
		zassert_equal(buf[i], (char)1);
	}
	ccprintf(" %" PRId64 " us) ", t3.val - t2.val);

	if (!IS_ENABLED(CONFIG_ARCH_POSIX)) {
		zassert_true((t1.val - t0.val) > (t3.val - t2.val));
	}

	memset(buf, 128, len);
	for (int i = 0; i < len; i++) {
		zassert_equal(buf[i], (char)128);
	}

	memset(buf, -2, len);
	for (int i = 0; i < len; i++) {
		zassert_equal(buf[i], (char)-2);
	}

	memset(buf + 1, 1, len - 2);
	for (int i = 0; i < len - 2; i++) {
		zassert_equal(buf[i + 1], (char)1);
	}

	shared_mem_release(buf);
}

__no_optimization static void test_memchr(void)
{
	char *buf = "1234";

	zassert_true(memchr("123567890", '4', 8) == NULL);
	zassert_true(memchr("123", '3', 2) == NULL);
	zassert_true(memchr(buf, '3', 4) == buf + 2);
	zassert_true(memchr(buf, '4', 4) == buf + 3);
}

ZTEST(stdlib, test_memchr)
{
	test_memchr();
}
