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

	panic_set_reason(0, 0, 0);

	k_sys_fatal_error_handler(K_ERR_KERNEL_PANIC, NULL);

	/* No panic reason set by EC, make sure the one from Zephyr is stored */
	panic_get_reason(&reason, &info, &exception);
	zassert_equal(PANIC_ZEPHYR_FATAL_ERROR, reason);
	zassert_equal(K_ERR_KERNEL_PANIC, info);
	zassert_equal(task_get_current(), exception);
}

ZTEST(panic_reason, test_panic_reason_ec)
{
	uint32_t reason;
	uint32_t info;
	uint8_t exception;

	panic_set_reason(PANIC_SW_EXIT, 0x64, 0xFA);

	k_sys_fatal_error_handler(K_ERR_KERNEL_PANIC, NULL);

	/* Make sure, that the panic reason set by EC is in place */
	panic_get_reason(&reason, &info, &exception);
	zassert_equal(PANIC_SW_EXIT, reason);
	zassert_equal(0x64, info);
	zassert_equal(0xFA, exception);
}

ZTEST_SUITE(panic_reason, drivers_predicate_post_main, NULL, NULL, NULL, NULL);
