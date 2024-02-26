/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "ec_commands.h"
#include "system.h"
#include "task.h"
#include "test_util.h"

#include <stdint.h>
#include <string.h>

int system_is_locked(void)
{
	/* return system_is_locked = 0 for the first run step i.e. state = 0
	 * (test_system_is_not_locked) and system_is_locked = 1 for all the
	 * other states.
	 */
	return test_get_state() != 0;
}

void hello_function(void)
{
	ccprints("Hello World!");
}

void bye_function(void)
{
	ccprints("Bye World!");
}

test_static int test_system_is_not_locked(void)
{
	TEST_EQ(system_is_locked(), 0, "%d");

	ccprints("Running hello_function.");
	hello_function();

	/* Copy No-op instruction. */
	uint16_t noop_instruction = 0xbf00;

	memcpy(&hello_function, &noop_instruction, 2);

	uint16_t instruction_copy = 0;

	memcpy(&instruction_copy, &hello_function, 2);
	TEST_EQ(instruction_copy, noop_instruction, "0x%04x");

	return EC_SUCCESS;
}

test_static int test_system_is_locked(void)
{
	TEST_EQ(system_is_locked(), 1, "%d");

	ccprints("Running bye_function.");
	bye_function();

	/* Copy No-op instruction. */
	uint16_t noop_instruction = 0xbf00;

	memcpy(&hello_function, &noop_instruction, 2);

	uint16_t instruction_copy = 0;

	/* This should cause a reboot. */
	memcpy(&instruction_copy, &hello_function, 2);
	TEST_EQ(instruction_copy, noop_instruction, "0x%04x");

	return EC_SUCCESS;
}

test_static void run_test_step1(void)
{
	ccprints("Step 1: Run before System is Locked");
	cflush();

	RUN_TEST(test_system_is_not_locked);

	if (test_get_error_count()) {
		test_reboot_to_next_step(TEST_STATE_FAILED);
	} else {
		test_reboot_to_next_step(TEST_STATE_STEP_2);
	}
}

test_static void run_test_step2(void)
{
	ccprints("Step 2: Run after System is locked");
	cflush();

	test_set_next_step(TEST_STATE_PASSED);
	RUN_TEST(test_system_is_locked);

	if (test_get_error_count()) {
	} else {
		test_set_next_step(TEST_STATE_FAILED);
	}
}

void test_run_step(uint32_t state)
{
	if (state & TEST_STATE_MASK(TEST_STATE_STEP_1)) {
		run_test_step1();
	} else if (state & TEST_STATE_MASK(TEST_STATE_STEP_2)) {
		run_test_step2();
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
	crec_msleep(100); /* Wait for TASK_ID_TEST to initialize */
	task_wake(TASK_ID_TEST);
}
