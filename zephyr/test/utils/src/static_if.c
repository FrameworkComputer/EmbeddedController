/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"

#include <zephyr/ztest.h>

#undef CONFIG_UNDEFINED
#define CONFIG_BLANK

STATIC_IF(CONFIG_UNDEFINED) int this_var_is_extern;
STATIC_IF_NOT(CONFIG_BLANK) int this_var_is_extern_too;
STATIC_IF(CONFIG_BLANK) int this_var_is_static;
STATIC_IF_NOT(CONFIG_UNDEFINED) int this_var_is_static_too;

ZTEST_SUITE(static_if, NULL, NULL, NULL, NULL, NULL);

ZTEST(static_if, test_static_if_blank)
{
	zassert_true(this_var_is_static == 0);
	zassert_true(this_var_is_static_too == 0);
}

ZTEST(static_if, test_static_if_unused_no_fail)
{
	/*
	 * This should not cause linker errors because the variables
	 * go unused (usage is optimized away).
	 */
	if (IS_ENABLED(CONFIG_UNDEFINED))
		this_var_is_extern = 1;

	if (!IS_ENABLED(CONFIG_BLANK))
		this_var_is_extern_too = 1;
}
