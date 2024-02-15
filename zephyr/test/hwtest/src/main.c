/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "ec_app_main.h"
#include "host_command.h"

#include <zephyr/kernel.h>

#ifdef CONFIG_ZTEST
#include <zephyr/ztest.h>
#ifdef CONFIG_ZTEST_SHELL

static void main_thread(void *arg1, void *arg2, void *arg3)
{
	ec_app_main();

	if (IS_ENABLED(CONFIG_TASK_HOSTCMD_THREAD_MAIN)) {
		host_command_main();
	} else if (IS_ENABLED(CONFIG_THREAD_MONITOR)) {
		k_sleep(K_FOREVER);
	}
}
K_THREAD_DEFINE(main_thread_tid, CONFIG_MAIN_STACK_SIZE, main_thread, NULL,
		NULL, NULL, 1, 0, 0);

#else /* CONFIG_ZTEST_SHELL */

/* test_main is called by a main function provided by the ZTEST. The Host
 * Commands have to use a dedicated thread if needed.
 */
void test_main(void)
{
	ec_app_main();

	k_sleep(K_MSEC(2000));

	ztest_run_test_suites(NULL, false, 1, 1);
}

#endif /* CONFIG_ZTEST_SHELL */
#endif /* CONFIG_ZTEST */
