/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "host_command.h"
#include "keyboard_scan.h"
#include "system.h"

#include <zephyr/device.h>
#include <zephyr/fff.h>
#include <zephyr/input/input.h>
#include <zephyr/sys/util.h>
#include <zephyr/ztest.h>

#include <dt-bindings/kbd.h>

DEFINE_FFF_GLOBALS;

FAKE_VOID_FUNC(host_set_single_event, enum host_event_code);
FAKE_VALUE_FUNC(int, system_jumped_late);
FAKE_VALUE_FUNC(uint32_t, system_get_reset_flags);
FAKE_VALUE_FUNC(int, power_button_is_pressed);
FAKE_VOID_FUNC(tablet_disable);

void test_power_button_change(void);
void test_reset(void);
void test_reinit(void);
bool test_dwork_pending(void);
uint32_t keyboard_scan_get_boot_keys(void);

#define CROS_EC_KEYBOARD_NODE DT_CHOSEN(cros_ec_keyboard)

#define TEST_BOOT_KEYS_NODE DT_NODELABEL(test_boot_keys)
#define ESC_ROW KBD_RC_ROW(DT_PROP(TEST_BOOT_KEYS_NODE, esc_rc))
#define ESC_COL KBD_RC_COL(DT_PROP(TEST_BOOT_KEYS_NODE, esc_rc))
#define LEFT_SHIFT_ROW KBD_RC_ROW(DT_PROP(TEST_BOOT_KEYS_NODE, left_shift_rc))
#define LEFT_SHIFT_COL KBD_RC_COL(DT_PROP(TEST_BOOT_KEYS_NODE, left_shift_rc))
#define REFRESH_ROW KBD_RC_ROW(DT_PROP(TEST_BOOT_KEYS_NODE, refresh_rc))
#define REFRESH_COL KBD_RC_COL(DT_PROP(TEST_BOOT_KEYS_NODE, refresh_rc))

#define RECOVERY_NORMAL_MASK \
	(BIT(BOOT_KEY_POWER) | BIT(BOOT_KEY_REFRESH) | BIT(BOOT_KEY_ESC))
#define RECOVERY_RETRAINING_MASK \
	(RECOVERY_NORMAL_MASK | BIT(BOOT_KEY_LEFT_SHIFT))
#define POWER_RELEASED_MASK (BIT(BOOT_KEY_REFRESH) | BIT(BOOT_KEY_ESC))

static void report_fake(int row, int col, bool val)
{
	const struct device *dev = DEVICE_DT_GET(CROS_EC_KEYBOARD_NODE);
	input_report_abs(dev, INPUT_ABS_X, col, false, K_FOREVER);
	input_report_abs(dev, INPUT_ABS_Y, row, false, K_FOREVER);
	input_report_key(dev, INPUT_BTN_TOUCH, val, true, K_FOREVER);
}

ZTEST(boot_keys, test_recovery_normal)
{
	system_jumped_late_fake.return_val = 0;
	system_get_reset_flags_fake.return_val = EC_RESET_FLAG_RESET_PIN;

	power_button_is_pressed_fake.return_val = 1;
	test_power_button_change();
	report_fake(ESC_ROW, ESC_COL, true);
	report_fake(REFRESH_ROW, REFRESH_COL, true);

	test_reinit();

	zassert_equal(test_dwork_pending(), false);
	zassert_equal(host_set_single_event_fake.call_count, 1);
	zassert_equal(tablet_disable_fake.call_count, 1);
	zassert_equal(keyboard_scan_get_boot_keys(), RECOVERY_NORMAL_MASK);

	/* check key release */
	power_button_is_pressed_fake.return_val = 0;
	test_power_button_change();

	zassert_equal(keyboard_scan_get_boot_keys(), POWER_RELEASED_MASK);

	report_fake(ESC_ROW, ESC_COL, false);
	report_fake(REFRESH_ROW, REFRESH_COL, false);

	zassert_equal(keyboard_scan_get_boot_keys(), 0);
}

ZTEST(boot_keys, test_recovery_release_power_early)
{
	system_jumped_late_fake.return_val = 0;
	system_get_reset_flags_fake.return_val = EC_RESET_FLAG_RESET_PIN;

	power_button_is_pressed_fake.return_val = 1;
	test_power_button_change();
	report_fake(ESC_ROW, ESC_COL, true);
	report_fake(REFRESH_ROW, REFRESH_COL, true);
	power_button_is_pressed_fake.return_val = 0;
	test_power_button_change();

	test_reinit();

	zassert_equal(test_dwork_pending(), false);
	zassert_equal(host_set_single_event_fake.call_count, 1);
	zassert_equal(tablet_disable_fake.call_count, 1);
	zassert_equal(keyboard_scan_get_boot_keys(), POWER_RELEASED_MASK);
}

