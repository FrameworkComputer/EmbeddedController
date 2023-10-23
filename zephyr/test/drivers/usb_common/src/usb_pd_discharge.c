/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "suite.h"
#include "usb_pd.h"
#include "usb_pd_flags.h"

#include <zephyr/fff.h>
#include <zephyr/ztest_assert.h>
#include <zephyr/ztest_test.h>

#define TEST_PORT 0

ZTEST_USER(usb_common, test_pd_set_vbus_discharge)
{
	board_vbus_source_enabled_fake.return_val = 0;

	pd_set_vbus_discharge(TEST_PORT, true);
	zassert_equal(ppc_discharge_vbus_fake.arg0_history[0], TEST_PORT);
	zassert_equal(ppc_discharge_vbus_fake.arg1_history[0], 1);
}

ZTEST_USER(usb_common, test_pd_set_vbus_discharge_wrong_args)
{
	pd_set_vbus_discharge(100, true);
	zassert_equal(ppc_discharge_vbus_fake.call_count, 0);
}
