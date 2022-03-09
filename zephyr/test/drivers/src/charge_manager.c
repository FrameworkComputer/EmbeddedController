/* Copyright 2022 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <ztest.h>

#include "charge_manager.h"
#include "ec_commands.h"
#include "test_state.h"

ZTEST_SUITE(charge_manager, drivers_predicate_post_main, NULL, NULL, NULL,
	    NULL);

/**
 * Test the default implementation of board_fill_source_power_info(). This
 * function should reset all the power info values. If this test overrides this
 * function, this test can be removed.
 */
ZTEST_USER(charge_manager, test_default_fill_power_info)
{
	struct ec_response_usb_pd_power_info info = {
		.meas = {
			.voltage_now = 10,
			.voltage_max = 10,
			.current_max = 10,
			.current_lim = 10,
		},
		.max_power = 10,
	};

	board_fill_source_power_info(0, &info);
	zassert_equal(info.meas.voltage_now, 0, NULL);
	zassert_equal(info.meas.voltage_max, 0, NULL);
	zassert_equal(info.meas.current_max, 0, NULL);
	zassert_equal(info.meas.current_lim, 0, NULL);
	zassert_equal(info.max_power, 0, NULL);
}
