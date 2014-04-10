/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 * Copyright 2013 Google Inc.
 *
 * Tests for keyboard scan deghosting and debouncing.
 */

#include "common.h"
#include "console.h"
#include "gpio.h"
#include "hooks.h"
#include "host_command.h"
#include "keyboard_raw.h"
#include "keyboard_scan.h"
#include "lid_switch.h"
#include "system.h"
#include "task.h"
#include "test_util.h"
#include "timer.h"
#include "util.h"

#define KEYDOWN_DELAY_MS     10
#define KEYDOWN_RETRY        10
#define NO_KEYDOWN_DELAY_MS  100

#define CHECK_KEY_COUNT(old, expected) \
	do { \
		if (verify_key_presses(old, expected) != EC_SUCCESS) \
			return EC_ERROR_UNKNOWN; \
		old = fifo_add_count; \
	} while (0)

static uint8_t mock_state[KEYBOARD_COLS];
static int column_driven;
static int fifo_add_count;
static int lid_open;
#ifdef EMU_BUILD
static int hibernated;
static int reset_called;
#endif

#ifdef CONFIG_LID_SWITCH
int lid_is_open(void)
{
	return lid_open;
}
#endif

void keyboard_raw_drive_column(int out)
{
	column_driven = out;
}

int keyboard_raw_read_rows(void)
{
	int i;
	int r = 0;

	if (column_driven == KEYBOARD_COLUMN_NONE) {
		return 0;
	} else if (column_driven == KEYBOARD_COLUMN_ALL) {
		for (i = 0; i < KEYBOARD_COLS; ++i)
			r |= mock_state[i];
		return r;
	} else {
		return mock_state[column_driven];
	}
}

int keyboard_fifo_add(const uint8_t *buffp)
{
	fifo_add_count++;
	return EC_SUCCESS;
}

#ifdef EMU_BUILD
void system_hibernate(uint32_t s, uint32_t us)
{
	hibernated = 1;
}

void chipset_reset(int cold_reset)
{
	reset_called = 1;
}
#endif

#define mock_defined_key(k, p) mock_key(KEYBOARD_ROW_ ## k, \
					KEYBOARD_COL_ ## k, \
					p)

static void mock_key(int r, int c, int keydown)
{
	ccprintf("%s (%d, %d)\n", keydown ? "Pressing" : "Releasing", r, c);
	if (keydown)
		mock_state[c] |= (1 << r);
	else
		mock_state[c] &= ~(1 << r);
}

static int expect_keychange(void)
{
	int old_count = fifo_add_count;
	int retry = KEYDOWN_RETRY;
	task_wake(TASK_ID_KEYSCAN);
	while (retry--) {
		msleep(KEYDOWN_DELAY_MS);
		if (fifo_add_count > old_count)
			return EC_SUCCESS;
	}
	return EC_ERROR_UNKNOWN;
}

static int expect_no_keychange(void)
{
	int old_count = fifo_add_count;
	task_wake(TASK_ID_KEYSCAN);
	msleep(NO_KEYDOWN_DELAY_MS);
	return (fifo_add_count == old_count) ? EC_SUCCESS : EC_ERROR_UNKNOWN;
}

static int host_command_simulate(int r, int c, int keydown)
{
	struct ec_params_mkbp_simulate_key params;

	params.col = c;
	params.row = r;
	params.pressed = keydown;

	return test_send_host_command(EC_CMD_MKBP_SIMULATE_KEY, 0, &params,
				      sizeof(params), NULL, 0);
}

static int verify_key_presses(int old, int expected)
{
	int retry = KEYDOWN_RETRY;

	if (expected == 0) {
		msleep(NO_KEYDOWN_DELAY_MS);
		return (fifo_add_count == old) ? EC_SUCCESS : EC_ERROR_UNKNOWN;
	} else {
		while (retry--) {
			msleep(KEYDOWN_DELAY_MS);
			if (fifo_add_count == old + expected)
				return EC_SUCCESS;
		}
		return EC_ERROR_UNKNOWN;
	}
}

