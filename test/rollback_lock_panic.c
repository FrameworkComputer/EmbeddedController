/* Copyright 2026 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "panic.h"
#include "rollback.h"
#include "system.h"
#include "task.h"
#include "test_util.h"

#include <errno.h>

/* Global flag to toggle mock behavior. */
static int mock_mpu_fail;

/* Mock the MPU lock function. */
int mpu_lock_rollback(bool lock)
{
	if (mock_mpu_fail)
		return -ENOENT;

	return EC_SUCCESS;
}

/*
 * These are the functions being tested. They are expected to panic when
 * mpu_lock_rollback fails.
 */
extern uint32_t unlock_rollback(void);
extern void lock_rollback(uint32_t key);

/* Validates that the previous reboot was caused by a software panic. */
test_static int verify_panic_reason(void)
{
	struct panic_data *pdata = panic_get_data();

	/*
	 * Validates that the previous reboot was caused by a software panic
	 * triggered during the rollback lock/unlock sequence.
	 */
	TEST_ASSERT(pdata->flags & PANIC_DATA_FLAG_FRAME_VALID);

	/* For ARM, R4 holds the panic reason in EC. */
	TEST_EQ(pdata->cm.regs[CORTEX_PANIC_REGISTER_R4], PANIC_SW_ASSERT,
		"%08x");

	return EC_SUCCESS;
}

void test_run_step(uint32_t state)
{
	/* Step 1: Trigger panic via unlock_rollback. */
	if (state & TEST_STATE_MASK(TEST_STATE_STEP_1)) {
		test_set_next_step(TEST_STATE_STEP_2);

		/* Enable the failure mock in the test. */
		mock_mpu_fail = 1;

		ccprintf("Triggering unlock_rollback panic...\n");
		cflush();

		unlock_rollback();

		/* Should never reach here. */
		test_reboot_to_next_step(TEST_STATE_FAILED);
	}

	/* Step 2: Verify first panic and trigger lock_rollback panic. */
	else if (state & TEST_STATE_MASK(TEST_STATE_STEP_2)) {
		RUN_TEST(verify_panic_reason);

		test_set_next_step(TEST_STATE_STEP_3);

		/* Enable the failure mock in the test. */
		mock_mpu_fail = 1;

		ccprintf("Triggering lock_rollback panic...\n");
		cflush();

		lock_rollback(0);

		/* Should never reach here. */
		test_reboot_to_next_step(TEST_STATE_FAILED);
	}

	/* Step 3: Verify second panic and finish. */
	else if (state & TEST_STATE_MASK(TEST_STATE_STEP_3)) {
		RUN_TEST(verify_panic_reason);
		test_reboot_to_next_step(TEST_STATE_PASSED);
	}
}

int task_test(void *unused)
{
	test_run_multistep();

	return EC_SUCCESS;
}

void run_test(int argc, const char **argv)
{
	test_reset();
	crec_msleep(30);
	task_wake(TASK_ID_TEST);
}
