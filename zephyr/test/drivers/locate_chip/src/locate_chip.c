/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <zephyr/ztest.h>

#include "test/drivers/test_state.h"
#include "host_command.h"

ZTEST_SUITE(locate_chip, drivers_predicate_post_main, NULL, NULL, NULL, NULL);

ZTEST_USER(locate_chip, test_invalid_request_for_eeprom)
{
	int ret;
	struct ec_params_locate_chip p = {
		.type = EC_CHIP_TYPE_CBI_EEPROM,
	};
	struct ec_response_locate_chip r;
	struct host_cmd_handler_args args =
		BUILD_HOST_COMMAND(EC_CMD_LOCATE_CHIP, 0, r, p);

	ret = host_command_process(&args);

	zassert_equal(ret, EC_RES_UNAVAILABLE, "Unexpected return value: %d",
		      ret);
}
