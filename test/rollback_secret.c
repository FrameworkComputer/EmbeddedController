/* Copyright 2020 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <stdint.h>

#include "mock/rollback_latest_mock.h"
#include "rollback.h"
#include "rollback_private.h"
#include "string.h"
#include "system.h"
#include "test_util.h"

extern int get_latest_rollback(struct rollback_data *data);

test_static int test_get_rollback_secret_latest_rollback_fail(void)
{
	struct rollback_data test_data;
	uint8_t secret[32] = { 0 };

	mock_ctrl_latest_rollback.output_type = GET_LATEST_ROLLBACK_FAIL;
	TEST_ASSERT(get_latest_rollback(&test_data) == -5);

	TEST_ASSERT(rollback_get_secret(secret) == EC_ERROR_UNKNOWN);

	return EC_SUCCESS;
}

test_static int test_get_rollback_secret_latest_rollback_secret_zeros(void)
{
	struct rollback_data test_data;
	uint8_t secret[32];
	const uint8_t zeros_secret[] = {
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	};

	mock_ctrl_latest_rollback.output_type = GET_LATEST_ROLLBACK_ZEROS;
	TEST_ASSERT(get_latest_rollback(&test_data) == EC_SUCCESS);
	TEST_ASSERT_ARRAY_EQ(test_data.secret, zeros_secret,
			     sizeof(zeros_secret));

	TEST_ASSERT(rollback_get_secret(secret) == EC_ERROR_UNKNOWN);

	return EC_SUCCESS;
}

test_static int test_get_rollback_secret_latest_rollback_secret_succeed(void)
{
	struct rollback_data test_data;
	uint8_t secret[32];
	const uint8_t real_secret[] = {
		0xcf, 0xe3, 0x23, 0x76, 0x35, 0x04, 0xc2, 0x0f,
		0x0d, 0xb6, 0x02, 0xa9, 0x68, 0xba, 0x2a, 0x61,
		0x86, 0x2a, 0x85, 0xd1, 0xca, 0x09, 0x54, 0x8a,
		0x6b, 0xe2, 0xe3, 0x38, 0xde, 0x5d, 0x59, 0x14,
	};

	mock_ctrl_latest_rollback.output_type = GET_LATEST_ROLLBACK_REAL;
	TEST_ASSERT(get_latest_rollback(&test_data) == EC_SUCCESS);
	TEST_ASSERT_ARRAY_EQ(test_data.secret, real_secret,
			     sizeof(real_secret));

	TEST_ASSERT(rollback_get_secret(secret) == EC_SUCCESS);
	TEST_ASSERT_ARRAY_EQ(secret, test_data.secret, sizeof(secret));

	return EC_SUCCESS;
}

void run_test(int argc, const char **argv)
{
	RUN_TEST(test_get_rollback_secret_latest_rollback_fail);
	RUN_TEST(test_get_rollback_secret_latest_rollback_secret_zeros);
	RUN_TEST(test_get_rollback_secret_latest_rollback_secret_succeed);
	test_print_result();
}
