/* Copyright 2019 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "compiler.h"
#include "printf.h"
#include "test_util.h"
#include "util.h"

#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>

#ifndef USE_BUILTIN_STDLIB
/* This is ugly, but we want to test the functions in builtin/stdlib.c while
 * still depending on the system stdlib.c
 */
#define snprintf hidden_crec_snprintf
#define vsnprintf hidden_crec_vsnprintf
#include "../builtin/stdlib.c"
#undef snprintf
#undef vsnprintf
#endif

#define VSNPRINTF crec_vsnprintf
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

	rv = VSNPRINTF(output_null ? NULL : output, size_limit, format, args);
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

int expect_success_crec(const char *expect, const char *format, ...)
{
	va_list args;
	int rv;

	va_start(args, format);
	rv = run(EC_SUCCESS, expect, false, sizeof(output), format, args);
	va_end(args);

	return rv;
}

int expect_success_std(const char *format, ...)
{
	char expect_std[1024];
	va_list args;
	int rv;

	va_start(args, format);
	vsnprintf(expect_std, sizeof(expect_std), format, args);
	va_end(args);

	va_start(args, format);
	rv = run(EC_SUCCESS, expect_std, false, sizeof(output), format, args);
	va_end(args);

	return rv;
}

int expect_success(const char *expect, const char *format, ...)
{
	char expect_std[1024];
	va_list args;
	int rv;

	va_start(args, format);
	rv = run(EC_SUCCESS, expect, false, sizeof(output), format, args);
	va_end(args);

	/*
	 * Verify expected result is consistent with the standard libc
	 * result.
	 */
	va_start(args, format);
	vsnprintf(expect_std, sizeof(expect_std), format, args);
	va_end(args);

	ccprintf("standard='%.*s'\n", 30, expect_std);
	TEST_ASSERT(strcmp(expect_std, expect) == 0);

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

	/*
	 * TODO(b/239233116): This differs from the C standard library
	 * behavior and should probably be changed.
	 */
	T(expect(/* expect an invalid args error */
		 EC_ERROR_INVAL, NO_BYTES_TOUCHED,
		 /* given 0 as output size limit */
		 false, 0, ""));
	T(expect(/* expect an overflow error */
		 EC_ERROR_OVERFLOW, "",
		 /* given 1 as output size limit with a non-blank format
		  */
		 false, 1, "a"));
	T(expect(/* expect an invalid args error */
		 EC_ERROR_INVAL, NO_BYTES_TOUCHED,
		 /* given NULL as the output buffer */
		 true, sizeof(output), ""));
	T(expect(/* expect an invalid args error */
		 EC_ERROR_INVAL, NO_BYTES_TOUCHED,
		 /* given a NULL format string */
		 false, sizeof(output), NULL));
	T(expect(/* expect SUCCESS */
		 EC_SUCCESS, "",
		 /* given 1 as output size limit and a blank format */
		 false, 1, ""));

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

	/* Precision or width larger than buffer should fail. */
	T(expect(EC_ERROR_OVERFLOW, "  1", false, 4, "%5d", 123));
	T(expect(EC_ERROR_OVERFLOW, "   ", false, 4, "%10d", 123));
	T(expect(EC_ERROR_OVERFLOW, "123", false, 4, "%-10d", 123));
	T(expect(EC_ERROR_OVERFLOW, "0.0", false, 4, "%.10d", 123));

	/*
	 * TODO(b/239233116): These are incorrect and should be fixed.
	 */
	T(expect_success_crec("0+123", "%+05d", 123));
	T(expect_success_crec("0+123", "%+005d", 123));

	T(expect_success("  123", "%*d", 5, 123));
	T(expect_success(" +123", "%+*d", 5, 123));
	T(expect_success("00123", "%0*d", 5, 123));
	T(expect_success("00123", "%00*d", 5, 123));

	/*
	 * TODO(b/239233116): This is incorrect and should be fixed.
	 */
	T(expect_success_crec("0+123", "%+0*d", 5, 123));

	/*
	 * TODO(b/239233116): This is incorrect and should be fixed.
	 */
	T(expect_success_crec("0+123", "%+00*d", 5, 123));

	T(expect_success("123  ", "%-5d", 123));
	T(expect_success("+123 ", "%-+5d", 123));
	T(expect_success("+123 ", "%+-5d", 123));
	T(expect_success("123  ", "%-05d", 123));
	T(expect_success("123  ", "%-005d", 123));
	T(expect_success("+123 ", "%-+05d", 123));
	T(expect_success("+123 ", "%-+005d", 123));

	T(expect_success("123", "%u", 123));
	T(expect_success("4294967295", "%u", -1));
	T(expect_success("18446744073709551615", "%llu", (uint64_t)-1));

	T(expect_success("0", "%x", 0));
	T(expect_success("0", "%X", 0));
	T(expect_success("5e", "%x", 0X5E));
	T(expect_success("5E", "%X", 0X5E));

	return EC_SUCCESS;
}

