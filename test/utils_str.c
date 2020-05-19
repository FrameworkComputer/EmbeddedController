/* Copyright 2017 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Test common utilities (string functions).
 */

#include "common.h"
#include "console.h"
#include "system.h"
#include "printf.h"
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

static int test_strstr(void)
{
	const char s1[] = "abcde";

	TEST_ASSERT(strstr(s1, "ab") == s1);
	TEST_ASSERT(strstr(s1, "") == NULL);
	TEST_ASSERT(strstr("", "ab") == NULL);
	TEST_ASSERT(strstr("", "x") == NULL);
	TEST_ASSERT(strstr(s1, "de") == &s1[3]);
	TEST_ASSERT(strstr(s1, "def") == NULL);

	return EC_SUCCESS;
}

static int test_strtoi(void)
{
	char *e;

	TEST_ASSERT(strtoi("10", &e, 0) == 10);
	TEST_ASSERT(e && (*e == '\0'));
	TEST_ASSERT(strtoi("010", &e, 0) == 8);
	TEST_ASSERT(e && (*e == '\0'));
	TEST_ASSERT(strtoi("+010", &e, 0) == 8);
	TEST_ASSERT(e && (*e == '\0'));
	TEST_ASSERT(strtoi("-010", &e, 0) == -8);
	TEST_ASSERT(e && (*e == '\0'));
	TEST_ASSERT(strtoi("0x1f z", &e, 0) == 31);
	TEST_ASSERT(e && (*e == ' '));
	TEST_ASSERT(strtoi("0X1f z", &e, 0) == 31);
	TEST_ASSERT(e && (*e == ' '));
	TEST_ASSERT(strtoi("10a", &e, 16) == 266);
	TEST_ASSERT(e && (*e == '\0'));
	TEST_ASSERT(strtoi("0x02C", &e, 16) == 44);
	TEST_ASSERT(e && (*e == '\0'));
	TEST_ASSERT(strtoi("+0x02C", &e, 16) == 44);
	TEST_ASSERT(e && (*e == '\0'));
	TEST_ASSERT(strtoi("-0x02C", &e, 16) == -44);
	TEST_ASSERT(e && (*e == '\0'));
	TEST_ASSERT(strtoi("0x02C", &e, 0) == 44);
	TEST_ASSERT(e && (*e == '\0'));
	TEST_ASSERT(strtoi("+0x02C", &e, 0) == 44);
	TEST_ASSERT(e && (*e == '\0'));
	TEST_ASSERT(strtoi("-0x02C", &e, 0) == -44);
	TEST_ASSERT(e && (*e == '\0'));
	TEST_ASSERT(strtoi("0X02C", &e, 16) == 44);
	TEST_ASSERT(e && (*e == '\0'));
	TEST_ASSERT(strtoi("+0X02C", &e, 16) == 44);
	TEST_ASSERT(e && (*e == '\0'));
	TEST_ASSERT(strtoi("-0X02C", &e, 16) == -44);
	TEST_ASSERT(e && (*e == '\0'));
	TEST_ASSERT(strtoi("0X02C", &e, 0) == 44);
	TEST_ASSERT(e && (*e == '\0'));
	TEST_ASSERT(strtoi("+0X02C", &e, 0) == 44);
	TEST_ASSERT(e && (*e == '\0'));
	TEST_ASSERT(strtoi("-0X02C", &e, 0) == -44);
	TEST_ASSERT(e && (*e == '\0'));
	TEST_ASSERT(strtoi("   -12", &e, 0) == -12);
	TEST_ASSERT(e && (*e == '\0'));
	TEST_ASSERT(strtoi("!", &e, 0) == 0);
	TEST_ASSERT(e && (*e == '!'));
	TEST_ASSERT(strtoi("+!", &e, 0) == 0);
	TEST_ASSERT(e && (*e == '!'));
	TEST_ASSERT(strtoi("+0!", &e, 0) == 0);
	TEST_ASSERT(e && (*e == '!'));
	TEST_ASSERT(strtoi("+0x!", &e, 0) == 0);
	TEST_ASSERT(e && (*e == '!'));
	TEST_ASSERT(strtoi("+0X!", &e, 0) == 0);
	TEST_ASSERT(e && (*e == '!'));

	return EC_SUCCESS;
}

