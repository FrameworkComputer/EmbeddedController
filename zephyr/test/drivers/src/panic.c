/* Copyright 2022 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/**
 * @file
 * @brief Unit Tests for panic.
 */

#include <device.h>

#include <logging/log.h>
#include <zephyr.h>
#include <ztest.h>

#include "common.h"
#include "ec_tasks.h"
#include "panic.h"
#include "test/drivers/stubs.h"
#include "test/drivers/test_state.h"

/**
 * @brief Test Suite: Verifies panic functionality.
 */
ZTEST_SUITE(panic, drivers_predicate_post_main, NULL, NULL, NULL, NULL);

/**
 * @brief TestPurpose: Verify panic set/get reason.
 *
 * @details
 * Validate panic set/get reason.
 *
 * Expected Results
 *  - Success
 */
ZTEST(panic, test_panic_reason)
{
	uint32_t reason;
	uint32_t info;
	uint8_t exception;
	struct panic_data *pdata = panic_get_data();

	zassert_is_null(pdata, NULL);
	panic_set_reason(PANIC_SW_WATCHDOG, 0, 0);

	panic_get_reason(&reason, &info, &exception);

	zassert_equal(PANIC_SW_WATCHDOG, reason, NULL);
	zassert_equal(0, info, NULL);
	zassert_equal(0, exception, NULL);

	pdata = panic_get_data();
	zassert_not_null(pdata, NULL);
	zassert_equal(pdata->struct_version, 2, NULL);
	zassert_equal(pdata->magic, PANIC_DATA_MAGIC, NULL);
	zassert_equal(pdata->struct_size, CONFIG_PANIC_DATA_SIZE, NULL);

	panic_data_print(pdata);
}