test_static int test_vsnprintf_fixed_point(void)
{
	/*
	 * Fixed point formatting is a cros-ec
	 * deviation from the standard.
	 */
	T(expect_success_crec("0.00123", "%.5d", 123));
	T(expect_success_crec("12.3", "%2.1d", 123));
	T(expect_success_crec("0.00123", "%.5d", 123));
	T(expect_success_crec("+0.00123", "%+.5d", 123));
	T(expect_success_crec("0.00123", "%7.5d", 123));
	T(expect_success_crec("  0.00123", "%9.5d", 123));
	T(expect_success_crec(" +0.00123", "%+9.5d", 123));

	return EC_SUCCESS;
}

test_static int test_printf_long32_enabled(void)
{
	bool use_l32 = IS_ENABLED(CONFIG_PRINTF_LONG_IS_32BITS);

	if (IS_ENABLED(BOARD_BLOONCHIPPER) || IS_ENABLED(BOARD_DARTMONKEY) ||
	    IS_ENABLED(BASEBOARD_HELIPILOT))
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
	T(expect_success_crec("123", "%i", 123));
	T(expect_success_crec("123", "%li", 123));

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

	/*
	 * TODO(b/239233116): These are incorrect and should be fixed.
	 */
	T(expect_success_crec(err_str, "%i", 123));
	T(expect_success_crec(err_str, "%li", 123));

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

	T(expect_success_crec(err_str, "%i", 123));
	T(expect_success_crec(err_str, "%li", 123));

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

	/*
	 * TODO(b/239233116): This incorrect and should be fixed.
	 */
	T(expect_success_crec("55005e00", "%p", ptr));

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
	char fmt[100];

	T(expect_success("abc", "%s", "abc"));
	T(expect_success("  abc", "%5s", "abc"));
	T(expect_success("abc", "%0s", "abc"));
	T(expect_success("abc  ", "%-5s", "abc"));
	T(expect_success("abc", "%*s", 0, "abc"));
	T(expect_success("a", "%.1s", "abc"));
	T(expect_success("a", "%.*s", 1, "abc"));
	T(expect_success("", "%.0s", "abc"));
	T(expect_success("", "%.*s", 0, "abc"));
	T(expect_success("abc", "%.4s", "abc"));

	/*
	 * Exhaustively test (width, precision) cases that
	 * are shorter and longer than given string.
	 */
	for (int width = 0; width < 6; ++width) {
		for (int prec = 0; prec < 6; ++prec) {
			snprintf(fmt, sizeof(fmt), "%%%d.%ds", width, prec);
			T(expect_success_std(fmt, "abc"));
		}
	}

	/*
	 * Given a malformed string (address 0x1 is a good example),
	 * if we ask for zero precision, expect no bytes to be read
	 * from the malformed address and a blank output string.
	 *
	 * Note: This is not a valid test using the standard libc
	 * printf as that will trigger the address sanitizer.
	 */
	T(expect_success_crec("", "%.0s", (char *)1));

	return EC_SUCCESS;
}

test_static int test_snprintf_timestamp(void)
{
	char str[PRINTF_TIMESTAMP_BUF_SIZE];
	int size;
	int ret;
	uint64_t ts = 0;

	/* Success cases. */

	ret = snprintf_timestamp(str, sizeof(str), ts);
	TEST_EQ(ret, 8, "%d");
	TEST_ASSERT_ARRAY_EQ(str, "0.000000", sizeof("0.000000"));

	ts = 123456;
	ret = snprintf_timestamp(str, sizeof(str), ts);
	TEST_EQ(ret, 8, "%d");
	TEST_ASSERT_ARRAY_EQ(str, "0.123456", sizeof("0.123456"));

	ts = 9999999000000;
	ret = snprintf_timestamp(str, sizeof(str), ts);
	TEST_EQ(ret, 14, "%d");
	TEST_ASSERT_ARRAY_EQ(str, "9999999.000000", sizeof("9999999.000000"));

	ts = UINT64_MAX;
	ret = snprintf_timestamp(str, sizeof(str), ts);
	TEST_EQ(ret, 21, "%d");
	TEST_ASSERT_ARRAY_EQ(str, "18446744073709.551615",
			     sizeof("18446744073709.551615"));

	/* Error cases. */

	/* Buffer is too small by one. */
	size = 21;
	ts = UINT64_MAX;
	str[0] = 'f';
	ret = snprintf_timestamp(str, size, ts);
	TEST_EQ(ret, -EC_ERROR_OVERFLOW, "%d");
	TEST_EQ(str[0], '\0', "%d");

	/* Size is zero. */
	size = 0;
	ts = UINT64_MAX;
	str[0] = 'f';
	ret = snprintf_timestamp(str, size, ts);
	TEST_EQ(ret, -EC_ERROR_INVAL, "%d");
	TEST_EQ(str[0], 'f', "%d");

	/* Size is one. */
	size = 1;
	ts = UINT64_MAX;
	str[0] = 'f';
	ret = snprintf_timestamp(str, size, ts);
	TEST_EQ(ret, -EC_ERROR_OVERFLOW, "%d");
	TEST_EQ(str[0], '\0', "%d");

	return EC_SUCCESS;
}

