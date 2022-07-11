/* Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>

#include "common.h"
#include "printf.h"
#include "test_util.h"
#include "util.h"

#define INIT_VALUE 0x5E
#define NO_BYTES_TOUCHED NULL

static const char err_str[] = "ERROR";
static char output[1024];

int run(int expect_ret, const char *expect, bool output_null, size_t size_limit,
	const char *format, va_list args)
{
	size_t expect_size = expect ? strlen(expect) + 1 : 0;
	int rv;

	ccprintf("\n");
	ccprintf("size_limit=%-4zd | format='%s'\n", size_limit, format);
	ccprintf("expect  ='%s'   | expect_status=%d\n",
		 expect ? expect : "NO_BYTES_TOUCHED", expect_ret);

	TEST_ASSERT(expect_size <= sizeof(output));
	TEST_ASSERT(expect_size <= size_limit);
	memset(output, INIT_VALUE, sizeof(output));

	rv = vsnprintf(output_null ? NULL : output, size_limit, format, args);
	ccprintf("received='%.*s'   | ret          =%d\n", 30, output, rv);

	TEST_ASSERT_ARRAY_EQ(output, expect, expect_size);
	TEST_ASSERT_MEMSET(&output[expect_size], INIT_VALUE,
			   sizeof(output) - expect_size);

	if (rv >= 0) {
		TEST_ASSERT(rv == expect_size - 1);
		TEST_ASSERT(EC_SUCCESS == expect_ret);
	} else {
		TEST_ASSERT(rv == -expect_ret);
	}

	return EC_SUCCESS;
}

int expect_success(const char *expect, const char *format, ...)
{
	va_list args;
	int rv;

	va_start(args, format);
	rv = run(EC_SUCCESS, expect, false, sizeof(output), format, args);
	va_end(args);

	return rv;
}

int expect(int expect_ret, const char *expect, bool output_null,
	   size_t size_limit, const char *format, ...)
{
	va_list args;
	int rv;

	va_start(args, format);
	rv = run(expect_ret, expect, output_null, size_limit, format, args);
	va_end(args);

	return rv;
}

#define T(n)                          \
	do {                          \
		int rv = (n);         \
		if (rv != EC_SUCCESS) \
			return rv;    \
	} while (0)

test_static int test_vsnprintf_args(void)
{
	T(expect_success("", ""));
	T(expect_success("a", "a"));

	T(expect(/* expect an invalid args error */
		 EC_ERROR_INVAL, NO_BYTES_TOUCHED,
		 /* given 0 as output size limit */
		 false, 0, ""));
	T(expect(/* expect SUCCESS */
		 EC_SUCCESS, "",
		 /* given 1 as output size limit and a blank format */
		 false, 1, ""));
	T(expect(/* expect an overflow error */
		 EC_ERROR_OVERFLOW, "",
		 /* given 1 as output size limit with a non-blank format */
		 false, 1, "a"));

	T(expect(/* expect an invalid args error */
		 EC_ERROR_INVAL, NO_BYTES_TOUCHED,
		 /* given NULL as the output buffer */
		 true, sizeof(output), ""));
	T(expect(/* expect an invalid args error */
		 EC_ERROR_INVAL, NO_BYTES_TOUCHED,
		 /* given a NULL format string */
		 false, sizeof(output), NULL));

	return EC_SUCCESS;
}

test_static int test_vsnprintf_int(void)
{
	T(expect_success("123", "%d", 123));
	T(expect_success("-123", "%d", -123));
	T(expect_success("+123", "%+d", 123));
	T(expect_success("-123", "%+d", -123));
	T(expect_success("123", "%-d", 123));
	T(expect_success("-123", "%-d", -123));

	T(expect_success("  123", "%5d", 123));
	T(expect_success(" +123", "%+5d", 123));
	T(expect_success("00123", "%05d", 123));
	T(expect_success("00123", "%005d", 123));
	/*
	 * TODO(crbug.com/974084): This odd behavior should be fixed.
	 * T(expect_success("+0123",     "%+05d",   123));
	 * Actual: "0+123"
	 * T(expect_success("+0123",     "%+005d",  123));
	 * Actual: "0+123"
	 */

	T(expect_success("  123", "%*d", 5, 123));
	T(expect_success(" +123", "%+*d", 5, 123));
	T(expect_success("00123", "%0*d", 5, 123));
	/*
	 * TODO(crbug.com/974084): This odd behavior should be fixed.
	 * T(expect_success("00123",     "%00*d",   5, 123));
	 * Actual: "ERROR"
	 */
	T(expect_success("0+123", "%+0*d", 5, 123));
	/*
	 * TODO(crbug.com/974084): This odd behavior should be fixed.
	 * T(expect_success("0+123",     "%+00*d",  5, 123));
	 * Actual: "ERROR"
	 */

	T(expect_success("123  ", "%-5d", 123));
	T(expect_success("+123 ", "%-+5d", 123));
	T(expect_success(err_str, "%+-5d", 123));
	T(expect_success("123  ", "%-05d", 123));
	T(expect_success("123  ", "%-005d", 123));
	T(expect_success("+123 ", "%-+05d", 123));
	T(expect_success("+123 ", "%-+005d", 123));

	T(expect_success("0.00123", "%.5d", 123));
	T(expect_success("+0.00123", "%+.5d", 123));
	T(expect_success("0.00123", "%7.5d", 123));
	T(expect_success("  0.00123", "%9.5d", 123));
	T(expect_success(" +0.00123", "%+9.5d", 123));

	T(expect_success("123", "%u", 123));
	T(expect_success("4294967295", "%u", -1));
	T(expect_success("18446744073709551615", "%llu", (uint64_t)-1));

	T(expect_success("0", "%x", 0));
	T(expect_success("0", "%X", 0));
	T(expect_success("5e", "%x", 0X5E));
	T(expect_success("5E", "%X", 0X5E));

	return EC_SUCCESS;
}

