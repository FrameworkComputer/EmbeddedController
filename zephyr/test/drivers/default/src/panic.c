/* Copyright 2022 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/**
 * @file
 * @brief Unit Tests for panic.
 */

#include <zephyr/device.h>

#include <zephyr/logging/log.h>
#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

#include "common.h"
#include "ec_tasks.h"
#include "panic.h"
#include "test/drivers/stubs.h"
#include "test/drivers/test_state.h"

struct panic_test_fixture {
	struct panic_data saved_pdata;
};

static void *panic_test_setup(void)
{
	static struct panic_test_fixture panic_fixture = { 0 };

	return &panic_fixture;
}

static void panic_before(void *state)
{
	struct panic_test_fixture *fixture = state;
	struct panic_data *pdata = get_panic_data_write();

	ARG_UNUSED(state);

	fixture->saved_pdata = *pdata;
}

static void panic_after(void *state)
{
	struct panic_test_fixture *fixture = state;
	struct panic_data *pdata = get_panic_data_write();

	ARG_UNUSED(state);

	*pdata = fixture->saved_pdata;
}

/**
 * @brief Test Suite: Verifies panic functionality.
 */
ZTEST_SUITE(panic, drivers_predicate_post_main, panic_test_setup, panic_before,
	    panic_after, NULL);

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

ZTEST(panic, test_panic_data_start_bad_magic)
{
	struct panic_data *pdata = get_panic_data_write();

	pdata->magic = PANIC_DATA_MAGIC + 1;
	zassert_equal(0, get_panic_data_start(), NULL);
}

ZTEST(panic, test_get_panic_data_start)
{
	struct panic_data *pdata = get_panic_data_write();

	pdata->magic = PANIC_DATA_MAGIC;
	zassert_equal((uintptr_t)pdata, get_panic_data_start(), NULL);
}
