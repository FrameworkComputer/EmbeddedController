/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Test common utilities.
 */

#include "common.h"
#include "console.h"
#include "util.h"

static int error_count;

#define RUN_TEST(n) \
	do { \
		ccprintf("Running %s...", #n); \
		cflush(); \
		if (n()) { \
			ccputs("OK\n"); \
		} else { \
			ccputs("Fail\n"); \
			error_count++; \
		} \
	} while (0)

static int test_strlen(void)
{
	return strlen("this is a string") == 16;
}

static int test_strcasecmp(void)
{
	return (strcasecmp("test string", "TEST strIng") == 0) &&
	       (strcasecmp("test123!@#", "TesT123!@#") == 0) &&
	       (strcasecmp("lower", "UPPER") != 0);
}

static int test_strncasecmp(void)
{
	return (strncasecmp("test string", "TEST str", 4) == 0) &&
	       (strncasecmp("test string", "TEST str", 8) == 0) &&
	       (strncasecmp("test123!@#", "TesT321!@#", 5) != 0) &&
	       (strncasecmp("test123!@#", "TesT321!@#", 4) == 0) &&
	       (strncasecmp("1test123!@#", "1TesT321!@#", 5) == 0);
}

static int test_atoi(void)
{
	return (atoi("  901") == 901) &&
	       (atoi("-12c") == -12) &&
	       (atoi("   0  ") == 0) &&
	       (atoi("\t111") == 111);
}

static int test_uint64divmod(void)
{
	uint64_t n = 8567106442584750ULL;
	int d = 54870071;
	int r = uint64divmod(&n, d);

	return (r == 5991285 && n == 156134415ULL);
}

static int command_run_test(int argc, char **argv)
{
	error_count = 0;

	RUN_TEST(test_strlen);
	RUN_TEST(test_strcasecmp);
	RUN_TEST(test_strncasecmp);
	RUN_TEST(test_atoi);
	RUN_TEST(test_uint64divmod);

	if (error_count) {
		ccprintf("Failed %d tests!\n", error_count);
		return EC_ERROR_UNKNOWN;
	} else {
		ccprintf("Pass!\n");
		return EC_SUCCESS;
	}
}
DECLARE_CONSOLE_COMMAND(runtest, command_run_test,
			NULL, NULL, NULL);
