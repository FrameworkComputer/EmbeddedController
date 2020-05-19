/* Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Test the STATIC_IF and STATIC_IF_NOT macros. */

#include "common.h"
#include "test_util.h"

#undef CONFIG_UNDEFINED
#define CONFIG_BLANK

STATIC_IF(CONFIG_UNDEFINED) int this_var_is_extern;
STATIC_IF_NOT(CONFIG_BLANK) int this_var_is_extern_too;
STATIC_IF(CONFIG_BLANK) int this_var_is_static;
STATIC_IF_NOT(CONFIG_UNDEFINED) int this_var_is_static_too;

static int test_static_if_blank(void)
{
	TEST_ASSERT(this_var_is_static == 0);
	TEST_ASSERT(this_var_is_static_too == 0);

	return EC_SUCCESS;
}

static int test_static_if_unused_no_fail(void)
{
	/*
	 * This should not cause linker errors because the variables
	 * go unused (usage is optimized away).
	 */
	if (IS_ENABLED(CONFIG_UNDEFINED))
		this_var_is_extern = 1;

	if (!IS_ENABLED(CONFIG_BLANK))
		this_var_is_extern_too = 1;

	return EC_SUCCESS;
}

void run_test(int argc, char **argv)
{
	test_reset();

	RUN_TEST(test_static_if_blank);
	RUN_TEST(test_static_if_unused_no_fail);

	test_print_result();
}
