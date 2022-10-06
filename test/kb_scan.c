/* Copyright 2013 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 * Copyright 2013 Google LLC
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

#define KEYDOWN_DELAY_MS 10
#define KEYDOWN_RETRY 10
#define NO_KEYDOWN_DELAY_MS 100

#define CHECK_KEY_COUNT(old, expected)                               \
	do {                                                         \
		if (verify_key_presses(old, expected) != EC_SUCCESS) \
			return EC_ERROR_UNKNOWN;                     \
		old = fifo_add_count;                                \
	} while (0)

/* Emulated physical key state */
static uint8_t mock_state[KEYBOARD_COLS_MAX];

/* Snapshot of last known key state */
static uint8_t key_state[KEYBOARD_COLS_MAX];

/* Counters for key state changes (UP/DOWN) */
static int key_state_change[KEYBOARD_COLS_MAX][KEYBOARD_ROWS];
static int total_key_state_change;

static int column_driven;
static int fifo_add_count;
static int lid_open;
#ifdef EMU_BUILD
static int hibernated;
static int reset_called;
#endif

/*
 * Helper method to wake a given task, and provide immediate opportunity to run.
 */
static void task_wake_then_sleep_1ms(int task_id)
{
	task_wake(task_id);
	msleep(1);
}

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
		for (i = 0; i < KEYBOARD_COLS_MAX; ++i)
			r |= mock_state[i];
		return r;
	} else {
		return mock_state[column_driven];
	}
}

int mkbp_keyboard_add(const uint8_t *buffp)
{
	int c, r;

	fifo_add_count++;

	for (c = 0; c < KEYBOARD_COLS_MAX; c++) {
		uint8_t diff = key_state[c] ^ buffp[c];

		for (r = 0; r < KEYBOARD_ROWS; r++) {
			if (diff & BIT(r)) {
				key_state_change[c][r]++;
				total_key_state_change++;
			}
		}
	}

	/* Save a snapshot. */
	memcpy(key_state, buffp, sizeof(key_state));

	return EC_SUCCESS;
}

#ifdef EMU_BUILD
void system_hibernate(uint32_t s, uint32_t us)
{
	hibernated = 1;
}

void chipset_reset(void)
{
	reset_called = 1;
}
#endif

#define mock_defined_key(k, p) mock_key(KEYBOARD_ROW_##k, KEYBOARD_COL_##k, p)

