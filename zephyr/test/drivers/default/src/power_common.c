/* Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "battery.h"
#include "battery_smart.h"
#include "chipset.h"
#include "common.h"
#include "ec_tasks.h"
#include "emul/emul_common_i2c.h"
#include "emul/emul_smart_battery.h"
#include "extpower.h"
#include "hooks.h"
#include "host_command.h"
#include "power.h"
#include "power/power.h"
#include "power/qcom.h"
#include "task.h"
#include "test/drivers/stubs.h"
#include "test/drivers/test_state.h"
#include "test/drivers/utils.h"

#include <string.h>

#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/gpio/gpio_emul.h>
#include <zephyr/shell/shell.h>
#include <zephyr/shell/shell_dummy.h>
#include <zephyr/shell/shell_uart.h>
#include <zephyr/ztest.h>

#define BATTERY_NODE DT_NODELABEL(battery)

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
		.transition_to = CHIPSET_STATE_HARD_OFF,
		.transition_from = CHIPSET_STATE_HARD_OFF,
	},
	{
		.p_state = POWER_G3S5,
		.transition_to = CHIPSET_STATE_SOFT_OFF,
		.transition_from = CHIPSET_STATE_HARD_OFF,
	},
	{
		.p_state = POWER_S5G3,
		.transition_to = CHIPSET_STATE_HARD_OFF,
		.transition_from = CHIPSET_STATE_SOFT_OFF,
	},
	{
		.p_state = POWER_S5,
		.transition_to = CHIPSET_STATE_SOFT_OFF,
		.transition_from = CHIPSET_STATE_SOFT_OFF,
	},
	{
		.p_state = POWER_S5S3,
		.transition_to = CHIPSET_STATE_SUSPEND,
		.transition_from = CHIPSET_STATE_SOFT_OFF,
	},
	{
		.p_state = POWER_S3S5,
		.transition_to = CHIPSET_STATE_SOFT_OFF,
		.transition_from = CHIPSET_STATE_SUSPEND,
	},
	{
		.p_state = POWER_S3,
		.transition_to = CHIPSET_STATE_SUSPEND,
		.transition_from = CHIPSET_STATE_SUSPEND,
	},
	{
		.p_state = POWER_S3S0,
		.transition_to = CHIPSET_STATE_ON,
		.transition_from = CHIPSET_STATE_SUSPEND,
	},
	{
		.p_state = POWER_S0S3,
		.transition_to = CHIPSET_STATE_SUSPEND,
		.transition_from = CHIPSET_STATE_ON,
	},
	{
		.p_state = POWER_S0,
		.transition_to = CHIPSET_STATE_ON,
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
ZTEST(power_common_no_tasks, test_power_chipset_in_state)
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
			transition_to = mask &
					test_power_state_desc[i].transition_to;
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
ZTEST(power_common_no_tasks, test_power_chipset_in_or_transitioning_to_state)
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

/* Test using chipset_exit_hard_off() in different power states. The only
 * way to test the value of want_g3_exit is to set the power state to G3
 * and then to see if test_power_common_state() transitions to G3S5 or not.
 */
ZTEST(power_common_no_tasks, test_power_exit_hard_off)
{
	/*
	 * Every test runs in a new thread, we need to add this thread to the
	 * dynamic shimmed tasks or this test will fail.
	 */
	set_test_runner_tid();

	/* Force initial state */
	power_set_state(POWER_G3);
	test_power_common_state();
	zassert_equal(POWER_G3, power_get_state());

	/* Test after exit hard off, we reach G3S5 */
	chipset_exit_hard_off();
	test_power_common_state();
	zassert_equal(POWER_G3S5, power_get_state());

	/* Go back to G3 and check we stay there */
	power_set_state(POWER_G3);
	test_power_common_state();
	zassert_equal(POWER_G3, power_get_state());

	/* Exit G3 again */
	chipset_exit_hard_off();
	test_power_common_state();
	zassert_equal(POWER_G3S5, power_get_state());

	/* Go to S5G3 */
	power_set_state(POWER_S5G3);
	test_power_common_state();
	zassert_equal(POWER_S5G3, power_get_state());

	/* Test exit hard off in S5G3 -- should set want_g3_exit */
	chipset_exit_hard_off();
	/* Go back to G3 and check we exit it to G3S5 */
	power_set_state(POWER_G3);
	test_power_common_state();
	zassert_equal(POWER_G3S5, power_get_state());

	/* Test exit hard off is cleared on entering S5 */
	chipset_exit_hard_off();
	power_set_state(POWER_S5);
	test_power_common_state();
	zassert_equal(POWER_S5, power_get_state());

	/* Go back to G3 and check we stay in G3 */
	power_set_state(POWER_G3);
	test_power_common_state();
	zassert_equal(POWER_G3, power_get_state());

	/* Test exit hard off doesn't work on other states */
	power_set_state(POWER_S5S3);
	test_power_common_state();
	zassert_equal(POWER_S5S3, power_get_state());
	chipset_exit_hard_off();
	test_power_common_state();

	/* Go back to G3 and check we stay in G3 */
	power_set_state(POWER_G3);
	test_power_common_state();
	zassert_equal(POWER_G3, power_get_state());
}

