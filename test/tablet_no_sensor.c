/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Test clamshell/tablet when Only the GMR sensor is driving the tablet mode:
 * In that mode, tablet mode is entered only when the lid angle is 360 degree.
 */

#include "common.h"
#include "console.h"
#include "gpio.h"
#include "hooks.h"
#include "host_command.h"
#include "tablet_mode.h"
#include "test_util.h"
#include "timer.h"
#include "util.h"

static int tablet_hook_count;

static void tablet_mode_change_hook(void)
{
	tablet_hook_count++;
}
DECLARE_HOOK(HOOK_TABLET_MODE_CHANGE, tablet_mode_change_hook,
	     HOOK_PRIO_DEFAULT);

void before_test(void)
{
	/* Make sure the device lid is in a consistent state (close). */
	gpio_set_level(GPIO_TABLET_MODE_L, 1);
	crec_msleep(50);
	gpio_set_level(GPIO_LID_OPEN, 0);
	crec_msleep(50);
	tablet_hook_count = 1;
}

/*
 * The device is in clamshell mode from before_test,
 * Go through GPIO transitions and observe the table mode state.
 */
test_static int test_start_lid_close(void)
{
	TEST_ASSERT(!tablet_get_mode());

	/* Opening, No change. */
	gpio_set_level(GPIO_LID_OPEN, 1);
	crec_msleep(50);
	TEST_ASSERT(tablet_hook_count == 1);
	TEST_ASSERT(!tablet_get_mode());

	/* full 360, tablet mode. */
	gpio_set_level(GPIO_TABLET_MODE_L, 0);
	crec_msleep(50);
	TEST_ASSERT(tablet_hook_count == 2);
	TEST_ASSERT(tablet_get_mode());

	/* Get out of full 360 mode, Immediately back to clamshell mode. */
	gpio_set_level(GPIO_TABLET_MODE_L, 1);
	crec_msleep(50);
	TEST_ASSERT(tablet_hook_count == 3);
	TEST_ASSERT(!tablet_get_mode());

	/* Back to close, no change. */
	gpio_set_level(GPIO_LID_OPEN, 0);
	crec_msleep(50);
	TEST_ASSERT(tablet_hook_count == 3);
	TEST_ASSERT(!tablet_get_mode());

	return EC_SUCCESS;
}

/*
 * Put the device in tablet mode first.
 * Reset the EC, keep the existing GPIO level.
 * Verify the state is not forgotten when the EC starts in tablet mode after
 * reset.
 */
test_static int test_start_tablet_mode(void)
{
	/* Go in tablet mode */
	gpio_set_level(GPIO_LID_OPEN, 1);
	gpio_set_level(GPIO_TABLET_MODE_L, 0);
	crec_msleep(50);
	TEST_ASSERT(tablet_hook_count == 2);

	/* Shutdown device */
	hook_notify(HOOK_CHIPSET_SHUTDOWN);

	/* Check we start in tablet mode */
	crec_msleep(50);
	TEST_ASSERT(tablet_get_mode());

	return EC_SUCCESS;
}

void run_test(int argc, const char **argv)
{
	test_reset();

	RUN_TEST(test_start_lid_close);
	RUN_TEST(test_start_tablet_mode);

	test_print_result();
}
