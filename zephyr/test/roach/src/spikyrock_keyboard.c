/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "keyboard_protocol.h"
#include "spikyrock_keyboard.h"

#include <zephyr/fff.h>
#include <zephyr/ztest.h>

ZTEST(spikyrock_keyboard, test_keyboard_event)
{
	zassert_equal_ptr(board_vivaldi_keybd_config(), &spikyrock_keyboard);
}

ZTEST_SUITE(spikyrock_keyboard, NULL, NULL, NULL, NULL, NULL);