/* Test reboot ap on g3 host command is triggering reboot */
ZTEST(power_common_no_tasks, test_power_reboot_ap_at_g3)
{
	struct ec_params_reboot_ap_on_g3_v1 params;
	int delay_ms;
	int64_t before_time;

	/*
	 * Every test runs in a new thread, we need to add this thread to the
	 * dynamic shimmed tasks or this test will fail.
	 */
	set_test_runner_tid();

	/* Force initial state S0 */
	power_set_state(POWER_S0);
	test_power_common_state();
	zassert_equal(POWER_S0, power_get_state());

	/* Test version 0 (no delay argument) */
	zassert_equal(EC_RES_SUCCESS, ec_cmd_reboot_ap_on_g3(NULL));

	/* Go to G3 and check if reboot is triggered */
	power_set_state(POWER_G3);
	test_power_common_state();
	zassert_equal(POWER_G3S5, power_get_state());

	/* Test version 1 (with delay argument) */
	delay_ms = 3000;
	params.reboot_ap_at_g3_delay = delay_ms / 1000; /* in seconds */
	zassert_equal(EC_RES_SUCCESS, ec_cmd_reboot_ap_on_g3_v1(NULL, &params));

	/* Go to G3 and check if reboot is triggered after delay */
	power_set_state(POWER_G3);
	before_time = k_uptime_get();
	test_power_common_state();
	zassert_true(k_uptime_delta(&before_time) >= 3000);
	zassert_equal(POWER_G3S5, power_get_state());
}

