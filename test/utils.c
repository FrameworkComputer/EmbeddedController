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
#include "timer.h"
#include "util.h"

static int error_count;

#define RUN_TEST(n) \
	do { \
		ccprintf("Running %s...", #n); \
		cflush(); \
		if (n() == EC_SUCCESS) { \
			ccputs("OK\n"); \
		} else { \
			ccputs("Fail\n"); \
			error_count++; \
		} \
	} while (0)

#define TEST_ASSERT(n) \
	do { \
		if (!(n)) \
			return EC_ERROR_UNKNOWN; \
	} while (0)

#define TEST_CHECK(n) \
	do { \
		if (n) \
			return EC_SUCCESS; \
		else \
			return EC_ERROR_UNKNOWN; \
	} while (0)

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
		   (strncasecmp("1test123!@#", "1TesT321!@#", 5) == 0));
}

static int test_atoi(void)
{
	TEST_CHECK((atoi("  901") == 901) &&
		   (atoi("-12c") == -12) &&
		   (atoi("   0  ") == 0) &&
		   (atoi("\t111") == 111));
}

static int test_uint64divmod(void)
{
	uint64_t n = 8567106442584750ULL;
	int d = 54870071;
	int r = uint64divmod(&n, d);

	TEST_CHECK(r == 5991285 && n == 156134415ULL);
}

static int test_shared_mem(void)
{
	int i, j;
	int sz = shared_mem_size();
	char *mem;

	TEST_ASSERT(shared_mem_acquire(sz, &mem) == EC_SUCCESS);
	TEST_ASSERT(shared_mem_acquire(sz, &mem) == EC_ERROR_BUSY);

	for (i = 0; i < 256; ++i) {
		memset(mem, i, sz);
		for (j = 0; j < sz; ++j)
			TEST_ASSERT(mem[j] == (char)i);

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

void run_test(void)
{
	error_count = 0;

	RUN_TEST(test_strlen);
	RUN_TEST(test_strcasecmp);
	RUN_TEST(test_strncasecmp);
	RUN_TEST(test_atoi);
	RUN_TEST(test_uint64divmod);
	RUN_TEST(test_shared_mem);
	RUN_TEST(test_scratchpad);

	if (error_count)
		ccprintf("Failed %d tests!\n", error_count);
	else
		ccprintf("Pass!\n");
}

static int command_run_test(int argc, char **argv)
{
	run_test();
	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(runtest, command_run_test,
			NULL, NULL, NULL);
