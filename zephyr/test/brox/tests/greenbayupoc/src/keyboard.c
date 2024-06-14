/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "keyboard_8042_sharedlib.h"

#include <zephyr/ztest.h>

ZTEST_SUITE(greenbayupoc_keyboard, NULL, NULL, NULL, NULL, NULL);

ZTEST(greenbayupoc_keyboard, test_get_scancode_set2)
{
	/* Test some special keys of the customization matrix */
	zassert_equal(get_scancode_set2(0, 11), SCANCODE_F12);

	/* Test out of the matrix range */
	zassert_equal(get_scancode_set2(8, 12), 0);
	zassert_equal(get_scancode_set2(0, 18), 0);
}

ZTEST(greenbayupoc_keyboard, test_set_scancode_set2)
{
	/* Set some special keys and read back */
	zassert_equal(get_scancode_set2(7, 0), 0);
	set_scancode_set2(7, 0, SCANCODE_CAPSLOCK);
	zassert_equal(get_scancode_set2(7, 0), SCANCODE_CAPSLOCK);

	zassert_equal(get_scancode_set2(1, 0), 0);
	set_scancode_set2(1, 0, SCANCODE_F12);
	zassert_equal(get_scancode_set2(1, 0), SCANCODE_F12);
}

ZTEST(greenbayupoc_keyboard, test_get_keycap_label)
{
	zassert_equal(get_keycap_label(1, 3), KLLI_SEARC);
	zassert_equal(get_keycap_label(0, 11), KLLI_F12);
	zassert_equal(get_keycap_label(0, 0), KLLI_UNKNO);
	zassert_equal(get_keycap_label(1, 0), KLLI_UNKNO);
}

ZTEST(greenbayupoc_keyboard, test_set_keycap_label)
{
	zassert_equal(get_keycap_label(0, 0), KLLI_UNKNO);
	set_keycap_label(0, 0, KLLI_SEARC);
	zassert_equal(get_keycap_label(0, 0), KLLI_SEARC);

	zassert_equal(get_keycap_label(1, 0), KLLI_UNKNO);
	set_keycap_label(1, 0, KLLI_F12);
	zassert_equal(get_keycap_label(1, 0), KLLI_F12);
}
