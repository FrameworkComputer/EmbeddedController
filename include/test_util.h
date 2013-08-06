/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Various utility for unit testing */

#ifndef __CROS_EC_TEST_UTIL_H
#define __CROS_EC_TEST_UTIL_H

#include "common.h"
#include "console.h"

#define RUN_TEST(n) \
	do { \
		ccprintf("Running %s...", #n); \
		cflush(); \
		if (n() == EC_SUCCESS) { \
			ccputs("OK\n"); \
		} else { \
			ccputs("Fail\n"); \
			__test_error_count++; \
		} \
	} while (0)

#define TEST_ASSERT(n) \
	do { \
		if (!(n)) { \
			ccprintf("%d: ASSERTION failed: %s\n", __LINE__, #n); \
			return EC_ERROR_UNKNOWN; \
		} \
	} while (0)

#define __ABS(n) ((n) > 0 ? (n) : -(n))

#define TEST_ASSERT_ABS_LESS(n, t) \
	do { \
		if (__ABS(n) >= t) { \
			ccprintf("%d: ASSERT_ABS_LESS failed: abs(%d) is " \
				 "not less than %d\n", __LINE__, n, t); \
			return EC_ERROR_UNKNOWN; \
		} \
	} while (0)

#define TEST_ASSERT_ARRAY_EQ(s, d, n) \
	do { \
		int __i; \
		for (__i = 0; __i < n; ++__i) \
			if ((s)[__i] != (d)[__i]) { \
				ccprintf("%d: ASSERT_ARRAY_EQ failed at " \
					 "index=%d: %d != %d\n", __LINE__, \
					 __i, (int)(s)[__i], (int)(d)[__i]); \
				return EC_ERROR_UNKNOWN; \
			} \
	} while (0)

#define TEST_CHECK(n) \
	do { \
		if (n) \
			return EC_SUCCESS; \
		else \
			return EC_ERROR_UNKNOWN; \
	} while (0)

/* Hooks gcov_flush() for test coverage report generation */
void register_test_end_hook(void);

/* Test entry point */
void run_test(void);

/* Resets test error count */
void test_reset(void);

/* Reports test pass */
void test_pass(void);

/* Reports test failure */
void test_fail(void);

/* Prints test result, including number of failed tests */
void test_print_result(void);

/* Returns the number of failed tests */
int test_get_error_count(void);

/* Simulates host command sent from the host */
int test_send_host_command(int command, int version, const void *params,
			   int params_size, void *resp, int resp_size);

/* Number of failed tests */
extern int __test_error_count;

/* Simulates UART input */
void uart_inject_char(char *s, int sz);

#define UART_INJECT(s) uart_inject_char(s, strlen(s));

/* Simulates chipset power on */
void test_chipset_on(void);

/* Simulates chipset power off */
void test_chipset_off(void);

/* Start/stop capturing console output */
void test_capture_console(int enabled);

/* Get captured console output */
const char *test_get_captured_console(void);

#endif /* __CROS_EC_TEST_UTIL_H */
