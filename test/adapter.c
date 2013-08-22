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

void chipset_throttle_cpu(int throttle)
{
	/* PROCHOT, ugh. */
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

/* Specify as discharge current */
static void mock_batt(int cur)
{
	ctx.curr.batt.current = -cur;
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

static int test_batt(void)
{
	struct adapter_limits *l, *h;
	int longtime;
	int i;

	/* NB: struct adapter_limits assumes hi_val > lo_val, so the values in
	 * batt_limits[] indicate discharge current (mA).  However, the value
	 * returned from battery_current() is postive for charging, and
	 * negative for discharging.
	 */

	/* We're assuming two limits, mild and urgent. */
	TEST_ASSERT(NUM_BATT_THRESHOLDS == 2);
	/* Find out which is which */
	if (batt_limits[0].hi_val > batt_limits[1].hi_val) {
		h = &batt_limits[0];
		l = &batt_limits[1];
	} else {
		h = &batt_limits[1];
		l = &batt_limits[0];
	}

	/* Find a time longer than all sample count limits */
	for (i = longtime = 0; i < NUM_BATT_THRESHOLDS; i++)
		longtime = MAX(longtime,
			       MAX(batt_limits[i].lo_cnt,
				   batt_limits[i].hi_cnt));
	longtime += 2;

	test_reset_mocks();
	TEST_ASSERT(ap_is_throttled == 0);

	/* reset, by staying low for a long time */
	for (i = 1; i < longtime; i++)
		watch_battery_closely(&ctx);
	TEST_ASSERT(l->triggered == 0);
	TEST_ASSERT(ap_is_throttled == 0);

	/* mock_batt() specifies the DISCHARGE current. Charging
	 * should do nothing, no matter how high. */
	mock_batt(-(h->hi_val + 2));
	for (i = 1; i < longtime; i++)
		watch_battery_closely(&ctx);
	TEST_ASSERT(l->triggered == 0);
	TEST_ASSERT(ap_is_throttled == 0);

	/* midrange for a long time shouldn't do anything */
	mock_batt((l->lo_val + l->hi_val) / 2);
	for (i = 1; i < longtime; i++)
		watch_battery_closely(&ctx);
	TEST_ASSERT(l->triggered == 0);
	TEST_ASSERT(ap_is_throttled == 0);

	/* above high limit for not quite long enough */
	mock_batt(l->hi_val + 1);
	for (i = 1; i < l->hi_cnt; i++)
		watch_battery_closely(&ctx);
	TEST_ASSERT(l->count != 0);
	TEST_ASSERT(l->triggered == 0);
	TEST_ASSERT(ap_is_throttled == 0);

	/* drop below the high limit once */
	mock_batt(l->hi_val - 1);
	watch_battery_closely(&ctx);
	TEST_ASSERT(l->count == 0);
	TEST_ASSERT(l->triggered == 0);
	TEST_ASSERT(ap_is_throttled == 0);

	/* now back up */
	mock_batt(l->hi_val + 1);
	for (i = 1; i < l->hi_cnt; i++)
		watch_battery_closely(&ctx);
	TEST_ASSERT(l->count != 0);
	TEST_ASSERT(l->triggered == 0);
	TEST_ASSERT(ap_is_throttled == 0);

	/* one more ought to do it */
	watch_battery_closely(&ctx);
	TEST_ASSERT(l->triggered == 1);
	TEST_ASSERT(ap_is_throttled == 1);

	/* going midrange for a long time shouldn't change anything */
	mock_batt((l->lo_val + l->hi_val) / 2);
	for (i = 1; i < longtime; i++)
		watch_battery_closely(&ctx);
	TEST_ASSERT(l->triggered == 1);
	TEST_ASSERT(ap_is_throttled == 1);

	/* charge for not quite long enough */
	mock_batt(-1);
	for (i = 1; i < l->lo_cnt; i++)
		watch_battery_closely(&ctx);
	TEST_ASSERT(l->triggered == 1);
	TEST_ASSERT(ap_is_throttled == 1);

	/* back above the low limit once */
	mock_batt(l->lo_val + 1);
	watch_battery_closely(&ctx);
	TEST_ASSERT(l->triggered == 1);
	TEST_ASSERT(ap_is_throttled == 1);

	/* now charge again  - that should have reset the count */
	mock_batt(-1);
	for (i = 1; i < l->lo_cnt; i++)
		watch_battery_closely(&ctx);
	TEST_ASSERT(l->triggered == 1);
	TEST_ASSERT(ap_is_throttled == 1);

	/* One more ought to do it */
	watch_battery_closely(&ctx);
	TEST_ASSERT(l->triggered == 0);
	TEST_ASSERT(ap_is_throttled == 0);

	/* Check the high limits too, just for fun */
	mock_batt(h->hi_val + 1);
	for (i = 1; i < h->hi_cnt; i++)
		watch_battery_closely(&ctx);
	TEST_ASSERT(h->triggered == 0);
	/* one more */
	watch_battery_closely(&ctx);
	TEST_ASSERT(h->triggered == 1);
	TEST_ASSERT(ap_is_throttled == 1);

	return EC_SUCCESS;
}


void run_test(void)
{
	test_reset();

	RUN_TEST(test_identification);
	RUN_TEST(test_turbo);
	RUN_TEST(test_thresholds);
	RUN_TEST(test_batt);

	test_print_result();
}
