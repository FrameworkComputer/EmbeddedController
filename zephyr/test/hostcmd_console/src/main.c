/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "ec_app_main.h"
#include "test_state.h"

#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

bool predicate_post_main(const void *state)
{
	return ((struct test_state *)state)->ec_app_main_run;
}

void test_main(void)
{
	const struct device *const dev =
		DEVICE_DT_GET(DT_CHOSEN(zephyr_shell_uart));

	struct test_state state = {
		.ec_app_main_run = false,
	};

	/* Run all the suites that depend on main not being called yet */
	ztest_run_test_suites(&state, false, 1, 1);

	ec_app_main();

	/* Allow the shell to initialize. */
	k_sleep(K_MSEC(10));

	state.ec_app_main_run = true;

	if (!device_is_ready(dev)) {
		return;
	}

	/* Run all the suites that depend on main being called */
	ztest_run_test_suites(&state, false, 1, 1);

	/* Check that every suite ran */
	ztest_verify_all_test_suites_ran();
}
