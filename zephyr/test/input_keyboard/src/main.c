/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "keyboard_scan.h"

#include <zephyr/device.h>
#include <zephyr/fff.h>
#include <zephyr/input/input.h>
#include <zephyr/sys/util.h>
#include <zephyr/ztest.h>

DEFINE_FFF_GLOBALS;

static const struct device *const fake_dev =
	DEVICE_DT_GET(DT_NODELABEL(fake_input_device));

DEVICE_DT_DEFINE(DT_INST(0, vnd_input_device), NULL, NULL, NULL, NULL,
		 PRE_KERNEL_1, CONFIG_KERNEL_INIT_PRIORITY_DEVICE, NULL);

FAKE_VOID_FUNC(keyboard_state_changed, int, int, int);

ZTEST(keyboard_input, test_keyboard_input_events)
{
	zassert_equal(keyboard_state_changed_fake.call_count, 0);

	input_report_abs(fake_dev, INPUT_ABS_X, 10, false, K_FOREVER);
	input_report_abs(fake_dev, INPUT_ABS_Y, 11, false, K_FOREVER);
	input_report_key(fake_dev, INPUT_BTN_TOUCH, 1, true, K_FOREVER);

	input_report_abs(fake_dev, INPUT_ABS_X, 10, false, K_FOREVER);
	input_report_abs(fake_dev, INPUT_ABS_Y, 11, false, K_FOREVER);
	input_report_key(fake_dev, INPUT_BTN_TOUCH, 0, true, K_FOREVER);

	zassert_equal(keyboard_state_changed_fake.call_count, 2);

	zassert_equal(keyboard_state_changed_fake.arg0_history[0], 11);
	zassert_equal(keyboard_state_changed_fake.arg1_history[0], 10);
	zassert_equal(keyboard_state_changed_fake.arg2_history[0], 1);

	zassert_equal(keyboard_state_changed_fake.arg0_history[1], 11);
	zassert_equal(keyboard_state_changed_fake.arg1_history[1], 10);
	zassert_equal(keyboard_state_changed_fake.arg2_history[1], 0);
}

ZTEST(keyboard_input, test_keyboard_input_enable_disable)
{
	zassert_equal(keyboard_state_changed_fake.call_count, 0);

	input_report_abs(fake_dev, INPUT_ABS_X, 1, false, K_FOREVER);
	input_report_abs(fake_dev, INPUT_ABS_Y, 2, false, K_FOREVER);
	input_report_key(fake_dev, INPUT_BTN_TOUCH, 1, true, K_FOREVER);

	zassert_equal(keyboard_state_changed_fake.call_count, 1);

	/* disable A */
	keyboard_scan_enable(0, KB_SCAN_DISABLE_A);

	input_report_key(fake_dev, INPUT_BTN_TOUCH, 1, true, K_FOREVER);

	zassert_equal(keyboard_state_changed_fake.call_count, 1);

	/* disable B */
	keyboard_scan_enable(0, KB_SCAN_DISABLE_B);

	input_report_key(fake_dev, INPUT_BTN_TOUCH, 1, true, K_FOREVER);

	zassert_equal(keyboard_state_changed_fake.call_count, 1);

	/* enable A */
	keyboard_scan_enable(1, KB_SCAN_DISABLE_A);

	input_report_key(fake_dev, INPUT_BTN_TOUCH, 1, true, K_FOREVER);

	zassert_equal(keyboard_state_changed_fake.call_count, 1);

	/* enable B */
	keyboard_scan_enable(1, KB_SCAN_DISABLE_B);

	input_report_key(fake_dev, INPUT_BTN_TOUCH, 1, true, K_FOREVER);

	zassert_equal(keyboard_state_changed_fake.call_count, 2);
}

static void reset(void *fixture)
{
	ARG_UNUSED(fixture);

	RESET_FAKE(keyboard_state_changed);
}

ZTEST_SUITE(keyboard_input, NULL, NULL, reset, reset, NULL);
