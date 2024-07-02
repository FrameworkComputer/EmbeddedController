/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "multistep_test.h"
#include "panic.h"

#include <zephyr/logging/log.h>
#include <zephyr/ztest.h>

#include <exception>

LOG_MODULE_REGISTER(exception_hw_test, LOG_LEVEL_INF);

void exception_lib_throw(void);

static void test_panic_data()
{
	/* The abort function is provided by Zephyr and causes kernel panic.
	 * All we can check is PC register, because a panic reason is not set.
	 */
#ifdef CONFIG_ARM
	struct panic_data *const pdata = panic_get_data();
	uint32_t abort_addr = (uint32_t)abort;
	/* Estimated end of the abort function, which is short. */
	uint32_t abort_end = (uint32_t)abort + 0x40;
	uint32_t pc = pdata->cm.frame[CORTEX_PANIC_FRAME_REGISTER_PC];

	/* Make sure Program Counter is stored correctly and points at the abort
	 * function.
	 */
	zassert_true((abort_addr <= pc) && (abort_end >= pc));
#endif
}

static void test_exception(void)
{
	LOG_INF("Throwing an exception");
	exception_lib_throw();

	/*
	 * Since we have exceptions disabled, we should not reach this.
	 * Instead, the exception should cause a reboot.
	 */
	zassert_unreachable();
}

static void (*test_steps[])(void) = { test_exception, test_panic_data };

MULTISTEP_TEST(exception, test_steps)
