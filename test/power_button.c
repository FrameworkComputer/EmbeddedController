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
#include "power_button.h"
#include "test_util.h"
#include "timer.h"
#include "util.h"

static int pb_hook_count;

int lid_is_open(void)
{
	return 1;
}

static void pb_change_hook(void)
{
	pb_hook_count++;
}
DECLARE_HOOK(HOOK_POWER_BUTTON_CHANGE, pb_change_hook, HOOK_PRIO_DEFAULT);

int pb_memmap_state(void)
{
	uint8_t *memmap = host_get_memmap(EC_MEMMAP_SWITCHES);
	return *memmap & EC_SWITCH_POWER_BUTTON_PRESSED;
}

static int test_hook(void)
{
	/* Release power button for testing */
	gpio_set_level(GPIO_POWER_BUTTON_L, 1);
	crec_msleep(100);
	pb_hook_count = 0;
	host_clear_events(0xffffffff);

	gpio_set_level(GPIO_POWER_BUTTON_L, 0);
	crec_msleep(50);
	TEST_ASSERT(pb_hook_count == 1);
	TEST_ASSERT(power_button_is_pressed());
	TEST_ASSERT(pb_memmap_state());
	TEST_ASSERT(host_get_events() &
		    EC_HOST_EVENT_MASK(EC_HOST_EVENT_POWER_BUTTON));
	host_clear_events(0xffffffff);

	gpio_set_level(GPIO_POWER_BUTTON_L, 1);
	crec_msleep(50);
	TEST_ASSERT(pb_hook_count == 2);
	TEST_ASSERT(!power_button_is_pressed());
	TEST_ASSERT(!pb_memmap_state());
	TEST_ASSERT(!(host_get_events() &
		      EC_HOST_EVENT_MASK(EC_HOST_EVENT_POWER_BUTTON)));

	return EC_SUCCESS;
}

static int test_debounce(void)
{
	/* Release power button for testing */
	gpio_set_level(GPIO_POWER_BUTTON_L, 1);
	crec_msleep(100);
	pb_hook_count = 0;
	host_clear_events(0xffffffff);

	gpio_set_level(GPIO_POWER_BUTTON_L, 0);
	crec_msleep(20);
	TEST_ASSERT(pb_hook_count == 0);
	TEST_ASSERT(!power_button_is_pressed());
	TEST_ASSERT(!pb_memmap_state());
	TEST_ASSERT(!(host_get_events() &
		      EC_HOST_EVENT_MASK(EC_HOST_EVENT_POWER_BUTTON)));

	gpio_set_level(GPIO_POWER_BUTTON_L, 1);
	crec_msleep(50);
	TEST_ASSERT(pb_hook_count == 0);
	TEST_ASSERT(!power_button_is_pressed());
	TEST_ASSERT(!pb_memmap_state());
	TEST_ASSERT(!(host_get_events() &
		      EC_HOST_EVENT_MASK(EC_HOST_EVENT_POWER_BUTTON)));

	return EC_SUCCESS;
}

void run_test(int argc, const char **argv)
{
	test_reset();

	RUN_TEST(test_hook);
	RUN_TEST(test_debounce);

	test_print_result();
}