test_static int test_snprintf_hex_buffer(void)
{
	const uint8_t bytes[] = { 0xAB, 0x5E };
	char str_buf[5];
	int rv;

	/* Success cases. */

	memset(str_buf, 0xff, sizeof(str_buf));
	rv = snprintf_hex_buffer(str_buf, sizeof(str_buf), HEX_BUF(bytes, 2));
	TEST_ASSERT_ARRAY_EQ(str_buf, "ab5e", sizeof("ab5e"));
	TEST_EQ(rv, 4, "%d");

	memset(str_buf, 0xff, sizeof(str_buf));
	rv = snprintf_hex_buffer(str_buf, sizeof(str_buf), HEX_BUF(bytes, 0));
	TEST_ASSERT_ARRAY_EQ(str_buf, "", sizeof(""));
	TEST_EQ(rv, 0, "%d");

	memset(str_buf, 0xff, sizeof(str_buf));
	rv = snprintf_hex_buffer(str_buf, sizeof(str_buf), HEX_BUF(bytes, 1));
	TEST_ASSERT_ARRAY_EQ(str_buf, "ab", sizeof("ab"));
	TEST_EQ(rv, 2, "%d");

	/* Error cases. */

	/* Zero for buffer size argument is an error. */
	memset(str_buf, 0xff, sizeof(str_buf));
	TEST_ASSERT_MEMSET(str_buf, (char)0xff, sizeof(str_buf));
	rv = snprintf_hex_buffer(str_buf, 0, HEX_BUF(bytes, 2));
	TEST_EQ(rv, -EC_ERROR_INVAL, "%d");
	TEST_ASSERT_MEMSET(str_buf, (char)0xff, sizeof(str_buf));

	/* Buffer only has space for terminating '\0'. */
	memset(str_buf, 0xff, sizeof(str_buf));
	TEST_ASSERT_MEMSET(str_buf, (char)0xff, sizeof(str_buf));
	rv = snprintf_hex_buffer(str_buf, 1, HEX_BUF(bytes, 1));
	TEST_ASSERT_ARRAY_EQ(str_buf, "", sizeof(""));
	TEST_EQ(rv, -EC_ERROR_OVERFLOW, "%d");

	/* Buffer only has space for one character and '\0'. */
	memset(str_buf, 0xff, sizeof(str_buf));
	TEST_ASSERT_MEMSET(str_buf, (char)0xff, sizeof(str_buf));
	rv = snprintf_hex_buffer(str_buf, 2, HEX_BUF(bytes, 1));
	TEST_ASSERT_ARRAY_EQ(str_buf, "a", sizeof("a"));
	TEST_EQ(rv, -EC_ERROR_OVERFLOW, "%d");

	return EC_SUCCESS;
}

test_static int test_vsnprintf_combined(void)
{
	T(expect_success("abc", "%c%s", 'a', "bc"));
	T(expect_success("12\tbc", "%d\t%s", 12, "bc"));
	return EC_SUCCESS;
}

