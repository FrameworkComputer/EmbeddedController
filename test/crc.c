/* Copyright 2016 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Tests crc32 sw implementation.
 */

#include "common.h"
#include "console.h"
#include "crc.h"
#include "crc8.h"
#include "test_util.h"
#include "util.h"

// test that static version matches context version
static int test_static_version(void)
{
	uint32_t crc;
	const uint32_t input = 0xdeadbeef;

	crc32_ctx_init(&crc);
	crc32_ctx_hash32(&crc, input);

	crc32_init();
	crc32_hash32(input);
	TEST_ASSERT(crc32_result() == crc32_ctx_result(&crc));

	crc32_init();
	crc32_hash(&input, sizeof(input));
	TEST_ASSERT(crc32_result() == crc32_ctx_result(&crc));

	return EC_SUCCESS;
}

// test that context bytes at a time matches static word at time
static int test_8(void)
{
	uint32_t crc;
	const uint32_t input = 0xdeadbeef;
	const uint8_t *p = (const uint8_t *)&input;
	int i;

	crc32_init();
	crc32_hash32(input);

	crc32_ctx_init(&crc);
	for (i = 0; i < sizeof(input); ++i)
		crc32_ctx_hash8(&crc, p[i]);

	TEST_ASSERT(crc32_result() == crc32_ctx_result(&crc));

	return EC_SUCCESS;
}

// http://www.febooti.com/products/filetweak/members/hash-and-crc/test-vectors/
static int test_kat0(void)
{
	uint32_t crc;
	int i;
	const char input[] = "The quick brown fox jumps over the lazy dog";

	crc32_ctx_init(&crc);
	for (i = 0; i < strlen(input); ++i)
		crc32_ctx_hash8(&crc, input[i]);
	TEST_ASSERT(crc32_ctx_result(&crc) == 0x414fa339);

	crc32_ctx_init(&crc);
	crc32_ctx_hash(&crc, input, strlen(input));
	TEST_ASSERT(crc32_ctx_result(&crc) == 0x414fa339);

	return EC_SUCCESS;
}

static int test_cros_crc8(void)
{
	uint8_t buffer[10] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 8 };

	int crc = cros_crc8(buffer, 10);

	/* Verifies polynomial values of 0x07 representing x^8 + x^2 + x + 1 */
	TEST_EQ(crc, 170, "%d");

	return EC_SUCCESS;
}

void run_test(int argc, const char **argv)
{
	test_reset();

	RUN_TEST(test_static_version);
	RUN_TEST(test_8);
	RUN_TEST(test_kat0);
	RUN_TEST(test_cros_crc8);

	test_print_result();
}