#define mock_default_key(k, p) \
	mock_key(KEYBOARD_DEFAULT_ROW_##k, KEYBOARD_DEFAULT_COL_##k, p)

static void mock_key(int r, int c, int keydown)
{
	ccprintf("  %s (%d, %d)\n", keydown ? "Pressing" : "Releasing", r, c);
	if (keydown)
		mock_state[c] |= (1 << r);
	else
		mock_state[c] &= ~(1 << r);
}

static void reset_key_state(void)
{
	memset(mock_state, 0, sizeof(mock_state));
	memset(key_state, 0, sizeof(key_state));
	memset(key_state_change, 0, sizeof(key_state_change));
	task_wake(TASK_ID_KEYSCAN);
	msleep(NO_KEYDOWN_DELAY_MS);
	total_key_state_change = 0;
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
	reset_key_state();

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

static int strict_debounce_test(void)
{
	reset_key_state();

	ccprintf("Test key press & hold.\n");
	mock_key(1, 1, 1);
	TEST_EQ(expect_keychange(), EC_SUCCESS, "%d");
	TEST_EQ(key_state_change[1][1], 1, "%d");
	TEST_EQ(total_key_state_change, 1, "%d");
	ccprintf("Pass.\n");

	reset_key_state();

	ccprintf("Test a short stroke.\n");
	mock_key(1, 1, 1);
	task_wake_then_sleep_1ms(TASK_ID_KEYSCAN);
	mock_key(1, 1, 0);
	task_wake_then_sleep_1ms(TASK_ID_KEYSCAN);
	TEST_EQ(expect_no_keychange(), EC_SUCCESS, "%d");
	TEST_EQ(key_state_change[1][1], 0, "%d");
	ccprintf("Pass.\n");

	reset_key_state();

	ccprintf("Test ripples being suppressed.\n");
	/* DOWN */
	mock_key(1, 1, 1);
	task_wake_then_sleep_1ms(TASK_ID_KEYSCAN);
	mock_key(1, 1, 0);
	task_wake_then_sleep_1ms(TASK_ID_KEYSCAN);
	mock_key(1, 1, 1);
	task_wake_then_sleep_1ms(TASK_ID_KEYSCAN);
	TEST_EQ(expect_keychange(), EC_SUCCESS, "%d");
	TEST_EQ(key_state_change[1][1], 1, "%d");
	TEST_EQ(total_key_state_change, 1, "%d");
	/* UP */
	mock_key(1, 1, 0);
	task_wake_then_sleep_1ms(TASK_ID_KEYSCAN);
	mock_key(1, 1, 1);
	task_wake_then_sleep_1ms(TASK_ID_KEYSCAN);
	mock_key(1, 1, 0);
	task_wake_then_sleep_1ms(TASK_ID_KEYSCAN);
	TEST_EQ(expect_keychange(), EC_SUCCESS, "%d");
	TEST_EQ(key_state_change[1][1], 2, "%d");
	TEST_EQ(total_key_state_change, 2, "%d");
	ccprintf("Pass.\n");

	reset_key_state();

	ccprintf("Test simultaneous strokes.\n");
	mock_key(1, 1, 1);
	mock_key(2, 1, 1);
	task_wake_then_sleep_1ms(TASK_ID_KEYSCAN);
	TEST_EQ(expect_keychange(), EC_SUCCESS, "%d");
	TEST_EQ(key_state_change[1][1], 1, "%d");
	TEST_EQ(key_state_change[1][2], 1, "%d");
	TEST_EQ(total_key_state_change, 2, "%d");
	ccprintf("Pass.\n");

	reset_key_state();

	ccprintf("Test simultaneous strokes in two columns.\n");
	mock_key(1, 1, 1);
	mock_key(1, 2, 1);
	task_wake_then_sleep_1ms(TASK_ID_KEYSCAN);
	TEST_EQ(expect_keychange(), EC_SUCCESS, "%d");
	TEST_EQ(key_state_change[1][1], 1, "%d");
	TEST_EQ(key_state_change[2][1], 1, "%d");
	TEST_EQ(total_key_state_change, 2, "%d");
	ccprintf("Pass.\n");

	reset_key_state();

	ccprintf("Test normal & short simultaneous strokes.\n");
	mock_key(1, 1, 1);
	task_wake_then_sleep_1ms(TASK_ID_KEYSCAN);
	mock_key(2, 1, 1);
	task_wake_then_sleep_1ms(TASK_ID_KEYSCAN);
	mock_key(1, 1, 0);
	task_wake_then_sleep_1ms(TASK_ID_KEYSCAN);
	TEST_EQ(expect_keychange(), EC_SUCCESS, "%d");
	TEST_EQ(key_state_change[1][1], 0, "%d");
	TEST_EQ(key_state_change[1][2], 1, "%d");
	TEST_EQ(total_key_state_change, 1, "%d");
	ccprintf("Pass.\n");

	reset_key_state();

	ccprintf("Test normal & short simultaneous strokes in two columns.\n");
	reset_key_state();
	mock_key(1, 1, 1);
	task_wake(TASK_ID_KEYSCAN);
	mock_key(1, 2, 1);
	task_wake(TASK_ID_KEYSCAN);
	mock_key(1, 1, 0);
	task_wake(TASK_ID_KEYSCAN);
	TEST_EQ(expect_keychange(), EC_SUCCESS, "%d");
	TEST_EQ(key_state_change[1][1], 0, "%d");
	TEST_EQ(key_state_change[2][1], 1, "%d");
	TEST_EQ(total_key_state_change, 1, "%d");
	ccprintf("Pass.\n");

	return EC_SUCCESS;
}

static int debounce_test(void)
{
	int old_count = fifo_add_count;
	int i;

	reset_key_state();

	/* One brief keypress is detected. */
	msleep(40);
	mock_key(1, 1, 1);
	task_wake_then_sleep_1ms(TASK_ID_KEYSCAN);
	mock_key(1, 1, 0);
	task_wake_then_sleep_1ms(TASK_ID_KEYSCAN);
	CHECK_KEY_COUNT(old_count, 2);

	/* Brief bounce, followed by continuous press is detected as one. */
	msleep(40);
	mock_key(1, 1, 1);
	task_wake_then_sleep_1ms(TASK_ID_KEYSCAN);
	mock_key(1, 1, 0);
	task_wake_then_sleep_1ms(TASK_ID_KEYSCAN);
	mock_key(1, 1, 1);
	task_wake_then_sleep_1ms(TASK_ID_KEYSCAN);
	CHECK_KEY_COUNT(old_count, 1);

	/* Brief lifting, then re-presseing is detected as new keypress. */
	msleep(40);
	mock_key(1, 1, 0);
	task_wake_then_sleep_1ms(TASK_ID_KEYSCAN);
	mock_key(1, 1, 1);
	task_wake_then_sleep_1ms(TASK_ID_KEYSCAN);
	CHECK_KEY_COUNT(old_count, 2);

	/* One bouncy re-contact while lifting is ignored. */
	msleep(40);
	mock_key(1, 1, 0);
	task_wake_then_sleep_1ms(TASK_ID_KEYSCAN);
	mock_key(1, 1, 1);
	task_wake_then_sleep_1ms(TASK_ID_KEYSCAN);
	mock_key(1, 1, 0);
	task_wake_then_sleep_1ms(TASK_ID_KEYSCAN);
	CHECK_KEY_COUNT(old_count, 1);

	/*
	 * Debounce interval of first key is not affected by continued
	 * activity of other keys.
	 */
	msleep(40);
	/* Push the first key */
	mock_key(0, 1, 0);
	task_wake_then_sleep_1ms(TASK_ID_KEYSCAN);
	/*
	 * Push down each subsequent key, until all 8 are pressed, each
	 * time bouncing the former one once.
	 */
	for (i = 1; i < 8; i++) {
		mock_key(i, 1, 1);
		task_wake(TASK_ID_KEYSCAN);
		msleep(3);
		mock_key(i - 1, 1, 0);
		task_wake(TASK_ID_KEYSCAN);
		msleep(1);
		mock_key(i - 1, 1, 1);
		task_wake(TASK_ID_KEYSCAN);
		msleep(1);
	}
	/* Verify that the bounces were. ignored */
	CHECK_KEY_COUNT(old_count, 8);
	/*
	 * Now briefly lift and re-press the first one, which should now be past
	 * its debounce interval
	 */
	mock_key(0, 1, 0);
	task_wake_then_sleep_1ms(TASK_ID_KEYSCAN);
	CHECK_KEY_COUNT(old_count, 1);
	mock_key(0, 1, 1);
	task_wake_then_sleep_1ms(TASK_ID_KEYSCAN);
	CHECK_KEY_COUNT(old_count, 1);
	/* For good measure, release all keys before proceeding. */
	for (i = 0; i < 8; i++)
		mock_key(i, 1, 0);
	task_wake_then_sleep_1ms(TASK_ID_KEYSCAN);

	return EC_SUCCESS;
}

static int simulate_key_test(void)
{
	int old_count;

	reset_key_state();

	task_wake(TASK_ID_KEYSCAN);
	msleep(40); /* Wait for debouncing to settle */

	old_count = fifo_add_count;
	host_command_simulate(1, 1, 1);
	TEST_ASSERT(fifo_add_count > old_count);
	msleep(40);
	old_count = fifo_add_count;
	host_command_simulate(1, 1, 0);
	TEST_ASSERT(fifo_add_count > old_count);
	msleep(40);

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
	reset_key_state();

	/* Alt-VolUp-H triggers system hibernation */
	mock_defined_key(LEFT_ALT, 1);
	mock_default_key(VOL_UP, 1);
	mock_defined_key(KEY_H, 1);
	TEST_ASSERT(wait_variable_set(&hibernated) == EC_SUCCESS);
	mock_defined_key(LEFT_ALT, 0);
	mock_default_key(VOL_UP, 0);
	mock_defined_key(KEY_H, 0);
	TEST_ASSERT(expect_keychange() == EC_SUCCESS);

	/* Alt-VolUp-R triggers chipset reset */
	mock_defined_key(RIGHT_ALT, 1);
	mock_default_key(VOL_UP, 1);
	mock_defined_key(KEY_R, 1);
	TEST_ASSERT(wait_variable_set(&reset_called) == EC_SUCCESS);
	mock_defined_key(RIGHT_ALT, 0);
	mock_default_key(VOL_UP, 0);
	mock_defined_key(KEY_R, 0);
	TEST_ASSERT(expect_keychange() == EC_SUCCESS);

	/* Must press exactly 3 keys to trigger runtime keys */
	mock_defined_key(LEFT_ALT, 1);
	mock_defined_key(KEY_H, 1);
	mock_defined_key(KEY_R, 1);
	mock_default_key(VOL_UP, 1);
	TEST_ASSERT(verify_variable_not_set(&hibernated) == EC_SUCCESS);
	TEST_ASSERT(verify_variable_not_set(&reset_called) == EC_SUCCESS);
	mock_default_key(VOL_UP, 0);
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
	reset_key_state();

	msleep(40); /* Allow debounce to settle */

	lid_open = 0;
	hook_notify(HOOK_LID_CHANGE);
	msleep(1); /* Allow hooks to run */
	mock_key(1, 1, 1);
	TEST_ASSERT(expect_no_keychange() == EC_SUCCESS);
	mock_key(1, 1, 0);
	TEST_ASSERT(expect_no_keychange() == EC_SUCCESS);

	lid_open = 1;
	hook_notify(HOOK_LID_CHANGE);
	msleep(1); /* Allow hooks to run */
	mock_key(1, 1, 1);
	TEST_ASSERT(expect_keychange() == EC_SUCCESS);
	mock_key(1, 1, 0);
	TEST_ASSERT(expect_keychange() == EC_SUCCESS);

	return EC_SUCCESS;
}
#endif

static int test_check_boot_esc(void)
{
	TEST_ASSERT(keyboard_scan_get_boot_keys() == BOOT_KEY_ESC);
	return EC_SUCCESS;
}

static int test_check_boot_down(void)
{
	TEST_ASSERT(keyboard_scan_get_boot_keys() == BOOT_KEY_DOWN_ARROW);
	return EC_SUCCESS;
}

void test_init(void)
{
	uint32_t state;

	system_get_scratchpad(&state);

	if (state & TEST_STATE_MASK(TEST_STATE_STEP_2)) {
		/* Power-F3-ESC */
		system_set_reset_flags(system_get_reset_flags() |
				       EC_RESET_FLAG_RESET_PIN);
		mock_key(KEYBOARD_ROW_ESC, KEYBOARD_COL_ESC, 1);
	} else if (state & TEST_STATE_MASK(TEST_STATE_STEP_3)) {
		/* Power-F3-Down */
		system_set_reset_flags(system_get_reset_flags() |
				       EC_RESET_FLAG_RESET_PIN);
		mock_key(6, 11, 1);
	}
}

static void run_test_step1(void)
{
	lid_open = 1;
	hook_notify(HOOK_LID_CHANGE);
	test_reset();

	RUN_TEST(deghost_test);

	if (IS_ENABLED(CONFIG_KEYBOARD_STRICT_DEBOUNCE))
		RUN_TEST(strict_debounce_test);
	else
		RUN_TEST(debounce_test);

	if (0) /* crbug.com/976974 */
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

void run_test(int argc, const char **argv)
{
	msleep(30); /* Wait for TASK_ID_TEST to initialize */
	task_wake(TASK_ID_TEST);
}
