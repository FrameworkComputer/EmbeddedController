/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "chipset.h"
#include "keyboard_protocol.h"
#include "system.h"

#include <zephyr/device.h>
#include <zephyr/fff.h>
#include <zephyr/input/input.h>
#include <zephyr/sys/util.h>
#include <zephyr/ztest.h>

#include <dt-bindings/kbd.h>

DEFINE_FFF_GLOBALS;

FAKE_VOID_FUNC(chipset_reset, enum chipset_shutdown_reason);
FAKE_VOID_FUNC(keyboard_clear_buffer);
FAKE_VOID_FUNC(system_enter_hibernate, uint32_t, uint32_t);

#define CROS_EC_KEYBOARD_NODE DT_CHOSEN(cros_ec_keyboard)

#define TEST_BOOT_KEYS_NODE DT_NODELABEL(test_runtime_keys)
#define VOL_UP_ROW KBD_RC_ROW(DT_PROP(TEST_BOOT_KEYS_NODE, vol_up_rc))
#define VOL_UP_COL KBD_RC_COL(DT_PROP(TEST_BOOT_KEYS_NODE, vol_up_rc))

#define LEFT_ALT_ROW KBD_RC_ROW(DT_PROP(TEST_BOOT_KEYS_NODE, left_alt_rc))
#define LEFT_ALT_COL KBD_RC_COL(DT_PROP(TEST_BOOT_KEYS_NODE, left_alt_rc))
#define RIGHT_ALT_ROW KBD_RC_ROW(DT_PROP(TEST_BOOT_KEYS_NODE, right_alt_rc))
#define RIGHT_ALT_COL KBD_RC_COL(DT_PROP(TEST_BOOT_KEYS_NODE, right_alt_rc))
#define R_ROW KBD_RC_ROW(DT_PROP(TEST_BOOT_KEYS_NODE, r_rc))
#define R_COL KBD_RC_COL(DT_PROP(TEST_BOOT_KEYS_NODE, r_rc))
#define H_ROW KBD_RC_ROW(DT_PROP(TEST_BOOT_KEYS_NODE, h_rc))
#define H_COL KBD_RC_COL(DT_PROP(TEST_BOOT_KEYS_NODE, h_rc))

static void report_fake(int row, int col, bool val)
{
	const struct device *dev = DEVICE_DT_GET(CROS_EC_KEYBOARD_NODE);
	input_report_abs(dev, INPUT_ABS_X, col, false, K_FOREVER);
	input_report_abs(dev, INPUT_ABS_Y, row, false, K_FOREVER);
	input_report_key(dev, INPUT_BTN_TOUCH, val, true, K_FOREVER);
}

#define assert_call_count(reset, clear_buffer, hibernate)             \
	do {                                                          \
		zassert_equal(chipset_reset_fake.call_count, reset);  \
		zassert_equal(keyboard_clear_buffer_fake.call_count,  \
			      clear_buffer);                          \
		zassert_equal(system_enter_hibernate_fake.call_count, \
			      hibernate);                             \
	} while (0)

ZTEST(runtime_keys, test_warm_reset)
{
	report_fake(VOL_UP_ROW, VOL_UP_COL, true);
	assert_call_count(0, 0, 0);

	report_fake(LEFT_ALT_ROW, LEFT_ALT_COL, true);
	assert_call_count(0, 0, 0);

	report_fake(R_ROW, R_COL, true);
	assert_call_count(1, 1, 0);
}

ZTEST(runtime_keys, test_warm_reset_alt)
{
	report_fake(VOL_UP_ROW, VOL_UP_COL, true);
	assert_call_count(0, 0, 0);

	report_fake(RIGHT_ALT_ROW, RIGHT_ALT_COL, true);
	assert_call_count(0, 0, 0);

	report_fake(R_ROW, R_COL, true);
	assert_call_count(1, 1, 0);
}

ZTEST(runtime_keys, test_hibernate)
{
	report_fake(VOL_UP_ROW, VOL_UP_COL, true);
	assert_call_count(0, 0, 0);

	report_fake(LEFT_ALT_ROW, LEFT_ALT_COL, true);
	assert_call_count(0, 0, 0);

	report_fake(H_ROW, H_COL, true);
	assert_call_count(0, 0, 1);
}

ZTEST(runtime_keys, test_hibernate_alt)
{
	report_fake(VOL_UP_ROW, VOL_UP_COL, true);
	assert_call_count(0, 0, 0);

	report_fake(RIGHT_ALT_ROW, RIGHT_ALT_COL, true);
	assert_call_count(0, 0, 0);

	report_fake(H_ROW, H_COL, true);
	assert_call_count(0, 0, 1);
}

ZTEST(runtime_keys, test_stray_keys_no_action)
{
	report_fake(VOL_UP_ROW, VOL_UP_COL, true);
	assert_call_count(0, 0, 0);

	report_fake(LEFT_ALT_ROW, LEFT_ALT_COL, true);
	assert_call_count(0, 0, 0);

	report_fake(10, 11, true); /* stray key */
	assert_call_count(0, 0, 0);

	report_fake(R_ROW, R_COL, true);
	assert_call_count(0, 0, 0);
	report_fake(R_ROW, R_COL, false);

	report_fake(H_ROW, H_COL, true);
	assert_call_count(0, 0, 0);
	report_fake(H_ROW, H_COL, false);
}

int test_reinit(void);

static void reset(void *fixture)
{
	ARG_UNUSED(fixture);

	test_reinit();

	RESET_FAKE(chipset_reset);
	RESET_FAKE(keyboard_clear_buffer);
	RESET_FAKE(system_enter_hibernate);
}

ZTEST_SUITE(runtime_keys, NULL, NULL, reset, reset, NULL);
