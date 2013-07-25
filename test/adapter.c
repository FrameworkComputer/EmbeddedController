/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Test GPIO extpower module.
 */

#include "adc.h"
#include "common.h"
#include "console.h"
#include "extpower.h"
#include "gpio.h"
#include "hooks.h"
#include "host_command.h"
#include "test_util.h"
#include "timer.h"
#include "util.h"
#include "chipset.h"
#include "chipset_x86_common.h"

/* Normally private stuff from the modules we're going to test */
#include "adapter_externs.h"

/* Local state */
static int mock_ac;
static int mock_id;
static int mock_current;
static struct power_state_context ctx;

static void test_reset_mocks(void)
{
	mock_ac = 0;
	mock_id = 0;
	mock_current = 0;
	memset(&ctx, 0, sizeof(ctx));
}

/* Mocked functions from the rest of the EC */

int gpio_get_level(enum gpio_signal signal)
{
	if (signal == GPIO_AC_PRESENT)
		return mock_ac;
	return 0;
}

int adc_read_channel(enum adc_channel ch)
{
	switch (ch) {
	case ADC_AC_ADAPTER_ID_VOLTAGE:
		return mock_id;
	case ADC_CH_CHARGER_CURRENT:
		return mock_current;
	default:
		break;
	}

	return 0;
}

int charger_set_input_current(int input_current)
{
	return EC_SUCCESS;
}

int charger_get_option(int *option)
{
	return EC_SUCCESS;
}


int charger_set_option(int option)
{
	return EC_SUCCESS;
}

/* Local functions to control the mocked functions. */

static void change_ac(int val)
{
	mock_ac = val;
	extpower_interrupt(GPIO_AC_PRESENT);
	msleep(50);
}

static void set_id(int val)
{
	mock_id = val;
}


/* And the tests themselves... */

/*
 * Run through the known ID ranges, making sure that values inside are
 * correctly identified, and values outside are not. We'll skip the default
 * ADAPTER_UNKNOWN range, of course.
 *
 *  NOTE: This assumes that the ranges have a gap between them.
 */
static int test_identification(void)
{
	int i;

	test_reset_mocks();

	for (i = 1; i < NUM_ADAPTER_TYPES; i++) {

		change_ac(0);
		TEST_ASSERT(ac_adapter == ADAPTER_UNKNOWN);

		set_id(ad_id_vals[i].lo - 1);
		change_ac(1);
		TEST_ASSERT(ac_adapter == ADAPTER_UNKNOWN);

		change_ac(0);
		TEST_ASSERT(ac_adapter == ADAPTER_UNKNOWN);

		set_id(ad_id_vals[i].lo);
		change_ac(1);
		TEST_ASSERT(ac_adapter == i);

		change_ac(0);
		TEST_ASSERT(ac_adapter == ADAPTER_UNKNOWN);

		set_id(ad_id_vals[i].hi);
		change_ac(1);
		TEST_ASSERT(ac_adapter == i);

		change_ac(0);
		TEST_ASSERT(ac_adapter == ADAPTER_UNKNOWN);

		set_id(ad_id_vals[i].hi + 1);
		change_ac(1);
		TEST_ASSERT(ac_adapter == ADAPTER_UNKNOWN);
	}

	return EC_SUCCESS;
}

/* Helper function */
static void test_turbo_init(void)
{
	/* Battery is awake and in good shape */
	ctx.curr.error = 0;
	ctx.curr.batt.state_of_charge = 25;

	/* Adapter is present and known */
	set_id(ad_id_vals[1].lo + 1);
	change_ac(1);
}

/* Test all the things that can turn turbo mode on and off */
static int test_turbo(void)
{
	test_reset_mocks();

	/* There's only one path that can enable turbo. Check it first. */
	test_turbo_init();
	watch_adapter_closely(&ctx);
	TEST_ASSERT(ac_turbo == 1);

	/* Now test things that turn turbo off. */

	test_turbo_init();
	ctx.curr.error = 1;
	watch_adapter_closely(&ctx);
	TEST_ASSERT(ac_turbo == 0);

	test_turbo_init();
	ctx.curr.batt.state_of_charge = 5;
	watch_adapter_closely(&ctx);
	TEST_ASSERT(ac_turbo == 0);

	test_turbo_init();
	set_id(ad_id_vals[1].lo - 1);
	change_ac(1);
	watch_adapter_closely(&ctx);
	TEST_ASSERT(ac_turbo == 0);

	test_turbo_init();
	change_ac(0);
	watch_adapter_closely(&ctx);
	TEST_ASSERT(ac_turbo == -1);

	return EC_SUCCESS;
}

