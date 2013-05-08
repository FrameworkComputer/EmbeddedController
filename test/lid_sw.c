/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Test lid switch.
 */

#include "common.h"
#include "console.h"
#include "hooks.h"
#include "host_command.h"
#include "lid_switch.h"
#include "timer.h"
#include "util.h"

static int error_count;

static int mock_lid;
static int lid_hook_count;

#define RUN_TEST(n) \
	do { \
		ccprintf("Running %s...", #n); \
		cflush(); \
		if (n() == EC_SUCCESS) { \
			ccputs("OK\n"); \
		} else { \
			ccputs("Fail\n"); \
			error_count++; \
		} \
	} while (0)

#define TEST_ASSERT(n) \
	do { \
		if (!(n)) \
			return EC_ERROR_UNKNOWN; \
	} while (0)

int gpio_get_level(enum gpio_signal signal)
{
	if (signal == GPIO_LID_OPEN)
		return mock_lid;
	return 0;
}

static void lid_change_hook(void)
{
	lid_hook_count++;
}
DECLARE_HOOK(HOOK_LID_CHANGE, lid_change_hook, HOOK_PRIO_DEFAULT);

static int test_hook(void)
{
	/* Close lid for testing */
	mock_lid = 0;
	lid_interrupt(GPIO_LID_OPEN);
	msleep(100);
	lid_hook_count = 0;
	host_clear_events(0xffffffff);

	mock_lid = 1;
	lid_interrupt(GPIO_LID_OPEN);
	msleep(50);
	TEST_ASSERT(lid_hook_count == 1);
	TEST_ASSERT(lid_is_open());
	TEST_ASSERT(host_get_events() &
		    EC_HOST_EVENT_MASK(EC_HOST_EVENT_LID_OPEN));

	mock_lid = 0;
	lid_interrupt(GPIO_LID_OPEN);
	msleep(50);
	TEST_ASSERT(lid_hook_count == 2);
	TEST_ASSERT(!lid_is_open());
	TEST_ASSERT(host_get_events() &
		    EC_HOST_EVENT_MASK(EC_HOST_EVENT_LID_CLOSED));

	return EC_SUCCESS;
}

static int test_debounce(void)
{
	/* Close lid for testing */
	mock_lid = 0;
	lid_interrupt(GPIO_LID_OPEN);
	msleep(100);
	lid_hook_count = 0;
	host_clear_events(0xffffffff);

	mock_lid = 1;
	lid_interrupt(GPIO_LID_OPEN);
	msleep(20);
	TEST_ASSERT(lid_hook_count == 0);
	TEST_ASSERT(!lid_is_open());
	TEST_ASSERT(!(host_get_events() &
		      EC_HOST_EVENT_MASK(EC_HOST_EVENT_LID_OPEN)));

	mock_lid = 0;
	lid_interrupt(GPIO_LID_OPEN);
	msleep(50);
	TEST_ASSERT(lid_hook_count == 0);
	TEST_ASSERT(!lid_is_open());
	TEST_ASSERT(!(host_get_events() &
		      EC_HOST_EVENT_MASK(EC_HOST_EVENT_LID_OPEN)));

	return EC_SUCCESS;
}

void run_test(void)
{
	error_count = 0;

	RUN_TEST(test_hook);
	RUN_TEST(test_debounce);

	if (error_count)
		ccprintf("Fail!\n", error_count);
	else
		ccprintf("Pass!\n");
}

static int command_run_test(int argc, char **argv)
{
	run_test();
	return EC_SUCCESS;
}
DECLARE_CONSOLE_COMMAND(runtest, command_run_test,
			NULL, NULL, NULL);
