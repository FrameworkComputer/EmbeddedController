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

ZTEST(write_protection, test_write_protection_external)
{
	zassert_true(write_protect_is_asserted());

	disable_write_protect_external();
	zassert_false(write_protect_is_asserted());
}

ZTEST_SUITE(write_protection, drivers_predicate_post_main, NULL, NULL, NULL,
	    NULL);
