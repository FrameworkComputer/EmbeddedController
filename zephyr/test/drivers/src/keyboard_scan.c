/* Copyright 2022 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include <ztest.h>
#include <drivers/emul.h>
#include <drivers/gpio.h>
#include <drivers/gpio/gpio_emul.h>
#include <emul/emul_kb_raw.h>

#include "test_state.h"

int emulate_keystate(int row, int col, int pressed)
{
	const struct device *dev =
		DEVICE_DT_GET(DT_NODELABEL(cros_kb_raw));

	return emul_kb_raw_set_kbstate(dev, row, col, pressed);
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
