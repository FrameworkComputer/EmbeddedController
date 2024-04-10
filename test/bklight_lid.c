/* Copyright 2013 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Test backlight control based on lid
 */

#include "backlight.h"
#include "common.h"
#include "console.h"
#include "hooks.h"
#include "host_command.h"
#include "lid_switch.h"
#include "test_util.h"
#include "timer.h"
#include "util.h"

static int mock_lid = 1;
static int backlight_en;

int gpio_get_level(enum gpio_signal signal)
{
	if (signal == GPIO_LID_OPEN)
		return mock_lid;
	return 0;
}

void gpio_set_level(enum gpio_signal signal, int level)
{
	if (signal == GPIO_ENABLE_BACKLIGHT)
		backlight_en = level;
}

void set_lid_state(int is_open)
{
	mock_lid = is_open;
	lid_interrupt(GPIO_LID_OPEN);
	crec_msleep(40);
}

static int send_bklight_hostcmd(int enabled)
{
	struct ec_params_switch_enable_backlight p;
	p.enabled = enabled;

	return test_send_host_command(EC_CMD_SWITCH_ENABLE_BKLIGHT, 0, &p,
				      sizeof(p), NULL, 0);
}

static int test_passthrough(void)
{
	/* Initial state */
	TEST_ASSERT(mock_lid == 1);
	TEST_ASSERT(backlight_en);

	/* Close lid. Backlight should turn off */
	set_lid_state(0);
	TEST_ASSERT(!backlight_en);

	/* Open lid. Backlight turns on */
	set_lid_state(1);
	TEST_ASSERT(backlight_en);

	return EC_SUCCESS;
}

static int test_hostcommand(void)
{
	/* Open lid */
	set_lid_state(1);
	TEST_ASSERT(backlight_en);

	/* Disable by host command */
	send_bklight_hostcmd(0);
	TEST_ASSERT(!backlight_en);

	/* Close and open lid. Backlight should come up */
	set_lid_state(0);
	set_lid_state(1);
	TEST_ASSERT(backlight_en);

	/* Close lid */
	set_lid_state(0);
	TEST_ASSERT(!backlight_en);

	/* Enable by host command */
	send_bklight_hostcmd(1);
	TEST_ASSERT(backlight_en);

	/* Disable backlight by lid */
	set_lid_state(1);
	set_lid_state(0);
	TEST_ASSERT(!backlight_en);

	return EC_SUCCESS;
}

void run_test(int argc, const char **argv)
{
	test_reset();

	RUN_TEST(test_passthrough);
	RUN_TEST(test_hostcommand);

	test_print_result();
}
