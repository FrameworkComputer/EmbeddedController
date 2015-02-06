/* Copyright 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Test AC input current ramp.
 */

#include "charge_manager.h"
#include "charge_ramp.h"
#include "common.h"
#include "console.h"
#include "gpio.h"
#include "hooks.h"
#include "task.h"
#include "test_util.h"
#include "timer.h"
#include "util.h"

#define TASK_EVENT_OVERCURRENT (1 << 0)

#define RAMP_STABLE_DELAY (120*SECOND)

/*
 * Time to delay for detecting the charger type. This value follows
 * the value in common/charge_ramp.c.
 */
#define CHARGE_DETECT_DELAY (2*SECOND)

static int system_load_current_ma;
static int vbus_low_current_ma = 500;
static int overcurrent_current_ma = 3000;

static int charge_limit_ma;

/* Mock functions */

int board_is_ramp_allowed(int supplier)
{
	/* Ramp for TEST4-TEST9 */
	return supplier > CHARGE_SUPPLIER_TEST3;
}

int board_is_consuming_full_charge(void)
{
	return charge_limit_ma <= system_load_current_ma;
}

int board_is_vbus_too_low(enum chg_ramp_vbus_state ramp_state)
{
	return MIN(system_load_current_ma, charge_limit_ma) >
	       vbus_low_current_ma;
}

void board_set_charge_limit(int limit_ma)
{
	charge_limit_ma = limit_ma;
	if (charge_limit_ma > overcurrent_current_ma)
		task_set_event(TASK_ID_TEST_RUNNER, TASK_EVENT_OVERCURRENT, 0);
}

int board_get_ramp_current_limit(int supplier)
{
	if (supplier == CHARGE_SUPPLIER_TEST9)
		return 1600;
	else if (supplier == CHARGE_SUPPLIER_TEST8)
		return 2400;
	else
		return 3000;
}

/* Test utilities */

static void plug_charger_with_ts(int supplier_type, int port, int min_current,
				 int vbus_low_current, int overcurrent_current,
				 timestamp_t reg_time)
{
	vbus_low_current_ma = vbus_low_current;
	overcurrent_current_ma = overcurrent_current;
	chg_ramp_charge_supplier_change(port, supplier_type, min_current,
					reg_time);
}

static void plug_charger(int supplier_type, int port, int min_current,
			 int vbus_low_current, int overcurrent_current)
{
	plug_charger_with_ts(supplier_type, port, min_current,
			     vbus_low_current, overcurrent_current,
			     get_time());
}

static void unplug_charger(void)
{
	chg_ramp_charge_supplier_change(CHARGE_PORT_NONE, CHARGE_SUPPLIER_NONE,
					0, get_time());
}

static int unplug_charger_and_check(void)
{
	unplug_charger();
	usleep(CHARGE_DETECT_DELAY);
	return charge_limit_ma == 0;
}

static int wait_stable_no_overcurrent(void)
{
	return task_wait_event(RAMP_STABLE_DELAY) != TASK_EVENT_OVERCURRENT;
}

static int is_in_range(int x, int min, int max)
{
	return x >= min && x <= max;
}

/* Tests */

static int test_no_ramp(void)
{
	system_load_current_ma = 3000;
	/* A powerful charger, but hey, you're not allowed to ramp! */
	plug_charger(CHARGE_SUPPLIER_TEST1, 0, 500, 3000, 3000);
	usleep(CHARGE_DETECT_DELAY);
	/* That's right. Start at 500 mA */
	TEST_ASSERT(charge_limit_ma == 500);
	TEST_ASSERT(wait_stable_no_overcurrent());
	/* ... and stays at 500 mA */
	TEST_ASSERT(charge_limit_ma == 500);

	TEST_ASSERT(unplug_charger_and_check());
	return EC_SUCCESS;
}

static int test_full_ramp(void)
{
	system_load_current_ma = 3000;
	/* Now you get to ramp with this 3A charger */
	plug_charger(CHARGE_SUPPLIER_TEST4, 0, 500, 3000, 3000);
	usleep(CHARGE_DETECT_DELAY);
	/* Start with something around 500 mA */
	TEST_ASSERT(is_in_range(charge_limit_ma, 500, 800));
	TEST_ASSERT(wait_stable_no_overcurrent());
	/* And ramp up to 3A */
	TEST_ASSERT(charge_limit_ma == 3000);

	TEST_ASSERT(unplug_charger_and_check());
	return EC_SUCCESS;
}

static int test_vbus_dip(void)
{
	system_load_current_ma = 3000;
	/* VBUS dips too low right before the charger shuts down */
	plug_charger(CHARGE_SUPPLIER_TEST5, 0, 1000, 1500, 1600);

	TEST_ASSERT(wait_stable_no_overcurrent());
	TEST_ASSERT(is_in_range(charge_limit_ma, 1300, 1500));

	TEST_ASSERT(unplug_charger_and_check());
	return EC_SUCCESS;
}