test_static int test_printf_long32_enabled(void)
{
	bool use_l32 = IS_ENABLED(CONFIG_PRINTF_LONG_IS_32BITS);

	if (IS_ENABLED(BOARD_BLOONCHIPPER) || IS_ENABLED(BOARD_DARTMONKEY))
		TEST_ASSERT(use_l32);
	else
		TEST_ASSERT(!use_l32);
	return EC_SUCCESS;
}

test_static int test_vsnprintf_32bit_long_supported(void)
{
	long long_min = INT32_MIN;
	long long_max = INT32_MAX;
	unsigned long ulong_max = UINT32_MAX;
	char const *long_min_str = "-2147483648";
	char const *long_max_str = "2147483647";
	char const *ulong_max_str = "4294967295";
	char const *long_min_hexstr = "80000000";
	char const *long_max_hexstr = "7fffffff";
	char const *ulong_max_hexstr = "ffffffff";

	T(expect_success(long_min_str, "%ld", long_min));
	T(expect_success(long_min_hexstr, "%lx", long_min));
	T(expect_success(long_max_str, "%ld", long_max));
	T(expect_success(long_max_hexstr, "%lx", long_max));
	T(expect_success(ulong_max_str, "%lu", ulong_max));
	T(expect_success(ulong_max_hexstr, "%lx", ulong_max));
	T(expect_success(long_max_str, "%ld", long_max));

	T(expect_success(" +123", "%+*ld", 5, 123));
	T(expect_success("00000123", "%08lu", 123));
	T(expect_success("131415", "%d%lu%d", 13, 14L, 15));

	/*
	 * %i and %li are only supported via the CONFIG_PRINTF_LONG_IS_32BITS
	 * configuration (see https://issuetracker.google.com/issues/172210614).
	 */
	T(expect_success("123", "%i", 123));
	T(expect_success("123", "%li", 123));

	return EC_SUCCESS;
}

test_static int test_vsnprintf_64bit_long_supported(void)
{
	/* These lines are only executed when sizeof(long) is 64-bits but are
	 * still compiled by systems with 32-bit longs, so the casts are needed
	 * to avoid compilation errors.
	 */
	long long_min = (long)INT64_MIN;
	long long_max = (long)INT64_MAX;
	unsigned long ulong_max = (unsigned long)UINT64_MAX;
	char const *long_min_str = "-9223372036854775808";
	char const *long_max_str = "9223372036854775807";
	char const *ulong_max_str = "18446744073709551615";
	char const *long_min_hexstr = "8000000000000000";
	char const *long_max_hexstr = "7fffffffffffffff";
	char const *ulong_max_hexstr = "ffffffffffffffff";

	T(expect_success(long_min_str, "%ld", long_min));
	T(expect_success(long_min_hexstr, "%lx", long_min));
	T(expect_success(long_max_str, "%ld", long_max));
	T(expect_success(long_max_hexstr, "%lx", long_max));
	T(expect_success(ulong_max_str, "%lu", ulong_max));
	T(expect_success(ulong_max_hexstr, "%lx", ulong_max));
	T(expect_success(long_max_str, "%ld", long_max));

	T(expect_success(" +123", "%+*ld", 5, 123));
	T(expect_success("00000123", "%08lu", 123));
	T(expect_success("131415", "%d%lu%d", 13, 14L, 15));

	T(expect_success(err_str, "%i", 123));
	T(expect_success(err_str, "%li", 123));

	return EC_SUCCESS;
}

test_static int test_vsnprintf_long_not_supported(void)
{
	T(expect_success(err_str, "%ld", 0x7b));
	T(expect_success(err_str, "%li", 0x7b));
	T(expect_success(err_str, "%lu", 0x7b));
	T(expect_success(err_str, "%lx", 0x7b));
	T(expect_success(err_str, "%08lu", 123));
	T(expect_success("13ERROR", "%d%lu%d", 13, 14L, 15));

	T(expect_success(err_str, "%i", 123));
	T(expect_success(err_str, "%li", 123));

	return EC_SUCCESS;
}