test_static int test_uint64_to_str(void)
{
	/* Longest uin64 in decimal = 20, plus terminating NUL. */
	char buf[21];
	char *str;

	str = uint64_to_str(buf, sizeof(buf), /*val=*/0, /*precision=*/-1,
			    /*base=*/10, /*uppercase=*/false);
	TEST_ASSERT_ARRAY_EQ(str, "0", sizeof("0"));

	str = uint64_to_str(buf, sizeof(buf), /*val=*/UINT64_MAX,
			    /*precision=*/-1, /*base=*/10,
			    /*uppercase=*/false);
	TEST_ASSERT_ARRAY_EQ(str, "18446744073709551615",
			     sizeof("18446744073709551615"));

	/* Buffer too small by 1. */
	str = uint64_to_str(buf, /*buf_len=*/20, /*val=*/UINT64_MAX,
			    /*precision=*/-1, /*base=*/10, /*uppercase=*/false);
	TEST_ASSERT(str == NULL);

	/* lower case hex */
	str = uint64_to_str(buf, sizeof(buf), /*val=*/0, /*precision=*/-1,
			    /*base=*/16, /*uppercase=*/false);
	TEST_ASSERT_ARRAY_EQ(str, "0", sizeof("0"));

	/* lower case hex */
	str = uint64_to_str(buf, sizeof(buf), /*val=*/UINT64_MAX,
			    /*precision=*/-1, /*base=*/16,
			    /*uppercase=*/false);
	TEST_ASSERT_ARRAY_EQ(str, "ffffffffffffffff",
			     sizeof("fffffffffffffff"));

	/* upper case hex */
	str = uint64_to_str(buf, sizeof(buf), /*val=*/0, /*precision=*/-1,
			    /*base=*/16, /*uppercase=*/true);
	TEST_ASSERT_ARRAY_EQ(str, "0", sizeof("0"));

	str = uint64_to_str(buf, sizeof(buf), /*val=*/UINT64_MAX,
			    /*precision=*/-1, /*base=*/16,
			    /*uppercase=*/true);
	TEST_ASSERT_ARRAY_EQ(str, "FFFFFFFFFFFFFFFF",
			     sizeof("FFFFFFFFFFFFFFF"));

	/* precision 0 */
	str = uint64_to_str(buf, sizeof(buf), /*val=*/1, /*precision=*/0,
			    /*base=*/10, /*uppercase=*/false);
	TEST_ASSERT_ARRAY_EQ(str, "1.", sizeof("1."));

	/* precision 6 */
	str = uint64_to_str(buf, sizeof(buf), /*val=*/1, /*precision=*/6,
			    /*base=*/10, /*uppercase=*/false);
	TEST_ASSERT_ARRAY_EQ(str, "0.000001", sizeof("0.000001"));

	/* Reduced precision due to buffer that is too small. */
	str = uint64_to_str(buf, /*buf_len=*/8, /*val=*/1, /*precision=*/6,
			    /*base=*/10, /*uppercase=*/false);
	TEST_ASSERT_ARRAY_EQ(str, "0.00001", sizeof("0.00001"));

	/*
	 * Reduced precision due to buffer that is too small, so precision
	 * gets changed to 0.
	 */
	str = uint64_to_str(buf, /*buf_len=*/3, /*val=*/1, /*precision=*/6,
			    /*base=*/10, /*uppercase=*/false);
	TEST_ASSERT_ARRAY_EQ(str, "1.", sizeof("1."));

	/* Precision is unable to fit in provided buffer. */
	str = uint64_to_str(buf, /*buf_len=*/2, /*val=*/1, /*precision=*/6,
			    /*base=*/10, /*uppercase=*/false);
	TEST_ASSERT(str == NULL);

	/* Negative base. */
	str = uint64_to_str(buf, sizeof(buf), /*val=*/0, /*precision=*/-1,
			    /*base=*/-1, /*uppercase=*/false);
	TEST_ASSERT(str == NULL);

	/* Base zero. */
	str = uint64_to_str(buf, sizeof(buf), /*val=*/1, /*precision=*/-1,
			    /*base=*/0, /*uppercase=*/false);
	TEST_ASSERT(str == NULL);

	/* Base one. */
	str = uint64_to_str(buf, sizeof(buf), /*val=*/1, /*precision=*/-1,
			    /*base=*/1, /*uppercase=*/false);
	TEST_ASSERT(str == NULL);

	/* Buffer size 1. */
	str = uint64_to_str(buf, /*buf_len=*/1, /*val=*/0, /*precision=*/-1,
			    /*base=*/10, /*uppercase=*/false);
	TEST_ASSERT(str == NULL);

	/* Buffer size 0. */
	str = uint64_to_str(buf, /*buf_len=*/0, /*val=*/0, /*precision=*/-1,
			    /*base=*/10, /*uppercase=*/false);
	TEST_ASSERT(str == NULL);

	/* Buffer size -1. */
	str = uint64_to_str(buf, /*buf_len=*/-1, /*val=*/0, /*precision=*/-1,
			    /*base=*/10, /*uppercase=*/false);
	TEST_ASSERT(str == NULL);

	return EC_SUCCESS;
}

void run_test(int argc, const char **argv)
{
	test_reset();

	RUN_TEST(test_vsnprintf_args);
	RUN_TEST(test_vsnprintf_int);
	RUN_TEST(test_vsnprintf_fixed_point);
	RUN_TEST(test_printf_long32_enabled);
	RUN_TEST(test_vsnprintf_long);
	RUN_TEST(test_vsnprintf_pointers);
	RUN_TEST(test_vsnprintf_chars);
	RUN_TEST(test_vsnprintf_strings);
	RUN_TEST(test_vsnprintf_combined);
	RUN_TEST(test_uint64_to_str);
	RUN_TEST(test_snprintf_timestamp);
	RUN_TEST(test_snprintf_hex_buffer);

	test_print_result();
}