static int test_overcurrent(void)
{
	system_load_current_ma = 3000;
	/* Huh...VBUS doesn't dip before the charger shuts down */
	plug_charger(CHARGE_SUPPLIER_TEST6, 0, 500, 3000, 1500);
	usleep(CHARGE_DETECT_DELAY);
	/* Ramp starts at 500 mA */
	TEST_ASSERT(is_in_range(charge_limit_ma, 500, 700));

	while (task_wait_event(RAMP_STABLE_DELAY) == TASK_EVENT_OVERCURRENT) {
		/* Charger goes away but comes back after 0.6 seconds */
		unplug_charger();
		usleep(MSEC * 600);
		plug_charger(CHARGE_SUPPLIER_TEST6, 0, 500, 3000, 1500);
		usleep(CHARGE_DETECT_DELAY);
		/* Ramp restarts at 500 mA */
		TEST_ASSERT(is_in_range(charge_limit_ma, 500, 700));
	}

	TEST_ASSERT(is_in_range(charge_limit_ma, 1300, 1500));

	TEST_ASSERT(unplug_charger_and_check());
	return EC_SUCCESS;
}

static int test_switch_outlet(void)
{
	int i;

	system_load_current_ma = 3000;
	/* Here's a nice powerful charger */
	plug_charger(CHARGE_SUPPLIER_TEST7, 0, 500, 3000, 3000);

	/*
	 * Now the user decides to move it to a nearby outlet...actually
	 * he decides to move it 5 times!
	 */
	for (i = 0; i < 5; ++i) {
		usleep(SECOND * 20);
		unplug_charger();
		usleep(SECOND * 1.5);
		plug_charger(CHARGE_SUPPLIER_TEST7, 0, 500, 3000, 3000);
		usleep(CHARGE_DETECT_DELAY);
		/* Ramp restarts at 500 mA */
		TEST_ASSERT(is_in_range(charge_limit_ma, 500, 700));
	}

	/* Should still ramp up to 3000 mA */
	TEST_ASSERT(wait_stable_no_overcurrent());
	TEST_ASSERT(charge_limit_ma == 3000);

	TEST_ASSERT(unplug_charger_and_check());
	return EC_SUCCESS;
}

static int test_fast_switch(void)
{
	int i;

	system_load_current_ma = 3000;
	plug_charger(CHARGE_SUPPLIER_TEST4, 0, 500, 3000, 3000);

	/*
	 * Here comes that naughty user again, and this time he's switching
	 * outlet really quickly. Fortunately this time he only does it twice.
	 */
	for (i = 0; i < 2; ++i) {
		usleep(SECOND * 20);
		unplug_charger();
		usleep(600 * MSEC);
		plug_charger(CHARGE_SUPPLIER_TEST4, 0, 500, 3000, 3000);
		usleep(CHARGE_DETECT_DELAY);
		/* Ramp restarts at 500 mA */
		TEST_ASSERT(is_in_range(charge_limit_ma, 500, 700));
	}

	/* Should still ramp up to 3000 mA */
	TEST_ASSERT(wait_stable_no_overcurrent());
	TEST_ASSERT(charge_limit_ma == 3000);

	TEST_ASSERT(unplug_charger_and_check());
	return EC_SUCCESS;
}

static int test_overcurrent_after_switch_outlet(void)
{
	system_load_current_ma = 3000;
	/* Here's a less powerful charger */
	plug_charger(CHARGE_SUPPLIER_TEST5, 0, 500, 3000, 1500);
	usleep(SECOND * 5);

	/* Now the user decides to move it to a nearby outlet */
	unplug_charger();
	usleep(SECOND * 1.5);
	plug_charger(CHARGE_SUPPLIER_TEST5, 0, 500, 3000, 1500);

	/* Okay the user is satisified */
	while (task_wait_event(RAMP_STABLE_DELAY) == TASK_EVENT_OVERCURRENT) {
		/* Charger goes away but comes back after 0.6 seconds */
		unplug_charger();
		usleep(MSEC * 600);
		plug_charger(CHARGE_SUPPLIER_TEST5, 0, 500, 3000, 1500);
		usleep(CHARGE_DETECT_DELAY);
		/* Ramp restarts at 500 mA */
		TEST_ASSERT(is_in_range(charge_limit_ma, 500, 700));
	}

	TEST_ASSERT(is_in_range(charge_limit_ma, 1300, 1500));

	TEST_ASSERT(unplug_charger_and_check());
	return EC_SUCCESS;
}

