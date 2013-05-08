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
#include "keyboard_raw.h"
#include "keyboard_scan.h"
#include "lid_switch.h"
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

void mock_key(int r, int c, int keydown)
{
	ccprintf("%s (%d, %d)\n", keydown ? "Pressing" : "Releasing", r, c);
	if (keydown)
		mock_state[c] |= (1 << r);
	else
		mock_state[c] &= ~(1 << r);
}

int expect_keychange(void)
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

int expect_no_keychange(void)
{
	int old_count = fifo_add_count;
	task_wake(TASK_ID_KEYSCAN);
	msleep(NO_KEYDOWN_DELAY_MS);
	return (fifo_add_count == old_count) ? EC_SUCCESS : EC_ERROR_UNKNOWN;
}

int verify_key_presses(int old, int expected)
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

int deghost_test(void)
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

int debounce_test(void)
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

#ifdef CONFIG_LID_SWITCH
int lid_test(void)
{
	lid_open = 0;
	mock_key(1, 1, 1);
	TEST_ASSERT(expect_no_keychange() == EC_SUCCESS);
	mock_key(1, 1, 0);
	TEST_ASSERT(expect_no_keychange() == EC_SUCCESS);

	lid_open = 1;
	mock_key(1, 1, 1);
	TEST_ASSERT(expect_keychange() == EC_SUCCESS);
	mock_key(1, 1, 0);
	TEST_ASSERT(expect_keychange() == EC_SUCCESS);

	return EC_SUCCESS;
}
#endif

void run_test(void)
{
	lid_open = 1;
	test_reset();

	RUN_TEST(deghost_test);
	RUN_TEST(debounce_test);
#ifdef CONFIG_LID_SWITCH
	RUN_TEST(lid_test);
#endif

	test_print_result();
}
