/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "usb_pd.h"

#include <zephyr/ztest.h>

ZTEST_SUITE(usbc, NULL, NULL, NULL, NULL, NULL);

ZTEST(usbc, test_board_get_pd_port_location)
{
	zassert_equal(board_get_pd_port_location(0),
		      EC_PD_PORT_LOCATION_LEFT_FRONT);
	zassert_equal(board_get_pd_port_location(1),
		      EC_PD_PORT_LOCATION_LEFT_BACK);
	zassert_equal(board_get_pd_port_location(2),
		      EC_PD_PORT_LOCATION_UNKNOWN);
}