/** Test setting cutoff and stay-up battery levels through host command */
ZTEST(power_common, test_power_hc_smart_discharge)
{
	struct ec_response_smart_discharge response;
	struct ec_params_smart_discharge params;
	const struct emul *emul = EMUL_DT_GET(BATTERY_NODE);
	struct i2c_common_emul_data *common_data =
		emul_smart_battery_get_i2c_common_data(emul);
	int hours_to_zero;
	int hibern_drate;
	int cutoff_drate;
	int stayup_cap;
	int cutoff_cap;

	/* Set up host command parameters */
	params.flags = EC_SMART_DISCHARGE_FLAGS_SET;

	/* Test fail when battery capacity is not available */
	i2c_common_emul_set_read_fail_reg(common_data, SB_FULL_CHARGE_CAPACITY);
	zassert_equal(EC_RES_UNAVAILABLE,
		      ec_cmd_smart_discharge(NULL, &params, &response));
	i2c_common_emul_set_read_fail_reg(common_data,
					  I2C_COMMON_EMUL_NO_FAIL_REG);

	/* Setup discharge rates */
	params.drate.hibern = 10;
	params.drate.cutoff = 100;
	/* Test fail on higher discahrge in hibernation than cutoff */
	zassert_equal(EC_RES_INVALID_PARAM,
		      ec_cmd_smart_discharge(NULL, &params, &response));

	/* Setup discharge rates */
	params.drate.hibern = 10;
	params.drate.cutoff = 0;
	/* Test fail on only one discharge rate set to 0 */
	zassert_equal(EC_RES_INVALID_PARAM,
		      ec_cmd_smart_discharge(NULL, &params, &response));

	/* Setup correct parameters */
	hours_to_zero = 1000;
	hibern_drate = 100; /* uA */
	cutoff_drate = 10; /* uA */
	/* Need at least 100 mA capacity to stay 1000h using 0.1mAh */
	stayup_cap = hibern_drate * hours_to_zero / 1000;
	/* Need at least 10 mA capacity to stay 1000h using 0.01mAh */
	cutoff_cap = cutoff_drate * hours_to_zero / 1000;

	params.drate.hibern = hibern_drate;
	params.drate.cutoff = cutoff_drate;
	params.hours_to_zero = hours_to_zero;

	/* Test if correct values are set */
	zassert_equal(EC_RES_SUCCESS,
		      ec_cmd_smart_discharge(NULL, &params, &response));
	zassert_equal(hibern_drate, response.drate.hibern);
	zassert_equal(cutoff_drate, response.drate.cutoff);
	zassert_equal(hours_to_zero, response.hours_to_zero);
	zassert_equal(stayup_cap, response.dzone.stayup);
	zassert_equal(cutoff_cap, response.dzone.cutoff);

	/* Setup discharge rate to 0 */
	params.drate.hibern = 0;
	params.drate.cutoff = 0;
	/* Update hours to zero */
	hours_to_zero = 2000;
	params.hours_to_zero = hours_to_zero;
	/* Need at least 200 mA capacity to stay 2000h using 0.1mAh */
	stayup_cap = hibern_drate * hours_to_zero / 1000;
	/* Need at least 20 mA capacity to stay 2000h using 0.01mAh */
	cutoff_cap = cutoff_drate * hours_to_zero / 1000;

	/* Test that command doesn't change drate but apply new hours to zero */
	zassert_equal(EC_RES_SUCCESS,
		      ec_cmd_smart_discharge(NULL, &params, &response));
	zassert_equal(hibern_drate, response.drate.hibern);
	zassert_equal(cutoff_drate, response.drate.cutoff);
	zassert_equal(hours_to_zero, response.hours_to_zero);
	zassert_equal(stayup_cap, response.dzone.stayup);
	zassert_equal(cutoff_cap, response.dzone.cutoff);

	/* Setup any parameters != 0 */
	params.drate.hibern = 1000;
	params.drate.cutoff = 1000;
	/* Clear set flag */
	params.flags = 0;

	/* Test that command doesn't change drate and dzone */
	zassert_equal(EC_RES_SUCCESS,
		      ec_cmd_smart_discharge(NULL, &params, &response));
	zassert_equal(hibern_drate, response.drate.hibern);
	zassert_equal(cutoff_drate, response.drate.cutoff);
	zassert_equal(hours_to_zero, response.hours_to_zero);
	zassert_equal(stayup_cap, response.dzone.stayup);
	zassert_equal(cutoff_cap, response.dzone.cutoff);
}

/**
 * Test if default board_system_is_idle() recognize cutoff and stay-up
 * levels correctly.
 */
ZTEST(power_common, test_power_board_system_is_idle)
{
	struct ec_response_smart_discharge response;
	struct ec_params_smart_discharge params;
	struct sbat_emul_bat_data *bat;
	const struct emul *emul = EMUL_DT_GET(BATTERY_NODE);
	struct i2c_common_emul_data *common_data =
		emul_smart_battery_get_i2c_common_data(emul);
	uint64_t last_shutdown_time = 0;
	uint64_t target;
	uint64_t now;

	bat = sbat_emul_get_bat_data(emul);

	/* Set up host command parameters */
	params.drate.hibern = 100; /* uA */
	params.drate.cutoff = 10; /* uA */
	params.hours_to_zero = 1000; /* h */
	params.flags = EC_SMART_DISCHARGE_FLAGS_SET;
	/* Set stay-up and cutoff zones */
	zassert_equal(EC_RES_SUCCESS,
		      ec_cmd_smart_discharge(NULL, &params, &response));

	/* Test shutdown ignore is send when target time is in future */
	target = 1125;
	now = 1000;
	zassert_equal(CRITICAL_SHUTDOWN_IGNORE,
		      board_system_is_idle(last_shutdown_time, &target, now),
		      NULL);

	/* Set "now" time after target time */
	now = target + 30;

	/*
	 * Test hibernation is requested when battery remaining capacity
	 * is not available
	 */
	i2c_common_emul_set_read_fail_reg(common_data, SB_REMAINING_CAPACITY);
	zassert_equal(CRITICAL_SHUTDOWN_HIBERNATE,
		      board_system_is_idle(last_shutdown_time, &target, now),
		      NULL);
	i2c_common_emul_set_read_fail_reg(common_data,
					  I2C_COMMON_EMUL_NO_FAIL_REG);

	/* Setup remaining capacity to trigger cutoff */
	bat->cap = response.dzone.cutoff - 5;
	zassert_equal(CRITICAL_SHUTDOWN_CUTOFF,
		      board_system_is_idle(last_shutdown_time, &target, now),
		      NULL);

	/* Setup remaining capacity to trigger stay-up and ignore shutdown */
	bat->cap = response.dzone.stayup - 5;
	zassert_equal(CRITICAL_SHUTDOWN_IGNORE,
		      board_system_is_idle(last_shutdown_time, &target, now),
		      NULL);

	/* Setup remaining capacity to be in safe zone to hibernate */
	bat->cap = response.dzone.stayup + 5;
	zassert_equal(CRITICAL_SHUTDOWN_HIBERNATE,
		      board_system_is_idle(last_shutdown_time, &target, now),
		      NULL);
}

