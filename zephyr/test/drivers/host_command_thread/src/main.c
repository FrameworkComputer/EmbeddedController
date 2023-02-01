/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/**
 * @file main.c
 *
 * WARNING:
 *   Do not add tests to this binary. This test messes with the main thread and
 *   can only run a single test function.
 */
#include "host_command.h"
#include "task.h"
#include "test/drivers/test_state.h"

#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

#define CUSTOM_COMMAND_ID 0x0088

/* Thread id of fake main thread */
static k_tid_t fake_main_tid;

/* 0 - did not run, 1 - true, -1 - false */
static int last_check_main_thread_result;

static enum ec_status check_main_thread(struct host_cmd_handler_args *args)
{
	last_check_main_thread_result =
		(k_current_get() == get_main_thread() ? 1 : -1);
	return EC_RES_SUCCESS;
}

DECLARE_HOST_COMMAND(CUSTOM_COMMAND_ID, check_main_thread, EC_VER_MASK(0));

static void fake_main_thread(void *a, void *b, void *c)
{
	host_command_main();
}

K_THREAD_STACK_DEFINE(fake_main_thread_stack, 4000);

/* Override get_hostcmd_thread() from shim/src/tasks.c so
 * task_get_current() returns TASK_ID_HOSTCMD when fake main thread
 * is running.
 */
k_tid_t get_hostcmd_thread(void)
{
	return fake_main_tid;
}

ZTEST_SUITE(host_cmd_thread, drivers_predicate_post_main, NULL, NULL, NULL,
	    NULL);

ZTEST(host_cmd_thread, test_takeover)
{
	struct host_cmd_handler_args args =
		BUILD_HOST_COMMAND_SIMPLE(CUSTOM_COMMAND_ID, 0);
	const char expected_thread_name[] = "HOSTCMD";
	struct k_thread fake_main_thread_data;

	fake_main_tid = k_thread_create(
		&fake_main_thread_data, fake_main_thread_stack,
		K_THREAD_STACK_SIZEOF(fake_main_thread_stack), fake_main_thread,
		NULL, NULL, NULL, 1, 0, K_NO_WAIT);

	/* Wait for the thread to start */
	k_msleep(500);

	/* Get the name of the thread (must be done after the sleep) */
	const char *main_thread_name = k_thread_name_get(get_main_thread());

	/* Verify that the thread is not the hostcmd thread */
	zassert_equal(EC_TASK_PRIORITY(EC_TASK_HOSTCMD_PRIO),
		      k_thread_priority_get(get_main_thread()));
	zassert_equal(strlen(expected_thread_name), strlen(main_thread_name));
	zassert_mem_equal(expected_thread_name, main_thread_name,
			  strlen(expected_thread_name));

	/* Try running a host command */
	host_command_received(&args);
	k_msleep(1000);

	/* Make sure that the host command ran, the result should be -1 because
	 * it's not the original main thread.
	 */
	zassert_equal(-1, last_check_main_thread_result);

	/* Kill the extra thread */
	k_thread_abort(fake_main_tid);
}
