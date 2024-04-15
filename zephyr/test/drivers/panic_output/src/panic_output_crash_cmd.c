/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "console.h"
#include "panic.h"
#include "test/drivers/test_state.h"
#include "test/drivers/utils.h"

#include <signal.h>
#include <stdint.h>
#include <stdio.h>

#include <zephyr/kernel.h>
#include <zephyr/shell/shell.h>
#include <zephyr/ztest.h>
#include <zephyr/ztest_assert.h>

/* Special return codes for run_crash_command() */
#define RETURN_CODE_CRASHED (-1)
#define RETURN_CODE_TIMEOUT (-2)

/** Create a new thread for running the `crash` console command. As its name
 * suggests, this command causes a number of fatal errors (e.g. divide by zero).
 * Run it in a separate thread so that we can observe these crashes without
 * causing the ztest thread that runs the test functions to get aborted.
 */
static struct k_thread crash_thread;

/* Create a stack for the crash thread */
K_THREAD_STACK_DEFINE(crash_thread_stack, 1024);

/** Captures the last signal number received by `handle_signal()`. */
static int signal_received;
/** Captures the number of times `handle_signal()` was called. */
static int signal_count;

static void handle_signal(int sig);
static void register_signals(void)
{
	signal(SIGSEGV, handle_signal);
	signal(SIGFPE, handle_signal);
}

/**
 * @brief Handler for signals sent to the process. Log the signal number,
 *        increment the signal count, and abort the crash test.
 *
 * @param sig Signal number, from signal.h
 */
static void handle_signal(int sig)
{
	register_signals();

	signal_received = sig;
	signal_count += 1;

	k_thread_abort(&crash_thread);
}

/**
 * @brief Worker function for the crash thread that calls the `crash` console
 *        command.
 *
 * @param a The value of `argc` (forced as a pointer type).
 * @param b `argv` pointer.
 * @param c Pointer to location to write the return value.
 */
static void crash_thread_worker(void *a, void *b, void *c)
{
	int argc = (int)a;
	const char **argv = (const char **)b;
	int *return_val = c;

	/* If the return value remains RETURN_CODE_CRASHED, then the command
	 * did not return.
	 */
	*return_val = RETURN_CODE_CRASHED;
	*return_val = test_command_crash(argc, argv);
}

/**
 * @brief Worker function for nested crashes. Calls the
 *        `command_crash_nested_handler()` function.
 *
 * @param a UNUSED.
 * @param b UNUSED.
 * @param c Pointer to location to write the return value.
 */
static void nested_crash_thread_worker(void *a, void *b, void *c)
{
	ARG_UNUSED(a);
	ARG_UNUSED(b);
	int *return_val = c;

	*return_val = RETURN_CODE_CRASHED;
	*return_val = command_crash_nested_handler();
}

/**
 * @brief Helper function to spawn a new thread that runs the `crash` console
 *        command. Waits for the command to exit/fail.
 *
 * @param argc Number of elements in `argv`
 * @param argv CLI arguments to pass to command. Include the command name.
 * @param timeout Time to wait for the thread to exit, in milliseconds.
 * @return int Console command's return value, or RETURN_CODE_CRASHED if caused
 *         a fatal error, or RETURN_CODE_TIMEOUT if it hung.
 */
static int run_crash_command(int argc, const char **argv, k_timeout_t timeout)
{
	static int return_val;
	static int nested_return_val;

	k_thread_create(&crash_thread, crash_thread_stack,
			K_THREAD_STACK_SIZEOF(crash_thread_stack),
			(k_thread_entry_t)crash_thread_worker, (void *)argc,
			argv, &return_val, CONFIG_ZTEST_THREAD_PRIORITY + 1,
			K_INHERIT_PERMS, K_NO_WAIT);

	if (k_thread_join(&crash_thread, timeout) == -EAGAIN) {
		k_thread_abort(&crash_thread);
		return_val = RETURN_CODE_TIMEOUT;
	}

	/* Keep running command_crash_nested_handler as long as crashes are
	 * occurring.
	 */
	while (return_val == RETURN_CODE_TIMEOUT ||
	       return_val == RETURN_CODE_CRASHED) {
		k_thread_create(&crash_thread, crash_thread_stack,
				K_THREAD_STACK_SIZEOF(crash_thread_stack),
				(k_thread_entry_t)nested_crash_thread_worker,
				(void *)0, NULL, &nested_return_val,
				CONFIG_ZTEST_THREAD_PRIORITY + 1,
				K_INHERIT_PERMS, K_NO_WAIT);
		if (k_thread_join(&crash_thread, K_MSEC(100)) == -EAGAIN) {
			k_thread_abort(&crash_thread);
			nested_return_val = RETURN_CODE_TIMEOUT;
		}
		/* command_crash_nested_handler returns EC_SUCCESS when
		 * there are no more nested crashes. Do not overwrite
		 * the return_val in this case.
		 */
		if (nested_return_val == EC_SUCCESS) {
			break;
		}
		return_val = nested_return_val;
	}

	return return_val;
}

ZTEST(panic_output, test_feature_present)
{
	struct ec_response_get_features feat = host_cmd_get_features();

	zassert_true(feat.flags[1] &
			     EC_FEATURE_MASK_1(EC_FEATURE_ASSERT_REBOOTS),
		     "Failed to see feature present");
	zassert_true(IS_ENABLED(CONFIG_PLATFORM_EC_CONSOLE_CMD_CRASH_NESTED));
}