/**
 * Test power console command
 */
ZTEST(power_common, test_power_console_cmd)
{
	const char *buffer;
	size_t buffer_size;

	test_set_chipset_to_g3();
	shell_backend_dummy_clear_output(get_ec_shell());
	zassert_equal(EC_SUCCESS, shell_execute_cmd(get_ec_shell(), "power"),
		      NULL);
	buffer = shell_backend_dummy_get_output(get_ec_shell(), &buffer_size);
	zassert_true(strcmp(buffer, "\r\noff\r\n") == 0 ||
			     strcmp(buffer, "\r\nOFF\r\n") == 0,
		     "Invalid console output %s", buffer);

	test_set_chipset_to_s0();
	shell_backend_dummy_clear_output(get_ec_shell());
	zassert_equal(EC_SUCCESS, shell_execute_cmd(get_ec_shell(), "power"),
		      NULL);
	buffer = shell_backend_dummy_get_output(get_ec_shell(), &buffer_size);
	zassert_true(strcmp(buffer, "\r\non\r\n") == 0 ||
			     strcmp(buffer, "\r\nON\r\n") == 0,
		     "Invalid console output %s", buffer);

	zassert_equal(EC_ERROR_PARAM1,
		      shell_execute_cmd(get_ec_shell(), "power x"), NULL);

	zassert_equal(EC_SUCCESS, shell_execute_cmd(get_ec_shell(), "power on"),
		      NULL);

	zassert_equal(EC_SUCCESS,
		      shell_execute_cmd(get_ec_shell(), "power off"), NULL);
}

/**
 * Test powerinfo console command
 */
ZTEST_USER(power_common, test_powerinfo_console_cmd)
{
	char expected_buffer[32];

	snprintf(expected_buffer, sizeof(expected_buffer), "power state %d",
		 power_get_state());

	CHECK_CONSOLE_CMD("powerinfo", expected_buffer, EC_SUCCESS);
}

/**
 * Common setup for hibernation delay tests. Smart discharge zone is setup,
 * battery is set in safe zone (which trigger hibernation), power state is
 * set to G3 and AC is disabled. system_hibernate mock is reset.
 */
static void setup_hibernation_delay(void *state)
{
	struct ec_response_smart_discharge response;
	struct ec_params_smart_discharge params;
	struct sbat_emul_bat_data *bat;
	const struct emul *emul = EMUL_DT_GET(BATTERY_NODE);
	ARG_UNUSED(state);

	bat = sbat_emul_get_bat_data(emul);

	/* Setup smart discharge zone and set capacity to safe zone */
	params.drate.hibern = 100; /* uA */
	params.drate.cutoff = 10; /* uA */
	params.hours_to_zero = 10000; /* h */
	params.flags = EC_SMART_DISCHARGE_FLAGS_SET;
	zassert_equal(EC_RES_SUCCESS,
		      ec_cmd_smart_discharge(NULL, &params, &response));
	/*
	 * Make sure that battery is in safe zone in good condition to
	 * not trigger hibernate in charge_state.c
	 */
	bat->cap = response.dzone.stayup + 5;
	bat->volt = battery_get_info()->voltage_normal;

	/* Force initial state */
	test_set_chipset_to_g3();

	/* Disable AC */
	set_ac_enabled(false);

	RESET_FAKE(system_hibernate);
}

