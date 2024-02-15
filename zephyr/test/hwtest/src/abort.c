/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "panic.h"
#include "system.h"

#include <stdlib.h>

#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/ztest.h>

LOG_MODULE_REGISTER(abort_hw_test, LOG_LEVEL_INF);

enum {
	/* Random number to signal the next stage of the test */
	TEST_STATE_ABORT = 0xA76C,
};

void abort_teardown(void *fixture)
{
	system_set_scratchpad(0);
}

ZTEST_SUITE(abort, NULL, NULL, NULL, NULL, abort_teardown);

static void test_abort(void)
{
	system_set_scratchpad(TEST_STATE_ABORT);
	LOG_INF("Calling abort\n");
	cflush();
	abort();
	/* Should never reach this. */
	zassert_unreachable();
}

static void test_panic_data(void)
{
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

ZTEST(abort, test_abort)
{
	uint32_t state = 0;

	system_get_scratchpad(&state);
	switch (state) {
	case TEST_STATE_ABORT:
		test_panic_data();
		break;
	default:
		test_abort();
	}
}

#ifdef CONFIG_ZTEST_SHELL
static struct k_work abort_test_work;
/* If the test shell is enabled, the test will be run once by a test runner.
 * The abort call causes a reboot, which means we need to run it again depending
 * on the test state.
 */
static void abort_test_handler(struct k_work *work)
{
	uint32_t state = 0;

	system_get_scratchpad(&state);
	/* The first state is run via console */
	switch (state) {
	case TEST_STATE_ABORT:
		ztest_run_test_suites(NULL, false, 1, 1);
		break;
	default:
		break;
	}
}

static int abort_test_init(void)
{
	k_work_init(&abort_test_work, abort_test_handler);

	/* Check if the test has to be run after reboot */
	k_work_submit(&abort_test_work);

	return 0;
}
SYS_INIT(abort_test_init, POST_KERNEL, CONFIG_APPLICATION_INIT_PRIORITY);
#endif /* CONFIG_ZTEST_SHELL */
