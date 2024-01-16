/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "ec_app_main.h"
#include "hooks.h"

#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

void test_main(void)
{
	ec_app_main();

	k_sleep(K_MSEC(1000));

	/* Run all the suites that depend on main being called */
	ztest_run_test_suites(NULL, false, 1, 1);

	/* Check that every suite ran */
	ztest_verify_all_test_suites_ran();
}
