/* Copyright 2022 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include <zephyr/ztest.h>
#include <zephyr/drivers/emul.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/gpio/gpio_emul.h>
#include <emul/emul_kb_raw.h>

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
ZTEST_SUITE(keyboard_scan, drivers_predicate_post_main, NULL, NULL, NULL, NULL);