/** Test setting hibernation delay through host command */
ZTEST(power_common_hibernation, test_power_hc_hibernation_delay)
{
	struct ec_response_hibernation_delay response;
	struct ec_params_hibernation_delay params;
	uint32_t h_delay;
	int sleep_time;

	/* Ensure the lid is closed so AC connect does not boot system */
	zassert_ok(shell_execute_cmd(get_ec_shell(), "lidclose"));

	zassert_equal(power_get_state(), POWER_G3,
		      "Power state is %d, expected G3", power_get_state());
	/* This is a no-op, but it will reset the last_shutdown_time. */
	power_set_state(POWER_G3);

	/* Set hibernate delay */
	h_delay = 9;
	params.seconds = h_delay;
	zassert_equal(EC_RES_SUCCESS,
		      ec_cmd_hibernation_delay(NULL, &params, &response));

	zassert_equal(0, response.time_g3, "Time from last G3 enter %d != 0",
		      response.time_g3);
	zassert_equal(h_delay, response.time_remaining,
		      "Time to hibernation %d != %d", response.time_remaining,
		      h_delay);
	zassert_equal(h_delay, response.hibernate_delay,
		      "Hibernation delay %d != %d", h_delay,
		      response.hibernate_delay);

	/* Kick chipset task to process new hibernation delay */
	task_wake(TASK_ID_CHIPSET);
	/* Wait some arbitrary time less than hibernate delay */
	sleep_time = 6;
	k_msleep(sleep_time * 1000);

	/* Get hibernate delay */
	params.seconds = 0;
	zassert_equal(EC_RES_SUCCESS,
		      ec_cmd_hibernation_delay(NULL, &params, &response));

	zassert_equal(sleep_time, response.time_g3,
		      "Time from last G3 enter %d != %d", response.time_g3,
		      sleep_time);
	zassert_equal(h_delay - sleep_time, response.time_remaining,
		      "Time to hibernation %d != %d", response.time_remaining,
		      h_delay - sleep_time);
	zassert_equal(h_delay, response.hibernate_delay,
		      "Hibernation delay %d != %d", h_delay,
		      response.hibernate_delay);
	zassert_equal(0, system_hibernate_fake.call_count,
		      "system_hibernate() shouldn't be called before delay");

	/* Wait to end of the hibenate delay */
	k_msleep((h_delay - sleep_time) * 1000);

	/* Get hibernate delay */
	params.seconds = 0;
	zassert_equal(EC_RES_SUCCESS,
		      ec_cmd_hibernation_delay(NULL, &params, &response));

	zassert_equal(h_delay, response.time_g3,
		      "Time from last G3 enter %d != %d", response.time_g3,
		      h_delay);
	zassert_equal(0, response.time_remaining, "Time to hibernation %d != 0",
		      response.time_remaining);
	zassert_equal(h_delay, response.hibernate_delay,
		      "Hibernation delay %d != %d", h_delay,
		      response.hibernate_delay);
	zassert_equal(1, system_hibernate_fake.call_count,
		      "system_hibernate() should be called after delay %d",
		      system_hibernate_fake.call_count);

	/* Wait some more time */
	k_msleep(2000);

	/* Get hibernate delay */
	params.seconds = 0;
	zassert_equal(EC_RES_SUCCESS,
		      ec_cmd_hibernation_delay(NULL, &params, &response));

	/* After hibernation, remaining time shouldn't be negative */
	zassert_equal(0, response.time_remaining, "Time to hibernation %d != 0",
		      response.time_remaining);

	/* Enable AC */
	set_ac_enabled(true);

	/* Reset system_hibernate fake to check that it is not called on AC */
	RESET_FAKE(system_hibernate);
	/* Allow chipset task to spin with enabled AC */
	task_wake(TASK_ID_CHIPSET);
	k_msleep(1);

	/* Get hibernate delay */
	params.seconds = 0;
	zassert_equal(EC_RES_SUCCESS,
		      ec_cmd_hibernation_delay(NULL, &params, &response));

	zassert_equal(0, response.time_g3,
		      "Time from last G3 enter %d should be 0 on AC",
		      response.time_g3);
	zassert_equal(0, system_hibernate_fake.call_count,
		      "system_hibernate() shouldn't be called on AC");

	/* Disable AC */
	set_ac_enabled(false);

	/* Go to different state */
	power_set_state(POWER_G3S5);
	zassert_equal(POWER_G3S5, power_get_state());

	/* Get hibernate delay */
	params.seconds = 0;
	zassert_equal(EC_RES_SUCCESS,
		      ec_cmd_hibernation_delay(NULL, &params, &response));

	zassert_equal(0, response.time_g3,
		      "Time from last G3 enter %d should be 0 on state != G3",
		      response.time_g3);
}

