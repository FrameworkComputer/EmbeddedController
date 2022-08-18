/* Copyright 2022 The ChromiumOS Authors.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <zephyr/zephyr.h>
#include <zephyr/ztest.h>

#include "builtin/stdio.h"
#include "test/drivers/test_state.h"

ZTEST_USER(console, printf_overflow)
{
	char buffer[10];

	zassert_equal(-EC_ERROR_OVERFLOW,
		      crec_snprintf(buffer, 4, "1234567890"), NULL);
	zassert_equal(0, strcmp(buffer, "123"), "got '%s'", buffer);
	zassert_equal(-EC_ERROR_OVERFLOW,
		      crec_snprintf(buffer, 4, "%%%%%%%%%%"), NULL);
	zassert_equal(0, strcmp(buffer, "%%%"), "got '%s'", buffer);
}

ZTEST_SUITE(console, drivers_predicate_post_main, NULL, NULL, NULL, NULL);
