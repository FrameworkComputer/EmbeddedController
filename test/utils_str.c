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

void run_test(int argc, const char **argv)
{
	test_reset();

	RUN_TEST(test_strtoi);
	RUN_TEST(test_parse_bool);
	RUN_TEST(test_strzcpy);

	test_print_result();
}