static int test_partial_load(void)
{
	/* We have a 3A charger, but we just want 1.5A */
	system_load_current_ma = 1500;
	plug_charger(CHARGE_SUPPLIER_TEST4, 0, 500, 3000, 2500);

	/* We should end up with a little bit more than 1.5A */
	TEST_ASSERT(wait_stable_no_overcurrent());
	TEST_ASSERT(is_in_range(charge_limit_ma, 1500, 1600));

	/* Ok someone just started watching YouTube */
	system_load_current_ma = 2000;
	TEST_ASSERT(wait_stable_no_overcurrent());
	TEST_ASSERT(is_in_range(charge_limit_ma, 2000, 2100));

	/* Somehow the system load increases again */
	system_load_current_ma = 2600;
	while (task_wait_event(RAMP_STABLE_DELAY) == TASK_EVENT_OVERCURRENT) {
		/* Charger goes away but comes back after 0.6 seconds */
		unplug_charger();
		usleep(MSEC * 600);
		plug_charger(CHARGE_SUPPLIER_TEST4, 0, 500, 3000, 2500);
		usleep(CHARGE_DETECT_DELAY);
		/* Ramp restarts at 500 mA */
		TEST_ASSERT(is_in_range(charge_limit_ma, 500, 700));
	}

	/* Alright the charger isn't powerful enough, so we'll stop at 2.5A */
	TEST_ASSERT(is_in_range(charge_limit_ma, 2300, 2500));

	TEST_ASSERT(unplug_charger_and_check());
	return EC_SUCCESS;
}

static int test_charge_supplier_stable(void)
{
	system_load_current_ma = 3000;
	/* The charger says it's of type TEST4 initially */
	plug_charger(CHARGE_SUPPLIER_TEST4, 0, 500, 1500, 1600);
	/*
	 * And then it decides it's actually TEST2 after 0.5 seconds,
	 * why? Well, this charger is just evil.
	 */
	usleep(500 * MSEC);
	plug_charger(CHARGE_SUPPLIER_TEST2, 0, 3000, 3000, 3000);
	/* We should get 3A right away. */
	usleep(SECOND);
	TEST_ASSERT(charge_limit_ma == 3000);

	TEST_ASSERT(unplug_charger_and_check());
	return EC_SUCCESS;
}

static int test_charge_supplier_stable_ramp(void)
{
	system_load_current_ma = 3000;
	/* This time we start with a non-ramp charge supplier */
	plug_charger(CHARGE_SUPPLIER_TEST3, 0, 500, 3000, 3000);
	/*
	 * After 0.5 seconds, it's decided that the supplier is actually
	 * a 1.5A ramp supplier.
	 */
	usleep(500 * MSEC);
	plug_charger(CHARGE_SUPPLIER_TEST5, 0, 500, 1400, 1500);
	TEST_ASSERT(wait_stable_no_overcurrent());
	TEST_ASSERT(is_in_range(charge_limit_ma, 1200, 1400));

	TEST_ASSERT(unplug_charger_and_check());
	return EC_SUCCESS;
}

static int test_charge_supplier_change(void)
{
	system_load_current_ma = 3000;
	/* Start with a 3A ramp charge supplier */
	plug_charger(CHARGE_SUPPLIER_TEST4, 0, 500, 3000, 3000);
	TEST_ASSERT(wait_stable_no_overcurrent());
	TEST_ASSERT(charge_limit_ma == 3000);

	/* The charger decides to change type to a 1.5A non-ramp supplier */
	plug_charger(CHARGE_SUPPLIER_TEST1, 0, 1500, 3000, 3000);
	usleep(500 * MSEC);
	TEST_ASSERT(charge_limit_ma == 1500);
	TEST_ASSERT(wait_stable_no_overcurrent());
	TEST_ASSERT(charge_limit_ma == 1500);

	TEST_ASSERT(unplug_charger_and_check());
	return EC_SUCCESS;
}

static int test_charge_port_change(void)
{
	system_load_current_ma = 3000;
	/* Start with a 1.5A ramp charge supplier on port 0 */
	plug_charger(CHARGE_SUPPLIER_TEST5, 0, 500, 1400, 1500);
	TEST_ASSERT(wait_stable_no_overcurrent());
	TEST_ASSERT(is_in_range(charge_limit_ma, 1200, 1400));

	/* Here comes a 2.1A ramp charge supplier on port 1 */
	plug_charger(CHARGE_SUPPLIER_TEST6, 0, 500, 2000, 2100);
	TEST_ASSERT(wait_stable_no_overcurrent());
	TEST_ASSERT(is_in_range(charge_limit_ma, 1800, 2000));

	/* Now we have a 2.5A non-ramp charge supplier on port 0 */
	plug_charger(CHARGE_SUPPLIER_TEST1, 0, 2500, 3000, 3000);
	usleep(SECOND);
	TEST_ASSERT(charge_limit_ma == 2500);
	TEST_ASSERT(wait_stable_no_overcurrent());
	TEST_ASSERT(charge_limit_ma == 2500);

	/* Unplug on port 0 */
	plug_charger(CHARGE_SUPPLIER_TEST6, 0, 500, 2000, 2100);
	TEST_ASSERT(wait_stable_no_overcurrent());
	TEST_ASSERT(is_in_range(charge_limit_ma, 1800, 2000));

	TEST_ASSERT(unplug_charger_and_check());
	return EC_SUCCESS;
}