ZTEST(boot_keys, test_recovery_stray_keys)
{
	system_jumped_late_fake.return_val = 0;
	system_get_reset_flags_fake.return_val = EC_RESET_FLAG_RESET_PIN;

	power_button_is_pressed_fake.return_val = 1;
	test_power_button_change();
	report_fake(ESC_ROW, ESC_COL, true);
	report_fake(REFRESH_ROW, REFRESH_COL, true);
	/* stray keys */
	report_fake(10, 11, true);
	report_fake(12, 13, true);
	report_fake(10, 11, false); /* test the release path */

	test_reinit();

	zassert_equal(test_dwork_pending(), false);
	zassert_equal(host_set_single_event_fake.call_count, 0);
	zassert_equal(tablet_disable_fake.call_count, 0);
	/* keys are still tracked */
	zassert_equal(keyboard_scan_get_boot_keys(), RECOVERY_NORMAL_MASK);
}

ZTEST(boot_keys, test_recovery_retraining)
{
	system_jumped_late_fake.return_val = 0;
	system_get_reset_flags_fake.return_val = EC_RESET_FLAG_RESET_PIN;

	power_button_is_pressed_fake.return_val = 1;
	test_power_button_change();
	report_fake(ESC_ROW, ESC_COL, true);
	report_fake(REFRESH_ROW, REFRESH_COL, true);
	report_fake(LEFT_SHIFT_ROW, LEFT_SHIFT_COL, true);

	test_reinit();

	zassert_equal(test_dwork_pending(), false);
	zassert_equal(host_set_single_event_fake.call_count, 2);
	zassert_equal(tablet_disable_fake.call_count, 1);
	zassert_equal(keyboard_scan_get_boot_keys(), 0x8000000d);
}

ZTEST(boot_keys, test_ignore_keys)
{
	system_jumped_late_fake.return_val = 0;
	system_get_reset_flags_fake.return_val = EC_RESET_FLAG_RESET_PIN;

	power_button_is_pressed_fake.return_val = 1;
	test_power_button_change();
	report_fake(ESC_ROW, ESC_COL, true);
	report_fake(REFRESH_ROW, REFRESH_COL, true);

	/* ignored stray keys */
	report_fake(REFRESH_ROW, 10, true);
	report_fake(REFRESH_ROW, 11, true);
	report_fake(REFRESH_ROW, 12, true);

	test_reinit();

	zassert_equal(test_dwork_pending(), false);

	if (IS_ENABLED(CONFIG_BOOT_KEYS_GHOST_REFRESH_WORKAROUND)) {
		zassert_equal(host_set_single_event_fake.call_count, 1);
		zassert_equal(keyboard_scan_get_boot_keys(),
			      RECOVERY_NORMAL_MASK);
	} else {
		zassert_equal(host_set_single_event_fake.call_count, 0);
		/* keys are still tracked */
		zassert_equal(keyboard_scan_get_boot_keys(),
			      RECOVERY_NORMAL_MASK);
	}
}

ZTEST(boot_keys, test_normal_boot)
{
	system_jumped_late_fake.return_val = 0;
	system_get_reset_flags_fake.return_val = EC_RESET_FLAG_RESET_PIN;

	test_reinit();

	zassert_equal(test_dwork_pending(), false);
	zassert_equal(host_set_single_event_fake.call_count, 0);
	zassert_equal(tablet_disable_fake.call_count, 0);
	zassert_equal(keyboard_scan_get_boot_keys(), 0);
}

ZTEST(boot_keys, test_no_reset_pin)
{
	system_jumped_late_fake.return_val = 0;
	system_get_reset_flags_fake.call_count = 0;

	test_reinit();

	zassert_equal(test_dwork_pending(), false);
}

ZTEST(boot_keys, test_jumped_late)
{
	system_jumped_late_fake.return_val = 1;

	test_reinit();

	zassert_equal(system_get_reset_flags_fake.call_count, 0);
	zassert_equal(test_dwork_pending(), false);
}

static void reset(void *fixture)
{
	ARG_UNUSED(fixture);

	RESET_FAKE(host_set_single_event);
	RESET_FAKE(system_jumped_late);
	RESET_FAKE(system_get_reset_flags);
	RESET_FAKE(power_button_is_pressed);
	RESET_FAKE(tablet_disable);

	test_reset();
}

ZTEST_SUITE(boot_keys, NULL, NULL, reset, reset, NULL);
