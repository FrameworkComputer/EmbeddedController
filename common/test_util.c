/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Test utilities.
 */

#include <signal.h>
#include <stdlib.h>

#include "console.h"
#include "host_command.h"
#include "system.h"
#include "task.h"
#include "test_util.h"
#include "util.h"

int __test_error_count;

/* Weak reference function as an entry point for unit test */
test_mockable void run_test(void) { }

/* Default dummy test init */
test_mockable void test_init(void) { }

#ifdef TEST_COVERAGE
extern void __gcov_flush(void);

void emulator_flush(void)
{
	__gcov_flush();
}

void test_end_hook(int sig)
{
	emulator_flush();
	exit(0);
}

void register_test_end_hook(void)
{
	signal(SIGTERM, test_end_hook);
}
#else
void emulator_flush(void)
{
}

void register_test_end_hook(void)
{
}
#endif

void test_reset(void)
{
	__test_error_count = 0;
}

void test_pass(void)
{
	ccprintf("Pass!\n");
}

void test_fail(void)
{
	ccprintf("Fail!\n");
}

void test_print_result(void)
{
	if (__test_error_count)
		ccprintf("Fail! (%d tests)\n", __test_error_count);
	else
		ccprintf("Pass!\n");
}

int test_get_error_count(void)
{
	return __test_error_count;
}

uint32_t test_get_state(void)
{
	return system_get_scratchpad();
}

test_mockable void test_clean_up(void)
{
}

void test_reboot_to_next_step(enum test_state_t step)
{
	ccprintf("Rebooting to next test step...\n");
	cflush();
	system_set_scratchpad(TEST_STATE_MASK(step));
	system_reset(SYSTEM_RESET_HARD);
}

test_mockable void test_run_step(uint32_t state)
{
}

void test_run_multistep(void)
{
	uint32_t state = test_get_state();

	if (state & TEST_STATE_MASK(TEST_STATE_PASSED)) {
		test_clean_up();
		system_set_scratchpad(0);
		test_pass();
	} else if (state & TEST_STATE_MASK(TEST_STATE_FAILED)) {
		test_clean_up();
		system_set_scratchpad(0);
		test_fail();
	}

	if (state & TEST_STATE_STEP_1 || state == 0) {
		task_wait_event(-1); /* Wait for run_test() */
		test_run_step(TEST_STATE_MASK(TEST_STATE_STEP_1));
	} else {
		test_run_step(state);
	}
}

#ifdef HAS_TASK_HOSTCMD
int test_send_host_command(int command, int version, const void *params,
			   int params_size, void *resp, int resp_size)
{
	struct host_cmd_handler_args args;

	args.version = version;
	args.command = command;
	args.params = params;
	args.params_size = params_size;
	args.response = resp;
	args.response_max = resp_size;
	args.response_size = 0;

	return host_command_process(&args);
}
#endif  /* TASK_HAS_HOSTCMD */

static int command_run_test(int argc, char **argv)
{
	run_test();
	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(runtest, command_run_test,
			NULL, NULL, NULL);
