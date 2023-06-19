/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "host_command.h"
#include "test/drivers/test_state.h"

#include <zephyr/ztest.h>

ZTEST(host_request_expected_size, test_mismatched_host_request_version)
{
	const struct ec_host_request request = {
		/* Make version mismatch */
		.struct_version = EC_HOST_REQUEST_VERSION + 1,
	};
	int ret = host_request_expected_size(&request);

	zassert_equal(ret, 0);
}

ZTEST(host_request_expected_size, test_nonzero_reserved_bytes)
{
	const struct ec_host_request request = {
		/* Make version match */
		.struct_version = EC_HOST_REQUEST_VERSION,
		/* Set lsb to reserved bytes */
		.reserved = 1,
	};
	int ret = host_request_expected_size(&request);

	zassert_equal(ret, 0);
}

ZTEST(host_request_expected_size, test_data_len_added_to_response_size)
{
	const struct ec_host_request request = {
		/* Make version match */
		.struct_version = EC_HOST_REQUEST_VERSION,
		/* Set reserved bytes to 0 (required) */
		.reserved = 0,
		/* Test data length max value to see changes in M/LSB */
		.data_len = UINT16_MAX,
	};
	int ret = host_request_expected_size(&request);

	zassert_equal(ret, sizeof(request) + request.data_len);
}

ZTEST_SUITE(host_request_expected_size, drivers_predicate_post_main, NULL, NULL,
	    NULL, NULL);
