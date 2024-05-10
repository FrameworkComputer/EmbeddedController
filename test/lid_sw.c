/* Copyright 2013 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Test lid switch.
 */

#include "common.h"
#include "console.h"
#include "gpio.h"
#include "hooks.h"
#include "host_command.h"
#include "lid_switch.h"
#include "test_util.h"
#include "timer.h"
#include "util.h"

static int lid_hook_count;

static void lid_change_hook(void)
{
	lid_hook_count++;
}
DECLARE_HOOK(HOOK_LID_CHANGE, lid_change_hook, HOOK_PRIO_DEFAULT);

int lid_memmap_state(void)
{
	uint8_t *memmap = host_get_memmap(EC_MEMMAP_SWITCHES);
	return *memmap & EC_SWITCH_LID_OPEN;
}

static int test_hook(void)
{
	/* Close lid for testing */
	gpio_set_level(GPIO_LID_OPEN, 0);
	crec_msleep(100);
	lid_hook_count = 0;
	host_clear_events(0xffffffff);

	gpio_set_level(GPIO_LID_OPEN, 1);
	crec_msleep(50);
	TEST_ASSERT(lid_hook_count == 1);
	TEST_ASSERT(lid_is_open());
	TEST_ASSERT(lid_memmap_state());
	TEST_ASSERT(host_get_events() &
		    EC_HOST_EVENT_MASK(EC_HOST_EVENT_LID_OPEN));

	gpio_set_level(GPIO_LID_OPEN, 0);
	crec_msleep(50);
	TEST_ASSERT(lid_hook_count == 2);
	TEST_ASSERT(!lid_is_open());
	TEST_ASSERT(!lid_memmap_state());
	TEST_ASSERT(host_get_events() &
		    EC_HOST_EVENT_MASK(EC_HOST_EVENT_LID_CLOSED));

	return EC_SUCCESS;
}

static int test_debounce(void)
{
	/* Close lid for testing */
	gpio_set_level(GPIO_LID_OPEN, 0);
	crec_msleep(100);
	lid_hook_count = 0;
	host_clear_events(0xffffffff);

	gpio_set_level(GPIO_LID_OPEN, 1);
	crec_msleep(20);
	TEST_ASSERT(lid_hook_count == 0);
	TEST_ASSERT(!lid_is_open());
	TEST_ASSERT(!lid_memmap_state());
	TEST_ASSERT(!(host_get_events() &
		      EC_HOST_EVENT_MASK(EC_HOST_EVENT_LID_OPEN)));

	gpio_set_level(GPIO_LID_OPEN, 0);
	crec_msleep(50);
	TEST_ASSERT(lid_hook_count == 0);
	TEST_ASSERT(!lid_is_open());
	TEST_ASSERT(!lid_memmap_state());
	TEST_ASSERT(!(host_get_events() &
		      EC_HOST_EVENT_MASK(EC_HOST_EVENT_LID_OPEN)));

	return EC_SUCCESS;
}

void run_test(int argc, const char **argv)
{
	test_reset();

	RUN_TEST(test_hook);
	RUN_TEST(test_debounce);

	test_print_result();
}
