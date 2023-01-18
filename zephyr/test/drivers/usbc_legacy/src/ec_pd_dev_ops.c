/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * Tests for operations related to PD chips based on chrome EC
 * source. Modern devices do not use this. These tests are primarily for
 * code coverage purposes.
 */

#include "config.h"
#include "ec_commands.h"
#include "host_command.h"
#include "test/drivers/test_state.h"
#include "test/drivers/utils.h"

#include <zephyr/ztest.h>

#define TEST_PORT 0
#define INVALID_TEST_PORT 99

ZTEST(ec_pd_dev_ops, dev_info)
{
	struct ec_params_usb_pd_rw_hash_entry response;

	zassert_equal(EC_RES_INVALID_PARAM,
		      host_cmd_usb_pd_dev_info(INVALID_TEST_PORT, &response));

	zassert_ok(host_cmd_usb_pd_dev_info(TEST_PORT, &response), NULL);
	/* We have not set up a device on the port, so dev_id should be 0. */
	zassert_equal(0, response.dev_id);
}

ZTEST_SUITE(ec_pd_dev_ops, NULL, NULL, NULL, NULL, NULL);
