/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "keyboard_protocol.h"

#include <zephyr/fff.h>
#include <zephyr/input/input_keymap.h>
#include <zephyr/sys/util.h>
#include <zephyr/ztest.h>

#include <dt-bindings/kbd.h>

DEFINE_FFF_GLOBALS;

FAKE_VOID_FUNC(keyboard_state_changed_process, int, int, int, int);

#define FN_KEYS_NODE DT_NODELABEL(fn_keys)

#define FN_ROW KBD_RC_ROW(DT_PROP(FN_KEYS_NODE, fn_rc))
#define FN_COL KBD_RC_COL(DT_PROP(FN_KEYS_NODE, fn_rc))

#define F1_ROW MATRIX_ROW(DT_PROP_BY_IDX(FN_KEYS_NODE, keymap, 0))
#define F1_COL MATRIX_COL(DT_PROP_BY_IDX(FN_KEYS_NODE, keymap, 0))
#define F1_CODE (DT_PROP_BY_IDX(FN_KEYS_NODE, keymap, 0) & 0xffff)

void assert_last_state(int row, int col, int is_pressed, int override)
{
	zassert_equal(keyboard_state_changed_process_fake.call_count, 1);
	zassert_equal(keyboard_state_changed_process_fake.arg0_history[0], row);
	zassert_equal(keyboard_state_changed_process_fake.arg1_history[0], col);
	zassert_equal(keyboard_state_changed_process_fake.arg2_history[0],
		      is_pressed);
	zassert_equal(keyboard_state_changed_process_fake.arg3_history[0],
		      override);

	RESET_FAKE(keyboard_state_changed_process);
}

ZTEST(keyboard_fn, test_unrelated_key)
{
	keyboard_state_changed(0, 11, 1);
	assert_last_state(0, 11, 1, -1);

	keyboard_state_changed(0, 11, 0);
	assert_last_state(0, 11, 0, -1);
}

ZTEST(keyboard_fn, test_fn_unrelated_key)
{
	/* No codes emitted when pressing Fn and a non mapped key */
	keyboard_state_changed(FN_ROW, FN_COL, 1);
	zassert_equal(keyboard_state_changed_process_fake.call_count, 0);

	keyboard_state_changed(0, 11, 1);
	zassert_equal(keyboard_state_changed_process_fake.call_count, 0);

	keyboard_state_changed(0, 11, 0);
	zassert_equal(keyboard_state_changed_process_fake.call_count, 0);

	keyboard_state_changed(FN_ROW, FN_COL, 0);
	zassert_equal(keyboard_state_changed_process_fake.call_count, 0);
}

ZTEST(keyboard_fn, test_fn_alone)
{
	keyboard_state_changed(FN_ROW, FN_COL, 1);
	zassert_equal(keyboard_state_changed_process_fake.call_count, 0);

	keyboard_state_changed(FN_ROW, FN_COL, 0);
	zassert_equal(keyboard_state_changed_process_fake.call_count, 2);

	zassert_equal(keyboard_state_changed_process_fake.arg0_history[0],
		      FN_ROW);
	zassert_equal(keyboard_state_changed_process_fake.arg1_history[0],
		      FN_COL);
	zassert_equal(keyboard_state_changed_process_fake.arg2_history[0], 1);
	zassert_equal(keyboard_state_changed_process_fake.arg3_history[0], -1);

	zassert_equal(keyboard_state_changed_process_fake.arg0_history[1],
		      FN_ROW);
	zassert_equal(keyboard_state_changed_process_fake.arg1_history[1],
		      FN_COL);
	zassert_equal(keyboard_state_changed_process_fake.arg2_history[1], 0);
	zassert_equal(keyboard_state_changed_process_fake.arg3_history[1], -1);
}

ZTEST(keyboard_fn, test_fn_f1_f1_fn)
{
	/* Press and release F1 while olding Fn */

	keyboard_state_changed(FN_ROW, FN_COL, 1);
	zassert_equal(keyboard_state_changed_process_fake.call_count, 0);

	keyboard_state_changed(F1_ROW, F1_COL, 1);
	assert_last_state(F1_ROW, F1_COL, 1, F1_CODE);

	keyboard_state_changed(F1_ROW, F1_COL, 0);
	assert_last_state(F1_ROW, F1_COL, 0, F1_CODE);

	keyboard_state_changed(FN_ROW, FN_COL, 0);
	zassert_equal(keyboard_state_changed_process_fake.call_count, 0);
}