static int test_vbus_shift(void)
{
	system_load_current_ma = 3000;
	/*
	 * At first, the charger is able to supply up to 1900 mA before
	 * the VBUS voltage starts to drop.
	 */
	plug_charger(CHARGE_SUPPLIER_TEST7, 0, 500, 1900, 2000);
	TEST_ASSERT(wait_stable_no_overcurrent());
	TEST_ASSERT(is_in_range(charge_limit_ma, 1700, 1900));

	/* The charger heats up and VBUS voltage drops by 100mV */
	vbus_low_current_ma = 1800;
	TEST_ASSERT(wait_stable_no_overcurrent());
	TEST_ASSERT(is_in_range(charge_limit_ma, 1600, 1800));

	TEST_ASSERT(unplug_charger_and_check());
	return EC_SUCCESS;
}

static int test_equal_priority_overcurrent(void)
{
	int overcurrent_count = 0;
	timestamp_t oc_time = get_time();

	system_load_current_ma = 3000;

	/*
	 * Now we have two charge suppliers of equal priorties plugged into
	 * port 0 and port 1. If the active one browns out, charge manager
	 * switches to the other one.
	 */
	while (1) {
		plug_charger_with_ts(CHARGE_SUPPLIER_TEST4, 0, 500, 3000,
				     2000, oc_time);
		oc_time = get_time();
		oc_time.val += 600 * MSEC;
		if (wait_stable_no_overcurrent())
			break;
		plug_charger_with_ts(CHARGE_SUPPLIER_TEST4, 1, 500, 3000,
				     2000, oc_time);
		oc_time = get_time();
		oc_time.val += 600 * MSEC;
		if (wait_stable_no_overcurrent())
			break;
		if (overcurrent_count++ >= 10) {
			/*
			 * Apparently we are in a loop and can never reach
			 * stable state.
			 */
			unplug_charger();
			return EC_ERROR_UNKNOWN;
		}
	}

	TEST_ASSERT(unplug_charger_and_check());
	return EC_SUCCESS;
}

static int test_ramp_limit(void)
{
	system_load_current_ma = 3000;

	/* Plug in supplier that is limited to 1.6A */
	plug_charger(CHARGE_SUPPLIER_TEST9, 0, 500, 3000, 3000);
	usleep(SECOND);
	TEST_ASSERT(is_in_range(charge_limit_ma, 500, 700));
	TEST_ASSERT(wait_stable_no_overcurrent());
	TEST_ASSERT(charge_limit_ma == 1600);

	/* Switch to supplier that is limited to 2.4A */
	plug_charger(CHARGE_SUPPLIER_TEST8, 1, 500, 3000, 3000);
	usleep(SECOND);
	TEST_ASSERT(is_in_range(charge_limit_ma, 500, 700));
	TEST_ASSERT(wait_stable_no_overcurrent());
	TEST_ASSERT(charge_limit_ma == 2400);

	/* Go back to 1.6A limited, but VBUS goes low before that point */
	plug_charger(CHARGE_SUPPLIER_TEST9, 0, 500, 1200, 1300);
	usleep(SECOND);
	TEST_ASSERT(is_in_range(charge_limit_ma, 500, 700));
	TEST_ASSERT(wait_stable_no_overcurrent());
	TEST_ASSERT(is_in_range(charge_limit_ma, 1000, 1200));

	TEST_ASSERT(unplug_charger_and_check());
	return EC_SUCCESS;
}

void run_test(void)
{
	test_reset();

	RUN_TEST(test_no_ramp);
	RUN_TEST(test_full_ramp);
	RUN_TEST(test_vbus_dip);
	RUN_TEST(test_overcurrent);
	RUN_TEST(test_switch_outlet);
	RUN_TEST(test_fast_switch);
	RUN_TEST(test_overcurrent_after_switch_outlet);
	RUN_TEST(test_partial_load);
	RUN_TEST(test_charge_supplier_stable);
	RUN_TEST(test_charge_supplier_stable_ramp);
	RUN_TEST(test_charge_supplier_change);
	RUN_TEST(test_charge_port_change);
	RUN_TEST(test_vbus_shift);
	RUN_TEST(test_equal_priority_overcurrent);
	RUN_TEST(test_ramp_limit);

	test_print_result();
}
