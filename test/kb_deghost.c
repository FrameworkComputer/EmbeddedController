/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 * Copyright 2013 Google Inc.
 *
 * Tasks for keyboard scan deghosting.
 */

#include "common.h"
#include "console.h"
#include "keyboard_raw.h"
#include "keyboard_scan.h"
#include "task.h"
#include "timer.h"
#include "util.h"

#define KEYDOWN_DELAY_MS     10
#define KEYDOWN_RETRY        10
#define NO_KEYDOWN_DELAY_MS  200

#define TEST(n) \
	do { \
		if (!n()) { \
			ccprintf("%s failed.\n", #n); \
			return EC_ERROR_UNKNOWN; \
		} \
	} while (0)

uint8_t mock_state[KEYBOARD_COLS];
int column_driven;
int fifo_add_count;

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
			return 1;
	}
	return 0;
}

int expect_no_keychange(void)
{
	int old_count = fifo_add_count;
	task_wake(TASK_ID_KEYSCAN);
	msleep(NO_KEYDOWN_DELAY_MS);
	return fifo_add_count == old_count;
}

int deghost_test(void)
{
	/* Test we can detect a keypress */
	mock_key(1, 1, 1);
	TEST(expect_keychange);
	mock_key(1, 1, 0);
	TEST(expect_keychange);

	/* (1, 1) (1, 2) (2, 1) (2, 2) form ghosting keys */
	mock_key(1, 1, 1);
	TEST(expect_keychange);
	mock_key(2, 2, 1);
	TEST(expect_keychange);
	mock_key(1, 2, 1);
	mock_key(2, 1, 1);
	TEST(expect_no_keychange);
	mock_key(2, 1, 0);
	mock_key(1, 2, 0);
	TEST(expect_no_keychange);
	mock_key(2, 2, 0);
	TEST(expect_keychange);
	mock_key(1, 1, 0);
	TEST(expect_keychange);

	/* (1, 1) (2, 0) (2, 1) don't form ghosting keys */
	mock_key(1, 1, 1);
	TEST(expect_keychange);
	mock_key(2, 0, 1);
	TEST(expect_keychange);
	mock_key(1, 0, 1);
	mock_key(2, 1, 1);
	TEST(expect_keychange);
	mock_key(1, 0, 0);
	mock_key(2, 1, 0);
	TEST(expect_keychange);
	mock_key(2, 0, 0);
	TEST(expect_keychange);
	mock_key(1, 1, 0);
	TEST(expect_keychange);

	return EC_SUCCESS;
}

static int command_run_test(int argc, char **argv)
{
	int r = deghost_test();
	if (r == EC_SUCCESS)
		ccprintf("Pass!\n");
	else
		ccprintf("Fail!\n");
	return r;
}
DECLARE_CONSOLE_COMMAND(runtest, command_run_test,
			NULL, NULL, NULL);