/** Test setting hibernation delay through UART command */
ZTEST(power_common_hibernation, test_power_cmd_hibernation_delay)
{
	uint32_t h_delay;
	int sleep_time;

	zassert_equal(power_get_state(), POWER_G3,
		      "Power state is %d, expected G3", power_get_state());
	/* This is a no-op, but it will reset the last_shutdown_time. */
	power_set_state(POWER_G3);

	/* Test success on call without argument */
	zassert_equal(EC_SUCCESS, shell_execute_cmd(get_ec_shell(), "hibdelay"),
		      NULL);

	/* Test error on hibernation delay argument that is not a number */
	zassert_equal(EC_ERROR_PARAM1,
		      shell_execute_cmd(get_ec_shell(), "hibdelay test1"),
		      NULL);

	/* Set hibernate delay */
	h_delay = 3;
	zassert_equal(EC_SUCCESS,
		      shell_execute_cmd(get_ec_shell(), "hibdelay 3"), NULL);

	/* Kick chipset task to process new hibernation delay */
	task_wake(TASK_ID_CHIPSET);
	/* Wait some arbitrary time less than hibernate delay */
	sleep_time = 2;
	k_msleep(sleep_time * 1000);

	zassert_equal(0, system_hibernate_fake.call_count,
		      "system_hibernate() shouldn't be called before delay");

	/* Wait to end of the hibenate delay */
	k_msleep((h_delay - sleep_time) * 1000);

	zassert_equal(1, system_hibernate_fake.call_count,
		      "system_hibernate() should be called after delay %d",
		      system_hibernate_fake.call_count);
}

/**
 * @brief GPIO test setup handler.
 */
static void siglog_before(void *state)
{
	/* enable chipset channel */
	zassert_ok(shell_execute_cmd(get_ec_shell(), "chan chipset"));
}

static void siglog_after(void *state)
{
	/* disable chipset channel */
	zassert_ok(shell_execute_cmd(get_ec_shell(), "chan chipset"));
}

#ifdef CONFIG_PLATFORM_EC_BRINGUP
ZTEST(power_common_bring_up, test_siglog_output)
{
	size_t buffer_size;
	const char *buffer;
	const struct gpio_dt_spec *gp_pwr_good =
		GPIO_DT_FROM_NODELABEL(gpio_mb_power_good);

	/* wait for the power state stabilized */
	k_msleep(10 * 1000);

	shell_backend_dummy_clear_output(get_ec_shell());
	/* test short logs */
	gpio_pin_set_dt(gp_pwr_good, 1);
	k_msleep(10);
	gpio_pin_set_dt(gp_pwr_good, 0);
	/* ensure the signal output printed */
	k_msleep(2000);

	buffer = shell_backend_dummy_get_output(get_ec_shell(), &buffer_size);
	zassert_not_equal(NULL, strstr(buffer, "2 signal changes:"));
	zassert_not_equal(NULL,
			  strstr(buffer, "+0.000000  mb_power_good => 1"));
	zassert_not_equal(NULL, strstr(buffer, "mb_power_good => 0"));
	zassert_equal(NULL, strstr(buffer, "SIGNAL LOG TRUNCATED..."));

	/* test signal log truncated */
	shell_backend_dummy_clear_output(get_ec_shell());
	for (int i = 0; i < 13; i++) {
		gpio_pin_set_dt(gp_pwr_good, 1);
		k_msleep(10);
		gpio_pin_set_dt(gp_pwr_good, 0);
		k_msleep(10);
	}
	/* ensure the signal output printed */
	k_msleep(2000);

	/* ensure the signal output printed */
	buffer = shell_backend_dummy_get_output(get_ec_shell(), &buffer_size);
	zassert_not_equal(NULL, strstr(buffer, "24 signal changes:"));
	zassert_not_equal(NULL,
			  strstr(buffer, "+0.000000  mb_power_good => 1"));
	zassert_not_equal(NULL, strstr(buffer, "SIGNAL LOG TRUNCATED..."));
}
#endif

ZTEST_SUITE(power_common_no_tasks, drivers_predicate_pre_main, NULL, NULL, NULL,
	    NULL);

ZTEST_SUITE(power_common, drivers_predicate_post_main, NULL, NULL, NULL, NULL);

ZTEST_SUITE(power_common_hibernation, drivers_predicate_post_main, NULL,
	    setup_hibernation_delay, NULL, NULL);

ZTEST_SUITE(power_common_bring_up, drivers_predicate_post_main, NULL,
	    siglog_before, siglog_after, NULL);
