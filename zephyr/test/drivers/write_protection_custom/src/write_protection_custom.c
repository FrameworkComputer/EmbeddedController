/* Copyright 2025 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "test/drivers/test_state.h"
#include "write_protect.h"

#include <zephyr/fff.h>
#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

FAKE_VALUE_FUNC(int, write_protect_is_asserted_custom);

ZTEST(write_protection, test_write_protection_custom)
{
	write_protect_is_asserted_custom_fake.return_val = 1;
	zassert_equal(1, write_protect_is_asserted());

	write_protect_is_asserted_custom_fake.return_val = 0;
	zassert_equal(0, write_protect_is_asserted());
}

ZTEST_SUITE(write_protection, drivers_predicate_post_main, NULL, NULL, NULL,
	    NULL);