ZTEST(panic_output, test_console_cmd__unaligned_unaligned)
{
	int rv;
	const char *cmd[] = { "crash", "unaligned", "unaligned" };

	rv = run_crash_command(3, cmd, K_FOREVER);

	zassert_equal(RETURN_CODE_CRASHED, rv,
		      "Command returned %d but shouldn't have exited", rv);
	zassert_equal(SIGSEGV, signal_received);
	zassert_equal(2, signal_count);
}

ZTEST(panic_output, test_console_cmd__watchdog)
{
	/* Note: this does not verify that the watchdog fired, but that is
	 * covered in a different test suite.
	 */

	int rv;
	const char *cmd[] = { "crash", "watchdog" };

	rv = run_crash_command(2, cmd, K_MSEC(100));

	zassert_equal(RETURN_CODE_TIMEOUT, rv,
		      "Command returned %d but shouldn't have exited", rv);
	zassert_equal(0, signal_count);
}

ZTEST(panic_output, test_console_cmd__hang)
{
	int rv;
	const char *cmd[] = { "crash", "hang" };

	rv = run_crash_command(2, cmd, K_MSEC(100));

	zassert_equal(RETURN_CODE_TIMEOUT, rv,
		      "Command returned %d but shouldn't have exited", rv);
	zassert_equal(0, signal_count);
}

ZTEST(panic_output, test_console_cmd__null)
{
	int rv;
	const char *cmd[] = { "crash", "null" };

	rv = run_crash_command(2, cmd, K_FOREVER);

	zassert_equal(RETURN_CODE_CRASHED, rv,
		      "Command returned %d but shouldn't have exited", rv);
	zassert_equal(SIGSEGV, signal_received);
	zassert_equal(1, signal_count);
}

ZTEST(panic_output, test_console_cmd__null_null)
{
	int rv;
	const char *cmd[] = { "crash", "null", "null" };

	rv = run_crash_command(3, cmd, K_FOREVER);

	zassert_equal(RETURN_CODE_CRASHED, rv,
		      "Command returned %d but shouldn't have exited", rv);
	zassert_equal(SIGSEGV, signal_received);
	zassert_equal(2, signal_count);
}

ZTEST(panic_output, test_console_cmd__null_null_null)
{
	int rv;
	const char *cmd[] = { "crash", "null", "null", "null" };

	rv = run_crash_command(4, cmd, K_FOREVER);

	zassert_equal(RETURN_CODE_CRASHED, rv,
		      "Command returned %d but shouldn't have exited", rv);
	zassert_equal(SIGSEGV, signal_received);
	zassert_equal(3, signal_count);
}

ZTEST(panic_output, test_console_cmd__null_watchdog)
{
	int rv;
	const char *cmd[] = { "crash", "null", "watchdog" };

	rv = run_crash_command(3, cmd, K_FOREVER);

	zassert_equal(RETURN_CODE_TIMEOUT, rv,
		      "Command returned %d but shouldn't have exited", rv);
	zassert_equal(SIGSEGV, signal_received);
	zassert_equal(1, signal_count);
}

ZTEST(panic_output, test_console_cmd__watchdog_null)
{
	int rv;
	const char *cmd[] = { "crash", "watchdog", "null" };

	rv = run_crash_command(3, cmd, K_MSEC(100));

	zassert_equal(RETURN_CODE_CRASHED, rv,
		      "Command returned %d but shouldn't have exited", rv);
	zassert_equal(SIGSEGV, signal_received);
	zassert_equal(1, signal_count);
}

ZTEST(panic_output, test_console_cmd__watchdog_watchdog)
{
	int rv;
	const char *cmd[] = { "crash", "watchdog", "watchdog" };

	rv = run_crash_command(3, cmd, K_MSEC(100));

	zassert_equal(RETURN_CODE_TIMEOUT, rv,
		      "Command returned %d but shouldn't have exited", rv);
	zassert_equal(0, signal_count);
}

ZTEST(panic_output, test_console_cmd__null_bad_param)
{
	int rv;
	const char *cmd[] = { "crash", "null", "xyz" };

	rv = run_crash_command(3, cmd, K_FOREVER);

	zassert_equal(EC_ERROR_PARAM1, rv, "Command returned %d", rv);
	zassert_equal(SIGSEGV, signal_received);
	zassert_equal(1, signal_count);
}

ZTEST(panic_output, test_console_cmd__bad_param_null)
{
	int rv;
	const char *cmd[] = { "crash", "xyz", "null" };

	rv = run_crash_command(2, cmd, K_FOREVER);

	zassert_equal(EC_ERROR_PARAM1, rv, "Command returned %d", rv);
	zassert_equal(0, signal_count);
}

ZTEST(panic_output, test_console_cmd__bad_param)
{
	int rv;
	const char *cmd[] = { "crash", "xyz" };

	rv = run_crash_command(2, cmd, K_FOREVER);

	zassert_equal(EC_ERROR_PARAM1, rv, "Command returned %d", rv);
	zassert_equal(0, signal_count);
}

ZTEST(panic_output, test_console_cmd__no_param)
{
	int rv;
	const char *cmd[] = { "crash" };

	rv = run_crash_command(1, cmd, K_FOREVER);

	zassert_equal(EC_ERROR_PARAM1, rv, "Command returned %d", rv);
	zassert_equal(0, signal_count);
}

static void reset(void *data)
{
	ARG_UNUSED(data);

	register_signals();

	signal_received = 0;
	signal_count = 0;
}

ZTEST_SUITE(panic_output, drivers_predicate_post_main, NULL, reset, reset,
	    NULL);
