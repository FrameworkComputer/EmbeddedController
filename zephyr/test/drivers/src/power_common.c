/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <ztest.h>

#include "chipset.h"
#include "common.h"
#include "host_command.h"
#include "power.h"
#include "stubs.h"
#include "task.h"

/* Description of all power states with chipset state masks */
static struct {
	/* Power state */
	enum power_state p_state;
	/*
	 * CHIPSET_STATE_* to which this state transition (the same as
	 * transition_from for static states)
	 */
	int transition_to;
	/* CHIPSET_STATE_* from which this state transition */
	int transition_from;
} test_power_state_desc[] = {
	{
		.p_state = POWER_G3,
		.transition_to   = CHIPSET_STATE_HARD_OFF,
		.transition_from = CHIPSET_STATE_HARD_OFF,
	},
	{
		.p_state = POWER_G3S5,
		.transition_to   = CHIPSET_STATE_SOFT_OFF,
		.transition_from = CHIPSET_STATE_HARD_OFF,
	},
	{
		.p_state = POWER_S5G3,
		.transition_to   = CHIPSET_STATE_HARD_OFF,
		.transition_from = CHIPSET_STATE_SOFT_OFF,
	},
	{
		.p_state = POWER_S5,
		.transition_to   = CHIPSET_STATE_SOFT_OFF,
		.transition_from = CHIPSET_STATE_SOFT_OFF,
	},
	{
		.p_state = POWER_S5S3,
		.transition_to   = CHIPSET_STATE_SUSPEND,
		.transition_from = CHIPSET_STATE_SOFT_OFF,
	},
	{
		.p_state = POWER_S3S5,
		.transition_to   = CHIPSET_STATE_SOFT_OFF,
		.transition_from = CHIPSET_STATE_SUSPEND,
	},
	{
		.p_state = POWER_S3,
		.transition_to   = CHIPSET_STATE_SUSPEND,
		.transition_from = CHIPSET_STATE_SUSPEND,
	},
	{
		.p_state = POWER_S3S0,
		.transition_to   = CHIPSET_STATE_ON,
		.transition_from = CHIPSET_STATE_SUSPEND,
	},
	{
		.p_state = POWER_S0S3,
		.transition_to   = CHIPSET_STATE_SUSPEND,
		.transition_from = CHIPSET_STATE_ON,
	},
	{
		.p_state = POWER_S0,
		.transition_to   = CHIPSET_STATE_ON,
		.transition_from = CHIPSET_STATE_ON,
	},
};

/*
 * Chipset state masks used by chipset_in_state and
 * chipset_in_or_transitioning_to_state tests
 */
static int in_state_test_masks[] = {
	CHIPSET_STATE_HARD_OFF,
	CHIPSET_STATE_SOFT_OFF,
	CHIPSET_STATE_SUSPEND,
	CHIPSET_STATE_ON,
	CHIPSET_STATE_STANDBY,
	CHIPSET_STATE_ANY_OFF,
	CHIPSET_STATE_ANY_SUSPEND,
	CHIPSET_STATE_ANY_SUSPEND | CHIPSET_STATE_SOFT_OFF,
};

/** Test chipset_in_state() for each state */
static void test_power_chipset_in_state(void)
{
	bool expected_in_state;
	bool transition_from;
	bool transition_to;
	bool in_state;
	int mask;

	for (int i = 0; i < ARRAY_SIZE(test_power_state_desc); i++) {
		/* Set given power state */
		power_set_state(test_power_state_desc[i].p_state);
		/* Test with selected state masks */
		for (int j = 0; j < ARRAY_SIZE(in_state_test_masks); j++) {
			mask = in_state_test_masks[j];
			/*
			 * Currently tested mask match with state if it match
			 * with transition_to and from chipset states
			 */
			transition_to =
				mask & test_power_state_desc[i].transition_to;
			transition_from =
				mask & test_power_state_desc[i].transition_from;
			expected_in_state = transition_to && transition_from;
			in_state = chipset_in_state(mask);
			zassert_equal(expected_in_state, in_state,
				      "Wrong chipset_in_state() == %d, "
				      "should be %d; mask 0x%x; power state %d "
				      "in test case %d",
				      in_state, expected_in_state, mask,
				      test_power_state_desc[i].p_state, i);
		}
	}
}

/** Test chipset_in_or_transitioning_to_state() for each state */
static void test_power_chipset_in_or_transitioning_to_state(void)
{
	bool expected_in_state;
	bool in_state;
	int mask;

	for (int i = 0; i < ARRAY_SIZE(test_power_state_desc); i++) {
		/* Set given power state */
		power_set_state(test_power_state_desc[i].p_state);
		/* Test with selected state masks */
		for (int j = 0; j < ARRAY_SIZE(in_state_test_masks); j++) {
			mask = in_state_test_masks[j];
			/*
			 * Currently tested mask match with state if it match
			 * with transition_to chipset state
			 */
			expected_in_state =
				mask & test_power_state_desc[i].transition_to;
			in_state = chipset_in_or_transitioning_to_state(mask);
			zassert_equal(expected_in_state, in_state,
				      "Wrong "
				      "chipset_in_or_transitioning_to_state() "
				      "== %d, should be %d; mask 0x%x; "
				      "power state %d in test case %d",
				      in_state, expected_in_state, mask,
				      test_power_state_desc[i].p_state, i);
		}
	}
}

void test_suite_power_common(void)
{
	ztest_test_suite(power_common,
			 ztest_unit_test(test_power_chipset_in_state),
			 ztest_unit_test(
			       test_power_chipset_in_or_transitioning_to_state)
			);
	ztest_run_test_suite(power_common);
}
