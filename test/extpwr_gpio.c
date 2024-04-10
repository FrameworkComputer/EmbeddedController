/* Copyright 2013 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Test GPIO extpower module.
 */

#include "common.h"
#include "console.h"
#include "extpower.h"
#include "gpio.h"
#include "hooks.h"
#include "host_command.h"
#include "test_util.h"
#include "timer.h"
#include "util.h"

static int ac_hook_count;

static void set_ac(int val)
{
	gpio_set_level(GPIO_AC_PRESENT, val);
	crec_msleep(50);
}

static void ac_change_hook(void)
{
	ac_hook_count++;
}
DECLARE_HOOK(HOOK_AC_CHANGE, ac_change_hook, HOOK_PRIO_DEFAULT);

static int test_hook(void)
{
	/* Remove AC for testing */
	set_ac(0);
	ac_hook_count = 0;
	host_clear_events(0xffffffff);

	set_ac(1);
	TEST_ASSERT(ac_hook_count == 1);
	TEST_ASSERT(extpower_is_present());
	TEST_ASSERT(host_get_events() &
		    EC_HOST_EVENT_MASK(EC_HOST_EVENT_AC_CONNECTED));

	set_ac(0);
	TEST_ASSERT(ac_hook_count == 2);
	TEST_ASSERT(!extpower_is_present());
	TEST_ASSERT(host_get_events() &
		    EC_HOST_EVENT_MASK(EC_HOST_EVENT_AC_DISCONNECTED));

	return EC_SUCCESS;
}

void run_test(int argc, const char **argv)
{
	test_reset();

	RUN_TEST(test_hook);

	test_print_result();
}
