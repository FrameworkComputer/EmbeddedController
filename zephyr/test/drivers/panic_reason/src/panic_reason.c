/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "panic.h"
#include "task.h"
#include "test/drivers/test_state.h"

#include <zephyr/ztest.h>
#include <zephyr/ztest_assert.h>

#include <sys/types.h>

ZTEST(panic_reason, test_panic_reason_zephyr)
{
	uint32_t reason;
	uint32_t info;
	uint8_t exception;

	k_sys_fatal_error_handler(K_ERR_KERNEL_PANIC, NULL);

	/* ESF structure empty, make sure the reason from Zephyr is stored */
	panic_get_reason(&reason, &info, &exception);
	zassert_equal(PANIC_ZEPHYR_FATAL_ERROR, reason);
	zassert_equal(K_ERR_KERNEL_PANIC, info);
	zassert_equal(task_get_current(), exception);
}

ZTEST_SUITE(panic_reason, drivers_predicate_post_main, NULL, NULL, NULL, NULL);