static int deghost_test(void)
{
	/* Test we can detect a keypress */
	mock_key(1, 1, 1);
	TEST_ASSERT(expect_keychange() == EC_SUCCESS);
	mock_key(1, 1, 0);
	TEST_ASSERT(expect_keychange() == EC_SUCCESS);

	/* (1, 1) (1, 2) (2, 1) (2, 2) form ghosting keys */
	mock_key(1, 1, 1);
	TEST_ASSERT(expect_keychange() == EC_SUCCESS);
	mock_key(2, 2, 1);
	TEST_ASSERT(expect_keychange() == EC_SUCCESS);
	mock_key(1, 2, 1);
	mock_key(2, 1, 1);
	TEST_ASSERT(expect_no_keychange() == EC_SUCCESS);
	mock_key(2, 1, 0);
	mock_key(1, 2, 0);
	TEST_ASSERT(expect_no_keychange() == EC_SUCCESS);
	mock_key(2, 2, 0);
	TEST_ASSERT(expect_keychange() == EC_SUCCESS);
	mock_key(1, 1, 0);
	TEST_ASSERT(expect_keychange() == EC_SUCCESS);

	/* (1, 1) (2, 0) (2, 1) don't form ghosting keys */
	mock_key(1, 1, 1);
	TEST_ASSERT(expect_keychange() == EC_SUCCESS);
	mock_key(2, 0, 1);
	TEST_ASSERT(expect_keychange() == EC_SUCCESS);
	mock_key(1, 0, 1);
	mock_key(2, 1, 1);
	TEST_ASSERT(expect_keychange() == EC_SUCCESS);
	mock_key(1, 0, 0);
	mock_key(2, 1, 0);
	TEST_ASSERT(expect_keychange() == EC_SUCCESS);
	mock_key(2, 0, 0);
	TEST_ASSERT(expect_keychange() == EC_SUCCESS);
	mock_key(1, 1, 0);
	TEST_ASSERT(expect_keychange() == EC_SUCCESS);

	return EC_SUCCESS;
}

static int debounce_test(void)
{
	int old_count = fifo_add_count;
	mock_key(1, 1, 1);
	task_wake(TASK_ID_KEYSCAN);
	mock_key(1, 1, 0);
	task_wake(TASK_ID_KEYSCAN);
	CHECK_KEY_COUNT(old_count, 0);

	mock_key(1, 1, 1);
	task_wake(TASK_ID_KEYSCAN);
	mock_key(1, 1, 0);
	task_wake(TASK_ID_KEYSCAN);
	mock_key(1, 1, 1);
	task_wake(TASK_ID_KEYSCAN);
	CHECK_KEY_COUNT(old_count, 1);

	mock_key(1, 1, 0);
	task_wake(TASK_ID_KEYSCAN);
	mock_key(1, 1, 1);
	task_wake(TASK_ID_KEYSCAN);
	CHECK_KEY_COUNT(old_count, 0);

	mock_key(2, 2, 1);
	task_wake(TASK_ID_KEYSCAN);
	mock_key(2, 2, 0);
	task_wake(TASK_ID_KEYSCAN);
	CHECK_KEY_COUNT(old_count, 0);

	mock_key(2, 2, 1);
	task_wake(TASK_ID_KEYSCAN);
	mock_key(2, 2, 0);
	task_wake(TASK_ID_KEYSCAN);
	mock_key(2, 2, 1);
	task_wake(TASK_ID_KEYSCAN);
	CHECK_KEY_COUNT(old_count, 1);

	mock_key(1, 1, 0);
	task_wake(TASK_ID_KEYSCAN);
	mock_key(1, 1, 1);
	task_wake(TASK_ID_KEYSCAN);
	mock_key(1, 1, 0);
	task_wake(TASK_ID_KEYSCAN);
	CHECK_KEY_COUNT(old_count, 1);

	mock_key(2, 2, 0);
	task_wake(TASK_ID_KEYSCAN);
	mock_key(2, 2, 1);
	task_wake(TASK_ID_KEYSCAN);
	mock_key(2, 2, 0);
	task_wake(TASK_ID_KEYSCAN);
	mock_key(2, 2, 1);
	task_wake(TASK_ID_KEYSCAN);
	mock_key(2, 2, 0);
	task_wake(TASK_ID_KEYSCAN);
	CHECK_KEY_COUNT(old_count, 1);

	return EC_SUCCESS;
}

static int simulate_key_test(void)
{
	int old_count;

	old_count = fifo_add_count;
	host_command_simulate(1, 1, 1);
	TEST_ASSERT(fifo_add_count > old_count);
	old_count = fifo_add_count;
	host_command_simulate(1, 1, 0);
	TEST_ASSERT(fifo_add_count > old_count);

	return EC_SUCCESS;
}

#ifdef EMU_BUILD
static int wait_variable_set(int *var)
{
	int retry = KEYDOWN_RETRY;
	*var = 0;
	task_wake(TASK_ID_KEYSCAN);
	while (retry--) {
		msleep(KEYDOWN_DELAY_MS);
		if (*var == 1)
			return EC_SUCCESS;
	}
	return EC_ERROR_UNKNOWN;
}

static int verify_variable_not_set(int *var)
{
	*var = 0;
	task_wake(TASK_ID_KEYSCAN);
	msleep(NO_KEYDOWN_DELAY_MS);
	return *var ? EC_ERROR_UNKNOWN : EC_SUCCESS;
}