static int test_strtoul(void)
{
	char *e;

	TEST_ASSERT(strtoul("10", &e, 0) == 10);
	TEST_ASSERT(e && (*e == '\0'));
	TEST_ASSERT(strtoul("010", &e, 0) == 8);
	TEST_ASSERT(e && (*e == '\0'));
	TEST_ASSERT(strtoul("+010", &e, 0) == 8);
	TEST_ASSERT(e && (*e == '\0'));
	TEST_ASSERT(strtoul("-010", &e, 0) == 0);
	TEST_ASSERT(e && (*e == '-'));
	TEST_ASSERT(strtoul("0x1f z", &e, 0) == 31);
	TEST_ASSERT(e && (*e == ' '));
	TEST_ASSERT(strtoul("0X1f z", &e, 0) == 31);
	TEST_ASSERT(e && (*e == ' '));
	TEST_ASSERT(strtoul("10a", &e, 16) == 266);
	TEST_ASSERT(e && (*e == '\0'));
	TEST_ASSERT(strtoul("0x02C", &e, 16) == 44);
	TEST_ASSERT(e && (*e == '\0'));
	TEST_ASSERT(strtoul("+0x02C", &e, 16) == 44);
	TEST_ASSERT(e && (*e == '\0'));
	TEST_ASSERT(strtoul("-0x02C", &e, 16) == 0);
	TEST_ASSERT(e && (*e == '-'));
	TEST_ASSERT(strtoul("0x02C", &e, 0) == 44);
	TEST_ASSERT(e && (*e == '\0'));
	TEST_ASSERT(strtoul("+0x02C", &e, 0) == 44);
	TEST_ASSERT(e && (*e == '\0'));
	TEST_ASSERT(strtoul("-0x02C", &e, 0) == 0);
	TEST_ASSERT(e && (*e == '-'));
	TEST_ASSERT(strtoul("0X02C", &e, 16) == 44);
	TEST_ASSERT(e && (*e == '\0'));
	TEST_ASSERT(strtoul("+0X02C", &e, 16) == 44);
	TEST_ASSERT(e && (*e == '\0'));
	TEST_ASSERT(strtoul("-0X02C", &e, 16) == 0);
	TEST_ASSERT(e && (*e == '-'));
	TEST_ASSERT(strtoul("0X02C", &e, 0) == 44);
	TEST_ASSERT(e && (*e == '\0'));
	TEST_ASSERT(strtoul("+0X02C", &e, 0) == 44);
	TEST_ASSERT(e && (*e == '\0'));
	TEST_ASSERT(strtoul("-0X02C", &e, 0) == 0);
	TEST_ASSERT(e && (*e == '-'));
	TEST_ASSERT(strtoul("   -12", &e, 0) == 0);
	TEST_ASSERT(e && (*e == '-'));
	TEST_ASSERT(strtoul("!", &e, 0) == 0);
	TEST_ASSERT(e && (*e == '!'));
	TEST_ASSERT(strtoul("+!", &e, 0) == 0);
	TEST_ASSERT(e && (*e == '!'));
	TEST_ASSERT(strtoul("+0!", &e, 0) == 0);
	TEST_ASSERT(e && (*e == '!'));
	TEST_ASSERT(strtoul("+0x!", &e, 0) == 0);
	TEST_ASSERT(e && (*e == '!'));
	TEST_ASSERT(strtoul("+0X!", &e, 0) == 0);
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

static int test_strncpy(void)
{
	char dest[10];

	strncpy(dest, "test", 10);
	TEST_ASSERT_ARRAY_EQ("test", dest, 5);
	strncpy(dest, "12345", 6);
	TEST_ASSERT_ARRAY_EQ("12345", dest, 6);
	strncpy(dest, "testtesttest", 10);
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

static int test_strlen(void)
{
	TEST_CHECK(strlen("this is a string") == 16);
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

static int test_snprintf(void)
{
	char buffer[32];

	TEST_CHECK(snprintf(buffer, sizeof(buffer), "%u", 1234) == 4);
	TEST_CHECK(strncmp(buffer, "1234", sizeof(buffer)));
}

void run_test(int argc, char **argv)
{
	test_reset();

	RUN_TEST(test_isalpha);
	RUN_TEST(test_isprint);
	RUN_TEST(test_strstr);
	RUN_TEST(test_strtoi);
	RUN_TEST(test_strtoul);
	RUN_TEST(test_parse_bool);
	RUN_TEST(test_strzcpy);
	RUN_TEST(test_strncpy);
	RUN_TEST(test_strncmp);
	RUN_TEST(test_strlen);
	RUN_TEST(test_strnlen);
	RUN_TEST(test_strcasecmp);
	RUN_TEST(test_strncasecmp);
	RUN_TEST(test_atoi);
	RUN_TEST(test_snprintf);

	test_print_result();
}
