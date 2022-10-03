/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <zephyr/ztest.h>

ZTEST_USER(board_power, test_test)
{
	zassert_true(true, NULL, NULL);
}

ZTEST_SUITE(board_power, NULL, NULL, NULL, NULL, NULL);
