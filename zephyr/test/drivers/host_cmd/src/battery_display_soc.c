/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "battery.h"
#include "charge_state.h"
#include "host_command.h"
#include "test/drivers/test_state.h"

#include <zephyr/ztest.h>

ZTEST_USER(battery_display_soc, happy_path)
{
	const uint32_t full_charge_as_tenths =
		CONFIG_BATT_HOST_FULL_FACTOR * 10;
	const uint32_t host_shutdown_charge_as_tenths =
		CONFIG_BATT_HOST_SHUTDOWN_PERCENTAGE * 10;
	struct ec_response_display_soc response;
	struct host_cmd_handler_args args;

	zassert_ok(ec_cmd_display_soc(&args, &response));

	zassert_equal(args.response_size, sizeof(response));
	zassert_equal(response.display_soc, charge_get_display_charge());
	zassert_equal(response.full_factor, full_charge_as_tenths);
	zassert_equal(response.shutdown_soc, host_shutdown_charge_as_tenths);
}

ZTEST_SUITE(battery_display_soc, drivers_predicate_post_main, NULL, NULL, NULL,
	    NULL);
