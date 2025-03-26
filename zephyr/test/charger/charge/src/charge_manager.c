/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "charge_manager.h"
#include "ec_commands.h"

#include <zephyr/ztest.h>

ZTEST_SUITE(charge_manager, NULL, NULL, NULL, NULL, NULL);

/**
 * Test the default implementation of board_fill_source_power_info(). The fill
 * function should reset all the power info values. If the test binary overrides
 * board_fill_source_power_info(), then this test can be removed.
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
	zassert_equal(info.meas.voltage_now, 0);
	zassert_equal(info.meas.voltage_max, 0);
	zassert_equal(info.meas.current_max, 0);
	zassert_equal(info.meas.current_lim, 0);
	zassert_equal(info.max_power, 0);
}

/**
 * Test the default implementation of board_charge_port_is_connected(). This
 * function should always return 1 regardless of input.
 */
ZTEST_USER(charge_manager, test_default_charge_port_is_connected)
{
	zassert_true(board_charge_port_is_connected(-1));
	zassert_true(board_charge_port_is_connected(0));
	zassert_true(board_charge_port_is_connected(1));
	zassert_true(board_charge_port_is_connected(500));
}

ZTEST_USER(charge_manager, test_default_charge_port_is_sink)
{
	zassert_true(board_charge_port_is_sink(-1));
	zassert_true(board_charge_port_is_sink(0));
	zassert_true(board_charge_port_is_sink(1));
	zassert_true(board_charge_port_is_sink(500));
}
