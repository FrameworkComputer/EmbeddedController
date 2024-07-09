/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "charge_manager.h"
#include "test/drivers/stubs.h"
#include "test/drivers/test_state.h"
#include "test/drivers/utils.h"
#include "usbc/pdc_power_mgmt.h"

#include <zephyr/fff.h>
#include <zephyr/ztest.h>

#define TEST_PORT USBC_PORT_C0
BUILD_ASSERT(TEST_PORT == USBC_PORT_C0);

FAKE_VALUE_FUNC(int, pdc_power_mgmt_get_vbus_voltage, int);

ZTEST(charge_manager, test_get_vbus_voltage)
{
	pdc_power_mgmt_get_vbus_voltage_fake.return_val = 15000;
	zassert_equal(charge_manager_get_vbus_voltage(TEST_PORT), 15000);
}

ZTEST_SUITE(charge_manager, drivers_predicate_post_main, NULL, NULL, NULL,
	    NULL);