ZTEST(keyboard_fn, test_fn_f1_fn_f1)
{
	/* Press Fn, press F1, release Fn, release F1 */

	keyboard_state_changed(FN_ROW, FN_COL, 1);
	zassert_equal(keyboard_state_changed_process_fake.call_count, 0);

	keyboard_state_changed(F1_ROW, F1_COL, 1);
	assert_last_state(F1_ROW, F1_COL, 1, F1_CODE);

	keyboard_state_changed(FN_ROW, FN_COL, 0);
	zassert_equal(keyboard_state_changed_process_fake.call_count, 0);

	keyboard_state_changed(F1_ROW, F1_COL, 0);
	assert_last_state(F1_ROW, F1_COL, 0, F1_CODE);
}

ZTEST(keyboard_fn, test_f1_fn_f1_fn)
{
	/* Press F1, press Fn, release F1, release Fn */

	keyboard_state_changed(F1_ROW, F1_COL, 1);
	assert_last_state(F1_ROW, F1_COL, 1, -1);

	keyboard_state_changed(FN_ROW, FN_COL, 1);
	zassert_equal(keyboard_state_changed_process_fake.call_count, 0);

	keyboard_state_changed(F1_ROW, F1_COL, 0);
	assert_last_state(F1_ROW, F1_COL, 0, -1);

	keyboard_state_changed(FN_ROW, FN_COL, 0);
	zassert_equal(keyboard_state_changed_process_fake.arg0_history[0],
		      FN_ROW);
	zassert_equal(keyboard_state_changed_process_fake.arg1_history[0],
		      FN_COL);
	zassert_equal(keyboard_state_changed_process_fake.arg2_history[0], 1);
	zassert_equal(keyboard_state_changed_process_fake.arg3_history[0], -1);

	zassert_equal(keyboard_state_changed_process_fake.arg0_history[1],
		      FN_ROW);
	zassert_equal(keyboard_state_changed_process_fake.arg1_history[1],
		      FN_COL);
	zassert_equal(keyboard_state_changed_process_fake.arg2_history[1], 0);
	zassert_equal(keyboard_state_changed_process_fake.arg3_history[1], -1);
	RESET_FAKE(keyboard_state_changed_process);
}

ZTEST(keyboard_fn, test_f1_fn_fn_f1)
{
	/* Press F1, press and release Fn, release F1 */

	keyboard_state_changed(F1_ROW, F1_COL, 1);
	assert_last_state(F1_ROW, F1_COL, 1, -1);

	keyboard_state_changed(FN_ROW, FN_COL, 1);
	zassert_equal(keyboard_state_changed_process_fake.call_count, 0);

	keyboard_state_changed(FN_ROW, FN_COL, 0);
	zassert_equal(keyboard_state_changed_process_fake.arg0_history[0],
		      FN_ROW);
	zassert_equal(keyboard_state_changed_process_fake.arg1_history[0],
		      FN_COL);
	zassert_equal(keyboard_state_changed_process_fake.arg2_history[0], 1);
	zassert_equal(keyboard_state_changed_process_fake.arg3_history[0], -1);

	zassert_equal(keyboard_state_changed_process_fake.arg0_history[1],
		      FN_ROW);
	zassert_equal(keyboard_state_changed_process_fake.arg1_history[1],
		      FN_COL);
	zassert_equal(keyboard_state_changed_process_fake.arg2_history[1], 0);
	zassert_equal(keyboard_state_changed_process_fake.arg3_history[1], -1);
	RESET_FAKE(keyboard_state_changed_process);

	keyboard_state_changed(F1_ROW, F1_COL, 0);
	assert_last_state(F1_ROW, F1_COL, 0, -1);
}

static void reset(void *fixture)
{
	ARG_UNUSED(fixture);

	RESET_FAKE(keyboard_state_changed_process);
}

ZTEST_SUITE(keyboard_fn, NULL, NULL, reset, reset, NULL);