/* Check the detection logic on one set of struct adapter_limits */
static int test_thresholds_sequence(int entry)
{
	struct adapter_limits *lim = &ad_limits[ac_adapter][ac_turbo][entry];
	int longtime = MAX(lim->lo_cnt, lim->hi_cnt) + 2;
	int i;

	/* reset, by staying low for a long time */
	mock_current = lim->lo_val - 1;
	for (i = 1; i < longtime; i++)
		check_threshold(mock_current, lim);
	TEST_ASSERT(lim->triggered == 0);
	TEST_ASSERT(ap_is_throttled == 0);

	/* midrange for a long time shouldn't do anything */
	mock_current = (lim->lo_val + lim->hi_val) / 2;
	for (i = 1; i < longtime; i++)
		check_threshold(mock_current, lim);
	TEST_ASSERT(lim->triggered == 0);
	TEST_ASSERT(ap_is_throttled == 0);

	/* above high limit for not quite long enough */
	mock_current = lim->hi_val + 1;
	for (i = 1; i < lim->hi_cnt; i++)
		check_threshold(mock_current, lim);
	TEST_ASSERT(lim->triggered == 0);
	TEST_ASSERT(ap_is_throttled == 0);

	/* drop below the high limit once */
	mock_current = lim->hi_val - 1;
	check_threshold(mock_current, lim);
	TEST_ASSERT(lim->triggered == 0);
	TEST_ASSERT(ap_is_throttled == 0);

	/* now back up - that should have reset the count */
	mock_current = lim->hi_val + 1;
	for (i = 1; i < lim->hi_cnt; i++)
		check_threshold(mock_current, lim);
	TEST_ASSERT(lim->triggered == 0);
	TEST_ASSERT(ap_is_throttled == 0);

	/* one more ought to do it */
	check_threshold(mock_current, lim);
	TEST_ASSERT(lim->triggered == 1);
	TEST_ASSERT(ap_is_throttled == 1);

	/* going midrange for a long time shouldn't change anything */
	mock_current = (lim->lo_val + lim->hi_val) / 2;
	for (i = 1; i < longtime; i++)
		check_threshold(mock_current, lim);
	TEST_ASSERT(lim->triggered == 1);
	TEST_ASSERT(ap_is_throttled == 1);

	/* below low limit for not quite long enough */
	mock_current = lim->lo_val - 1;
	for (i = 1; i < lim->lo_cnt; i++)
		check_threshold(mock_current, lim);
	TEST_ASSERT(lim->triggered == 1);
	TEST_ASSERT(ap_is_throttled == 1);

	/* back above the low limit once */
	mock_current = lim->lo_val + 1;
	check_threshold(mock_current, lim);
	TEST_ASSERT(lim->triggered == 1);
	TEST_ASSERT(ap_is_throttled == 1);

	/* now back down - that should have reset the count */
	mock_current = lim->lo_val - 1;
	for (i = 1; i < lim->lo_cnt; i++)
		check_threshold(mock_current, lim);
	TEST_ASSERT(lim->triggered == 1);
	TEST_ASSERT(ap_is_throttled == 1);

	/* One more ought to do it */
	check_threshold(mock_current, lim);
	TEST_ASSERT(lim->triggered == 0);
	TEST_ASSERT(ap_is_throttled == 0);

	return EC_SUCCESS;
}

/*
 * Check all sets of thresholds. This probably doesn't add much value, but at
 * least it ensures that they're somewhat sane.
 */
static int test_thresholds(void)
{
	int e;

	for (ac_adapter = 0; ac_adapter < NUM_ADAPTER_TYPES; ac_adapter++)
		for (ac_turbo = 0; ac_turbo < NUM_AC_TURBO_STATES; ac_turbo++)
			for (e = 0; e < NUM_AC_THRESHOLDS; e++)
				TEST_ASSERT(EC_SUCCESS ==
					    test_thresholds_sequence(e));

	return EC_SUCCESS;
}

void run_test(void)
{
	test_reset();

	RUN_TEST(test_identification);
	RUN_TEST(test_turbo);
	RUN_TEST(test_thresholds);

	test_print_result();
}
