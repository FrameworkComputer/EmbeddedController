/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "ec_app_main.h"
#include "test_state.h"

#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

bool roach_predicate_post_main(const void *state)
{
	return ((struct test_state *)state)->ec_app_main_run;
}

void test_main(void)
{
	struct test_state state = {
		.ec_app_main_run = false,
	};

	/* Run all the suites that depend on main not being called yet */
	ztest_run_test_suites(&state);

	ec_app_main();

	state.ec_app_main_run = true;

	/* Run all the suites that depend on main being called */
	ztest_run_test_suites(&state);

	/* Check that every suite ran */
	ztest_verify_all_test_suites_ran();
}