test_static int test_vsnprintf_long(void)
{
	/*
	 * %l is functional on 64-bit systems but is not supported on 32-bit
	 * systems (see https://issuetracker.google.com/issues/172210614) unless
	 * explicitly enabled via configuration.
	 */
	if (IS_ENABLED(CONFIG_PRINTF_LONG_IS_32BITS))
		return test_vsnprintf_32bit_long_supported();
	else if (sizeof(long) == sizeof(uint64_t))
		return test_vsnprintf_64bit_long_supported();
	else
		return test_vsnprintf_long_not_supported();
}

test_static int test_vsnprintf_pointers(void)
{
	void *ptr = (void *)0x55005E00;
	unsigned int val = 0;

	T(expect_success("55005e00", "%pP", ptr));
	T(expect_success(err_str, "%P", ptr));
	/* %p by itself is invalid */
	T(expect(EC_ERROR_INVAL, "", false, sizeof(output), "%p"));
	/* %p with an unknown suffix is invalid */
	T(expect(EC_ERROR_INVAL, "", false, sizeof(output), "%p "));
	/* %p with an unknown suffix is invalid */
	T(expect(EC_ERROR_INVAL, "", false, sizeof(output), "%pQ"));

	/* Test %pb, binary format */
	T(expect_success("0", "%pb", BINARY_VALUE(val, 0)));
	val = 0x5E;
	T(expect_success("1011110", "%pb", BINARY_VALUE(val, 0)));
	T(expect_success("0000000001011110", "%pb", BINARY_VALUE(val, 16)));
	val = 0x12345678;
	T(expect_success("10010001101000101011001111000", "%pb",
			 BINARY_VALUE(val, 0)));
	val = 0xFEDCBA90;
	/* Test a number that makes the longest string possible */
	T(expect_success("11111110110111001011101010010000", "%pb",
			 BINARY_VALUE(val, 0)));
	return EC_SUCCESS;
}

test_static int test_vsnprintf_chars(void)
{
	T(expect_success("a", "%c", 'a'));
	T(expect_success("*", "%c", '*'));
	return EC_SUCCESS;
}

test_static int test_vsnprintf_strings(void)
{
	T(expect_success("abc", "%s", "abc"));
	T(expect_success("  abc", "%5s", "abc"));
	T(expect_success("abc", "%0s", "abc"));
	T(expect_success("abc  ", "%-5s", "abc"));
	T(expect_success("abc", "%*s", 0, "abc"));
	T(expect_success("a", "%.1s", "abc"));
	T(expect_success("a", "%.*s", 1, "abc"));
	T(expect_success("", "%.0s", "abc"));
	T(expect_success("", "%.*s", 0, "abc"));
	/*
	 * TODO(crbug.com/974084):
	 * Ignoring the padding parameter is slightly
	 * odd behavior and could use a review.
	 */
	T(expect_success("ab", "%5.2s", "abc"));
	T(expect_success("abc", "%.4s", "abc"));

	/*
	 * Given a malformed string (address 0x1 is a good example),
	 * if we ask for zero precision, expect no bytes to be read
	 * from the malformed address and a blank output string.
	 */
	T(expect_success("", "%.0s", (char *)1));

	return EC_SUCCESS;
}

test_static int test_vsnprintf_timestamps(void)
{
	uint64_t ts = 0;

	T(expect_success("0.000000", "%pT", &ts));
	ts = 123456;
	T(expect_success("0.123456", "%pT", &ts));
	ts = 9999999000000;
	T(expect_success("9999999.000000", "%pT", &ts));
	return EC_SUCCESS;
}

test_static int test_vsnprintf_hexdump(void)
{
	const char bytes[] = { 0x00, 0x5E };

	T(expect_success("005e", "%ph", HEX_BUF(bytes, 2)));
	T(expect_success("", "%ph", HEX_BUF(bytes, 0)));
	T(expect_success("00", "%ph", HEX_BUF(bytes, 1)));
	return EC_SUCCESS;
}

test_static int test_vsnprintf_combined(void)
{
	T(expect_success("abc", "%c%s", 'a', "bc"));
	T(expect_success("12\tbc", "%d\t%s", 12, "bc"));
	return EC_SUCCESS;
}

void run_test(int argc, char **argv)
{
	test_reset();

	RUN_TEST(test_vsnprintf_args);
	RUN_TEST(test_vsnprintf_int);
	RUN_TEST(test_printf_long32_enabled);
	RUN_TEST(test_vsnprintf_long);
	RUN_TEST(test_vsnprintf_pointers);
	RUN_TEST(test_vsnprintf_chars);
	RUN_TEST(test_vsnprintf_strings);
	RUN_TEST(test_vsnprintf_timestamps);
	RUN_TEST(test_vsnprintf_hexdump);
	RUN_TEST(test_vsnprintf_combined);
	test_print_result();
}
