/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "host_command.h"
#include "test/drivers/test_state.h"

#include <zephyr/ztest.h>

ZTEST_SUITE(locate_chip, drivers_predicate_post_main, NULL, NULL, NULL, NULL);

ZTEST_USER(locate_chip, test_invalid_request_for_eeprom)
{
	int ret;
	struct ec_params_locate_chip p = {
		.type = EC_CHIP_TYPE_CBI_EEPROM,
	};
	struct ec_response_locate_chip r;

	ret = ec_cmd_locate_chip(NULL, &p, &r);

	zassert_equal(ret, EC_RES_UNAVAILABLE, "Unexpected return value: %d",
		      ret);
}
