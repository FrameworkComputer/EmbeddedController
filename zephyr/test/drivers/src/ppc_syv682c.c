/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <ztest.h>

#include "syv682x.h"

static void test_board_is_syv682c(void)
{
	zassert_true(syv682x_board_is_syv682c(0), NULL);
}

void test_suite_ppc_syv682c(void)
{
	ztest_test_suite(
		ppc_syv682c,
		ztest_unit_test(test_board_is_syv682c));
	ztest_run_test_suite(ppc_syv682c);
}
