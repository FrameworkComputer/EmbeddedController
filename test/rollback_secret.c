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

	mock_ctrl_latest_rollback.output_type = GET_LATEST_ROLLBACK_ZEROS;
	TEST_ASSERT(get_latest_rollback(&test_data) == EC_SUCCESS);
	TEST_ASSERT_ARRAY_EQ(test_data.secret,
			     fake_latest_rollback_zeros.secret,
			     sizeof(fake_latest_rollback_zeros.secret));

	TEST_ASSERT(rollback_get_secret(secret) == EC_ERROR_UNKNOWN);

	return EC_SUCCESS;
}

test_static int test_get_rollback_secret_latest_rollback_secret_succeed(void)
{
	struct rollback_data test_data;
	uint8_t secret[32];

	mock_ctrl_latest_rollback.output_type = GET_LATEST_ROLLBACK_REAL;
	TEST_ASSERT(get_latest_rollback(&test_data) == EC_SUCCESS);
	TEST_ASSERT_ARRAY_EQ(test_data.secret, fake_latest_rollback_real.secret,
			     sizeof(fake_latest_rollback_real.secret));

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
