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
#include "power_button.h"
#include "test_util.h"
#include "timer.h"
#include "util.h"

static int mock_power_button = 1;
static int mock_lid = 1;
static int pb_hook_count;

int gpio_get_level(enum gpio_signal signal)
{
	if (signal == GPIO_POWER_BUTTON_L)
		return mock_power_button;
	return 0;
}

int lid_is_open(void)
{
	return mock_lid;
}

static void pb_change_hook(void)
{
	pb_hook_count++;
}
DECLARE_HOOK(HOOK_POWER_BUTTON_CHANGE, pb_change_hook, HOOK_PRIO_DEFAULT);

static int test_hook(void)
{
	/* Release power button for testing */
	mock_power_button = 1;
	power_button_interrupt(GPIO_POWER_BUTTON_L);
	msleep(100);
	pb_hook_count = 0;
	host_clear_events(0xffffffff);

	mock_power_button = 0;
	power_button_interrupt(GPIO_POWER_BUTTON_L);
	msleep(50);
	TEST_ASSERT(pb_hook_count == 1);
	TEST_ASSERT(power_button_is_pressed());
	TEST_ASSERT(host_get_events() &
		    EC_HOST_EVENT_MASK(EC_HOST_EVENT_POWER_BUTTON));
	host_clear_events(0xffffffff);

	mock_power_button = 1;
	power_button_interrupt(GPIO_POWER_BUTTON_L);
	msleep(50);
	TEST_ASSERT(pb_hook_count == 2);
	TEST_ASSERT(!power_button_is_pressed());
	TEST_ASSERT(!(host_get_events() &
		      EC_HOST_EVENT_MASK(EC_HOST_EVENT_POWER_BUTTON)));

	return EC_SUCCESS;
}

static int test_debounce(void)
{
	/* Release power button for testing */
	mock_power_button = 1;
	power_button_interrupt(GPIO_POWER_BUTTON_L);
	msleep(100);
	pb_hook_count = 0;
	host_clear_events(0xffffffff);

	mock_power_button = 0;
	power_button_interrupt(GPIO_POWER_BUTTON_L);
	msleep(20);
	TEST_ASSERT(pb_hook_count == 0);
	TEST_ASSERT(!power_button_is_pressed());
	TEST_ASSERT(!(host_get_events() &
		      EC_HOST_EVENT_MASK(EC_HOST_EVENT_POWER_BUTTON)));

	mock_power_button = 1;
	power_button_interrupt(GPIO_POWER_BUTTON_L);
	msleep(50);
	TEST_ASSERT(pb_hook_count == 0);
	TEST_ASSERT(!power_button_is_pressed());
	TEST_ASSERT(!(host_get_events() &
		      EC_HOST_EVENT_MASK(EC_HOST_EVENT_POWER_BUTTON)));

	return EC_SUCCESS;
}

void run_test(void)
{
	test_reset();

	RUN_TEST(test_hook);
	RUN_TEST(test_debounce);

	test_print_result();
}
