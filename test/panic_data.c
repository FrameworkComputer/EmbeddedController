/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "builtin/assert.h"
#include "panic.h"
#include "system.h"
#include "task.h"
#include "test_util.h"

static int get_assert_line(void)
{
	/* Returned number should point to ASSERT(0) line below */
	return __LINE__ + 5;
}

static void crash_system(void)
{
	ASSERT(0);
}

test_static int test_panic_data(void)
{
	struct panic_data *pdata = panic_get_data();

	/* Check panic reason. */
	TEST_EQ(pdata->cm.regs[CORTEX_PANIC_REGISTER_R4], PANIC_SW_ASSERT,
		"%08x");

	/*
	 * Upper two bytes of panic info are the first two characters of the
	 * filename. The name of this file is "test/panic_data.c", so we look
	 * for "te".
	 */
	TEST_EQ((pdata->cm.regs[CORTEX_PANIC_REGISTER_R5] & 0xff000000) >> 24,
		't', "%c");

	TEST_EQ((pdata->cm.regs[CORTEX_PANIC_REGISTER_R5] & 0x00ff0000) >> 16,
		'e', "%c");

	/*
	 * The lower 16 bits of the panic info is the line number of the
	 * ASSERT call.
	 */
	TEST_EQ(pdata->cm.regs[CORTEX_PANIC_REGISTER_R5] & 0xffff,
		get_assert_line(), "%d");

	/*
	 * Check panic exception - it should be always 0 because panic didn't
	 * happen during interrupt processing.
	 */
	TEST_EQ(pdata->cm.regs[CORTEX_PANIC_REGISTER_IPSR], 0, "%d");

	/* Check panic flags. */
	TEST_EQ(pdata->flags,
		PANIC_DATA_FLAG_FRAME_VALID | PANIC_DATA_FLAG_OLD_HOSTEVENT,
		"%02x");

	return EC_SUCCESS;
}

/*
 * After hard reboot we expect to have panic flags, panic exception and lower
 * 16 bits of panic reason and info (upper 16 bits should be zero). This
 * information is saved in backup RAM because hard reboot clears memory. The
 * backup RAM only has 16 bits available for this information. Check if lower
 * 16 bits of reason and info are present and upper 16 bits are zero.
 */
test_static int test_panic_data_half(void)
{
	struct panic_data *pdata = panic_get_data();

	/* Check panic reason. */
	TEST_EQ(pdata->cm.regs[CORTEX_PANIC_REGISTER_R4],
		(PANIC_SW_ASSERT & 0xffff), "%08x");

	/* Check panic info. */
	TEST_EQ(pdata->cm.regs[CORTEX_PANIC_REGISTER_R5],
		(get_assert_line() & 0xffff), "%d");

	/*
	 * Check panic exception - it should be always 0 because panic didn't
	 * happen during interrupt processing.
	 */
	TEST_EQ(pdata->cm.regs[CORTEX_PANIC_REGISTER_IPSR], 0, "%d");

	/* Check panic flags. */
	TEST_EQ(pdata->flags,
		PANIC_DATA_FLAG_FRAME_VALID | PANIC_DATA_FLAG_OLD_HOSTEVENT,
		"%02x");

	return EC_SUCCESS;
}

void test_run_step(uint32_t state)
{
	/* Step 1: Crash system to get panic data. */
	if (state & TEST_STATE_MASK(TEST_STATE_STEP_1)) {
		test_set_next_step(TEST_STATE_STEP_2);
		/* Crash the system */
		ccprintf("Crash the system!\n");
		cflush();
		crash_system();
	}
	/* Step 2: Check panic data after crash and do soft reboot. */
	else if (state & TEST_STATE_MASK(TEST_STATE_STEP_2)) {
		RUN_TEST(test_panic_data);
		if (!test_get_error_count()) {
			test_set_next_step(TEST_STATE_STEP_3);
			/* Do a soft system reset */
			ccprintf("Perform soft reboot\n");
			cflush();
			system_reset(0);
		} else
			test_reboot_to_next_step(TEST_STATE_FAILED);
	}
	/* Step 3: Check panic data after soft reboot and do hard reboot. */
	else if (state & TEST_STATE_MASK(TEST_STATE_STEP_3)) {
		RUN_TEST(test_panic_data);
		if (!test_get_error_count()) {
			test_set_next_step(TEST_STATE_STEP_4);
			/* Do a hard system reset */
			ccprintf("Perform hard reboot\n");
			cflush();
			system_reset(SYSTEM_RESET_HARD);
		} else
			test_reboot_to_next_step(TEST_STATE_FAILED);
	}
	/* Step 4: Check panic data after hard reboot */
	else if (state & TEST_STATE_MASK(TEST_STATE_STEP_4)) {
		RUN_TEST(test_panic_data_half);
		if (!test_get_error_count())
			test_reboot_to_next_step(TEST_STATE_PASSED);
		else
			test_reboot_to_next_step(TEST_STATE_FAILED);
	}
}

int task_test(void *unused)
{
	if (IS_ENABLED(SECTION_IS_RW))
		test_run_multistep();

	return EC_SUCCESS;
}

void run_test(int argc, const char **argv)
{
	test_reset();
	crec_msleep(30); /* Wait for TASK_ID_TEST to initialize */
	task_wake(TASK_ID_TEST);
}