static int runtime_key_test(void)
{
	/* Alt-VolUp-H triggers system hibernation */
	mock_defined_key(LEFT_ALT, 1);
	mock_defined_key(VOL_UP, 1);
	mock_defined_key(KEY_H, 1);
	TEST_ASSERT(wait_variable_set(&hibernated) == EC_SUCCESS);
	mock_defined_key(LEFT_ALT, 0);
	mock_defined_key(VOL_UP, 0);
	mock_defined_key(KEY_H, 0);
	TEST_ASSERT(expect_keychange() == EC_SUCCESS);

	/* Alt-VolUp-R triggers chipset reset */
	mock_defined_key(RIGHT_ALT, 1);
	mock_defined_key(VOL_UP, 1);
	mock_defined_key(KEY_R, 1);
	TEST_ASSERT(wait_variable_set(&reset_called) == EC_SUCCESS);
	mock_defined_key(RIGHT_ALT, 0);
	mock_defined_key(VOL_UP, 0);
	mock_defined_key(KEY_R, 0);
	TEST_ASSERT(expect_keychange() == EC_SUCCESS);

	/* Must press exactly 3 keys to trigger runtime keys */
	mock_defined_key(LEFT_ALT, 1);
	mock_defined_key(KEY_H, 1);
	mock_defined_key(KEY_R, 1);
	mock_defined_key(VOL_UP, 1);
	TEST_ASSERT(verify_variable_not_set(&hibernated) == EC_SUCCESS);
	TEST_ASSERT(verify_variable_not_set(&reset_called) == EC_SUCCESS);
	mock_defined_key(VOL_UP, 0);
	mock_defined_key(KEY_R, 0);
	mock_defined_key(KEY_H, 0);
	mock_defined_key(LEFT_ALT, 0);
	TEST_ASSERT(expect_keychange() == EC_SUCCESS);

	return EC_SUCCESS;
}
#endif

#ifdef CONFIG_LID_SWITCH
static int lid_test(void)
{
	lid_open = 0;
	hook_notify(HOOK_LID_CHANGE);
	mock_key(1, 1, 1);
	TEST_ASSERT(expect_no_keychange() == EC_SUCCESS);
	mock_key(1, 1, 0);
	TEST_ASSERT(expect_no_keychange() == EC_SUCCESS);

	lid_open = 1;
	hook_notify(HOOK_LID_CHANGE);
	mock_key(1, 1, 1);
	TEST_ASSERT(expect_keychange() == EC_SUCCESS);
	mock_key(1, 1, 0);
	TEST_ASSERT(expect_keychange() == EC_SUCCESS);

	return EC_SUCCESS;
}
#endif

static int test_check_boot_esc(void)
{
	TEST_CHECK(keyboard_scan_get_boot_key() == BOOT_KEY_ESC);
}

static int test_check_boot_down(void)
{
	TEST_CHECK(keyboard_scan_get_boot_key() == BOOT_KEY_DOWN_ARROW);
}

void test_init(void)
{
	uint32_t state = system_get_scratchpad();

	if (state & TEST_STATE_MASK(TEST_STATE_STEP_2)) {
		/* Power-F3-ESC */
		system_set_reset_flags(system_get_reset_flags() |
				       RESET_FLAG_RESET_PIN);
		mock_key(1, 1, 1);
	} else if (state & TEST_STATE_MASK(TEST_STATE_STEP_3)) {
		/* Power-F3-Down */
		system_set_reset_flags(system_get_reset_flags() |
				       RESET_FLAG_RESET_PIN);
		mock_key(6, 11, 1);
	}
}

static void run_test_step1(void)
{
	lid_open = 1;
	hook_notify(HOOK_LID_CHANGE);
	test_reset();

	RUN_TEST(deghost_test);
	RUN_TEST(debounce_test);
	RUN_TEST(simulate_key_test);
#ifdef EMU_BUILD
	RUN_TEST(runtime_key_test);
#endif
#ifdef CONFIG_LID_SWITCH
	RUN_TEST(lid_test);
#endif

	if (test_get_error_count())
		test_reboot_to_next_step(TEST_STATE_FAILED);
	else
		test_reboot_to_next_step(TEST_STATE_STEP_2);
}

static void run_test_step2(void)
{
	lid_open = 1;
	hook_notify(HOOK_LID_CHANGE);
	test_reset();

	RUN_TEST(test_check_boot_esc);

	if (test_get_error_count())
		test_reboot_to_next_step(TEST_STATE_FAILED);
	else
		test_reboot_to_next_step(TEST_STATE_STEP_3);
}

static void run_test_step3(void)
{
	lid_open = 1;
	hook_notify(HOOK_LID_CHANGE);
	test_reset();

	RUN_TEST(test_check_boot_down);

	if (test_get_error_count())
		test_reboot_to_next_step(TEST_STATE_FAILED);
	else
		test_reboot_to_next_step(TEST_STATE_PASSED);
}

void test_run_step(uint32_t state)
{
	if (state & TEST_STATE_MASK(TEST_STATE_STEP_1))
		run_test_step1();
	else if (state & TEST_STATE_MASK(TEST_STATE_STEP_2))
		run_test_step2();
	else if (state & TEST_STATE_MASK(TEST_STATE_STEP_3))
		run_test_step3();
}

int test_task(void *data)
{
	test_run_multistep();
	return EC_SUCCESS;
}

void run_test(void)
{
	msleep(30); /* Wait for TASK_ID_TEST to initialize */
	task_wake(TASK_ID_TEST);
}
