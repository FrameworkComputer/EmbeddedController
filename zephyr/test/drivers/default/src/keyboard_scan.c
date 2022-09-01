/* Copyright 2022 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include <string.h>
#include <zephyr/shell/shell_dummy.h>
#include <zephyr/ztest.h>
#include <zephyr/drivers/emul.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/gpio/gpio_emul.h>
#include <emul/emul_kb_raw.h>

#include "console.h"
#include "keyboard_scan.h"
#include "test/drivers/test_mocks.h"
#include "test/drivers/test_state.h"

int emulate_keystate(int row, int col, int pressed)
{
	const struct device *dev = DEVICE_DT_GET(DT_NODELABEL(cros_kb_raw));

	return emul_kb_raw_set_kbstate(dev, row, col, pressed);
}

ZTEST(keyboard_scan, test_boot_key)
{
	const struct device *dev = DEVICE_DT_GET(DT_NODELABEL(cros_kb_raw));
	const int kb_cols = DT_PROP(DT_NODELABEL(cros_kb_raw), cols);

	emul_kb_raw_reset(dev);
	zassert_equal(keyboard_scan_get_boot_keys(), BOOT_KEY_NONE, NULL);

	/* Case 1: refresh + esc -> BOOT_KEY_ESC */
	emul_kb_raw_reset(dev);
	zassert_ok(emulate_keystate(KEYBOARD_ROW_REFRESH, KEYBOARD_COL_REFRESH,
				    true),
		   NULL);
	zassert_ok(emulate_keystate(KEYBOARD_ROW_ESC, KEYBOARD_COL_ESC, true),
		   NULL);
	keyboard_scan_init();
	zassert_equal(keyboard_scan_get_boot_keys(), BOOT_KEY_ESC, NULL);

	/*
	 * Case 1.5:
	 * GSC may hold ksi2 when power button is pressed, simulate this
	 * behavior and verify boot key detection again.
	 */
	zassert_true(IS_ENABLED(CONFIG_KEYBOARD_PWRBTN_ASSERTS_KSI2), NULL);
	for (int i = 0; i < kb_cols; i++) {
		zassert_ok(emulate_keystate(KEYBOARD_ROW_REFRESH, i, true),
			   NULL);
	}
	keyboard_scan_init();
	zassert_equal(keyboard_scan_get_boot_keys(), BOOT_KEY_ESC, NULL);

	/* Case 2: esc only -> BOOT_KEY_NONE */
	emul_kb_raw_reset(dev);
	zassert_ok(emulate_keystate(KEYBOARD_ROW_ESC, KEYBOARD_COL_ESC, true),
		   NULL);
	keyboard_scan_init();
	zassert_equal(keyboard_scan_get_boot_keys(), BOOT_KEY_NONE, NULL);

	/* Case 3: refresh + arrow down -> BOOT_KEY_DOWN_ARROW */
	emul_kb_raw_reset(dev);
	zassert_ok(emulate_keystate(KEYBOARD_ROW_REFRESH, KEYBOARD_COL_REFRESH,
				    true),
		   NULL);
	zassert_ok(emulate_keystate(KEYBOARD_ROW_DOWN, KEYBOARD_COL_DOWN, true),
		   NULL);
	keyboard_scan_init();
	zassert_equal(keyboard_scan_get_boot_keys(), BOOT_KEY_DOWN_ARROW, NULL);

	/* Case 4: refresh + L shift -> BOOT_KEY_LEFT_SHIFT */
	emul_kb_raw_reset(dev);
	zassert_ok(emulate_keystate(KEYBOARD_ROW_REFRESH, KEYBOARD_COL_REFRESH,
				    true),
		   NULL);
	zassert_ok(emulate_keystate(KEYBOARD_ROW_LEFT_SHIFT,
				    KEYBOARD_COL_LEFT_SHIFT, true),
		   NULL);
	keyboard_scan_init();
	zassert_equal(keyboard_scan_get_boot_keys(), BOOT_KEY_LEFT_SHIFT, NULL);

	/* Case 5: refresh + esc + other random key -> BOOT_KEY_NONE */
	emul_kb_raw_reset(dev);
	zassert_ok(emulate_keystate(KEYBOARD_ROW_REFRESH, KEYBOARD_COL_REFRESH,
				    true),
		   NULL);
	zassert_ok(emulate_keystate(KEYBOARD_ROW_ESC, KEYBOARD_COL_ESC, true),
		   NULL);
	zassert_ok(emulate_keystate(KEYBOARD_ROW_KEY_0, KEYBOARD_COL_KEY_0,
				    true),
		   NULL);
	keyboard_scan_init();
	zassert_equal(keyboard_scan_get_boot_keys(), BOOT_KEY_NONE, NULL);

	/* Case 6: BOOT_KEY_NONE after late sysjump */
	system_jumped_late_fake.return_val = 1;
	emul_kb_raw_reset(dev);
	zassert_ok(emulate_keystate(KEYBOARD_ROW_REFRESH, KEYBOARD_COL_REFRESH,
				    true),
		   NULL);
	zassert_ok(emulate_keystate(KEYBOARD_ROW_LEFT_SHIFT,
				    KEYBOARD_COL_LEFT_SHIFT, true),
		   NULL);
	keyboard_scan_init();
	zassert_equal(keyboard_scan_get_boot_keys(), BOOT_KEY_NONE, NULL);
}

