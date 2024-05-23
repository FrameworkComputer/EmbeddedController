/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "charge_manager.h"
#include "test/drivers/stubs.h"
#include "test/drivers/test_state.h"
#include "test/drivers/utils.h"

#include <zephyr/ztest.h>

#define TEST_PORT USBC_PORT_C0
BUILD_ASSERT(TEST_PORT == USBC_PORT_C0);

ZTEST(charge_manager, test_get_vbus_voltage)
{
}

ZTEST_SUITE(charge_manager, drivers_predicate_post_main, NULL, NULL, NULL,
	    NULL);
