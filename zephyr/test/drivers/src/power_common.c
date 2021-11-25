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

/** Test using chipset_exit_hard_off() in different power states */
static void test_power_exit_hard_off(void)
{
	/* Force initial state */
	force_power_state(true, POWER_G3);
	zassert_equal(POWER_G3, power_get_state(), NULL);

	/* Stop forcing state */
	force_power_state(false, 0);

	/* Test after exit hard off, we reach G3S5 */
	chipset_exit_hard_off();
	/*
	 * TODO(b/201420132) - chipset_exit_hard_off() is waking up
	 * TASK_ID_CHIPSET Sleep is required to run chipset task before
	 * continuing with test
	 */
	k_msleep(1);
	zassert_equal(POWER_G3S5, power_get_state(), NULL);

	/* Go back to G3 and check we stay there */
	force_power_state(true, POWER_G3);
	force_power_state(false, 0);
	zassert_equal(POWER_G3, power_get_state(), NULL);

	/* Exit G3 again */
	chipset_exit_hard_off();
	/* TODO(b/201420132) - see comment above */
	k_msleep(1);
	zassert_equal(POWER_G3S5, power_get_state(), NULL);

	/* Go to S5G3 */
	force_power_state(true, POWER_S5G3);
	zassert_equal(POWER_S5G3, power_get_state(), NULL);

	/* Test exit hard off in S5G3 -- should immedietly exit G3 */
	chipset_exit_hard_off();
	/* Go back to G3 and check we exit it to G3S5 */
	force_power_state(true, POWER_G3);
	zassert_equal(POWER_G3S5, power_get_state(), NULL);

	/* Test exit hard off is cleared on entering S5 */
	chipset_exit_hard_off();
	force_power_state(true, POWER_S5);
	zassert_equal(POWER_S5, power_get_state(), NULL);
	/* Go back to G3 and check we stay in G3 */
	force_power_state(true, POWER_G3);
	force_power_state(false, 0);
	zassert_equal(POWER_G3, power_get_state(), NULL);

	/* Test exit hard off doesn't work on other states */
	force_power_state(true, POWER_S5S3);
	force_power_state(false, 0);
	zassert_equal(POWER_S5S3, power_get_state(), NULL);
	chipset_exit_hard_off();
	/* TODO(b/201420132) - see comment above */
	k_msleep(1);

	/* Go back to G3 and check we stay in G3 */
	force_power_state(true, POWER_G3);
	force_power_state(false, 0);
	zassert_equal(POWER_G3, power_get_state(), NULL);
}

/* Test reboot ap on g3 host command is triggering reboot */
static void test_power_reboot_ap_at_g3(void)
{
	struct ec_params_reboot_ap_on_g3_v1 params;
	struct host_cmd_handler_args args = {
		.command = EC_CMD_REBOOT_AP_ON_G3,
		.version = 0,
		.send_response = stub_send_response_callback,
		.params = &params,
		.params_size = sizeof(params),
	};
	int offset_for_still_in_g3_test;
	int delay_ms;

	/* Force initial state S0 */
	force_power_state(true, POWER_S0);
	zassert_equal(POWER_S0, power_get_state(), NULL);

	/* Test version 0 (no delay argument) */
	zassert_equal(EC_RES_SUCCESS, host_command_process(&args), NULL);

	/* Go to G3 and check if reboot is triggered */
	force_power_state(true, POWER_G3);
	zassert_equal(POWER_G3S5, power_get_state(), NULL);

	/* Test version 1 (with delay argument) */
	args.version = 1;
	delay_ms = 3000;
	params.reboot_ap_at_g3_delay = delay_ms / 1000; /* in seconds */
	zassert_equal(EC_RES_SUCCESS, host_command_process(&args), NULL);

	/* Go to G3 and check if reboot is triggered after delay */
	force_power_state(true, POWER_G3);
	force_power_state(false, 0);
	zassert_equal(POWER_G3, power_get_state(), NULL);
	/*
	 * Arbitrary chosen offset before end of reboot delay to check if G3
	 * state wasn't left too soon
	 */
	offset_for_still_in_g3_test = 50;
	k_msleep(delay_ms - offset_for_still_in_g3_test);
	/* Test if still in G3 */
	zassert_equal(POWER_G3, power_get_state(), NULL);
	/*
	 * power_common_state() use for loop with 100ms sleeps. msleep() wait at
	 * least specified time, so wait 10% longer than specified delay to take
	 * this into account.
	 */
	k_msleep(offset_for_still_in_g3_test + delay_ms / 10);
	/* Test if reboot is triggered */
	zassert_equal(POWER_G3S5, power_get_state(), NULL);
}

void test_suite_power_common(void)
{
	ztest_test_suite(power_common,
			 ztest_unit_test(test_power_chipset_in_state),
			 ztest_unit_test(
			       test_power_chipset_in_or_transitioning_to_state),
			 ztest_unit_test(test_power_exit_hard_off),
			 ztest_unit_test(test_power_reboot_ap_at_g3));
	ztest_run_test_suite(power_common);
}