ZTEST(keyboard_scan, test_press_enter)
{
	zassert_ok(emulate_keystate(4, 11, true), NULL);
	k_sleep(K_MSEC(100));
	/* TODO(jbettis): Check espi_emul to verify the AP was notified. */
	zassert_ok(emulate_keystate(4, 11, false), NULL);
	k_sleep(K_MSEC(100));
}

ZTEST(keyboard_scan, console_command_ksstate__noargs)
{
	const char *outbuffer;
	size_t buffer_size;

	/* With no args, print current state */
	shell_backend_dummy_clear_output(get_ec_shell());
	zassert_ok(shell_execute_cmd(get_ec_shell(), "ksstate"), NULL);
	outbuffer =
		shell_backend_dummy_get_output(get_ec_shell(), &buffer_size);

	/* Check for some expected lines */
	zassert_true(buffer_size > 0, NULL);
	zassert_ok(!strstr(outbuffer, "Keyboard scan disable mask: 0x00000000"),
		   "Output was: `%s`", outbuffer);
	zassert_ok(!strstr(outbuffer, "Keyboard scan state printing off"),
		   "Output was: `%s`", outbuffer);

	/* Ensure we are still scanning */
	zassert_true(keyboard_scan_is_enabled(), NULL);
}

ZTEST(keyboard_scan, console_command_ksstate__force)
{
	/* This command forces the keyboard to start scanning (if not already)
	 * and enable state change printing. To test: turn scanning off, run
	 * command, and verify we are scanning and printing state
	 */

	keyboard_scan_enable(false, -1);
	zassume_false(keyboard_scan_is_enabled(), NULL);

	zassert_ok(shell_execute_cmd(get_ec_shell(), "ksstate force"), NULL);

	zassert_true(keyboard_scan_is_enabled(), NULL);
	zassert_true(keyboard_scan_get_print_state_changes(), NULL);
}

ZTEST(keyboard_scan, console_command_ksstate__on_off)
{
	/* This command turns state change printing on/off */

	zassume_false(keyboard_scan_get_print_state_changes(), NULL);

	zassert_ok(shell_execute_cmd(get_ec_shell(), "ksstate on"), NULL);
	zassert_true(keyboard_scan_get_print_state_changes(), NULL);

	zassert_ok(shell_execute_cmd(get_ec_shell(), "ksstate off"), NULL);
	zassert_false(keyboard_scan_get_print_state_changes(), NULL);
}

ZTEST(keyboard_scan, console_command_ksstate__invalid)
{
	/* Pass a string that cannot be parsed as a bool */
	zassert_ok(!shell_execute_cmd(get_ec_shell(), "ksstate xyz"), NULL);
}

static void reset_keyboard(void *data)
{
	ARG_UNUSED(data);

	/* Enable scanning and clear all reason bits (reason bits explain why
	 * scanning was disabled -- see `enum kb_scan_disable_masks`)
	 */
	keyboard_scan_enable(true, -1);

	/* Turn off key state change printing */
	keyboard_scan_set_print_state_changes(0);
}

ZTEST_SUITE(keyboard_scan, drivers_predicate_post_main, NULL, reset_keyboard,
	    reset_keyboard, NULL);
