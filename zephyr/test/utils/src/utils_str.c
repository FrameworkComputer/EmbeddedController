/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "util.h"

#include <zephyr/ztest.h>

ZTEST_SUITE(utils_str, NULL, NULL, NULL, NULL, NULL);

ZTEST(utils_str, test_strtoi)
{
	char *e;

	zassert_true(strtoi("10", &e, 0) == 10);
	zassert_true(e && (*e == '\0'));
	zassert_true(strtoi("010", &e, 0) == 8);
	zassert_true(e && (*e == '\0'));
	zassert_true(strtoi("+010", &e, 0) == 8);
	zassert_true(e && (*e == '\0'));
	zassert_true(strtoi("-010", &e, 0) == -8);
	zassert_true(e && (*e == '\0'));
	zassert_true(strtoi("0x1f z", &e, 0) == 31);
	zassert_true(e && (*e == ' '));
	zassert_true(strtoi("0X1f z", &e, 0) == 31);
	zassert_true(e && (*e == ' '));
	zassert_true(strtoi("10a", &e, 16) == 266);
	zassert_true(e && (*e == '\0'));
	zassert_true(strtoi("0x02C", &e, 16) == 44);
	zassert_true(e && (*e == '\0'));
	zassert_true(strtoi("+0x02C", &e, 16) == 44);
	zassert_true(e && (*e == '\0'));
	zassert_true(strtoi("-0x02C", &e, 16) == -44);
	zassert_true(e && (*e == '\0'));
	zassert_true(strtoi("0x02C", &e, 0) == 44);
	zassert_true(e && (*e == '\0'));
	zassert_true(strtoi("+0x02C", &e, 0) == 44);
	zassert_true(e && (*e == '\0'));
	zassert_true(strtoi("-0x02C", &e, 0) == -44);
	zassert_true(e && (*e == '\0'));
	zassert_true(strtoi("0X02C", &e, 16) == 44);
	zassert_true(e && (*e == '\0'));
	zassert_true(strtoi("+0X02C", &e, 16) == 44);
	zassert_true(e && (*e == '\0'));
	zassert_true(strtoi("-0X02C", &e, 16) == -44);
	zassert_true(e && (*e == '\0'));
	zassert_true(strtoi("0X02C", &e, 0) == 44);
	zassert_true(e && (*e == '\0'));
	zassert_true(strtoi("+0X02C", &e, 0) == 44);
	zassert_true(e && (*e == '\0'));
	zassert_true(strtoi("-0X02C", &e, 0) == -44);
	zassert_true(e && (*e == '\0'));
	zassert_true(strtoi("   -12", &e, 0) == -12);
	zassert_true(e && (*e == '\0'));
	zassert_true(strtoi("!", &e, 0) == 0);
	zassert_true(e && (*e == '!'));
	zassert_true(strtoi("+!", &e, 0) == 0);
	zassert_true(e && (*e == '!'));
	zassert_true(strtoi("+0!", &e, 0) == 0);
	zassert_true(e && (*e == '!'));
	zassert_true(strtoi("+0x!", &e, 0) == 0);
	zassert_true(e && (*e == '!'));
	zassert_true(strtoi("+0X!", &e, 0) == 0);
	zassert_true(e && (*e == '!'));
}

ZTEST(utils_str, test_parse_bool)
{
	int v;

	zassert_true(parse_bool("on", &v) == 1);
	zassert_true(v == 1);
	zassert_true(parse_bool("off", &v) == 1);
	zassert_true(v == 0);
	zassert_true(parse_bool("enable", &v) == 1);
	zassert_true(v == 1);
	zassert_true(parse_bool("disable", &v) == 1);
	zassert_true(v == 0);
	zassert_true(parse_bool("di", &v) == 0);
	zassert_true(parse_bool("en", &v) == 0);
	zassert_true(parse_bool("of", &v) == 0);
}

ZTEST(utils_str, test_strzcpy)
{
	char dest[10];

	strzcpy(dest, "test", 10);
	zassert_mem_equal("test", dest, 5);
	strzcpy(dest, "testtesttest", 10);
	zassert_mem_equal("testtestt", dest, 10);
	strzcpy(dest, "aaaa", -1);
	zassert_mem_equal("testtestt", dest, 10);
}
