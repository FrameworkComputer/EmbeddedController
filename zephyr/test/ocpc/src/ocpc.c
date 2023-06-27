/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include "charge_state.h"
#include "console.h"
#include "host_command.h"
#include "ocpc.h"

#include <string.h>

#include <zephyr/drivers/emul.h>
#include <zephyr/fff.h>
#include <zephyr/shell/shell_dummy.h>
#include <zephyr/ztest.h>

FAKE_VOID_FUNC(ocpc_get_pid_constants, int *, int *, int *, int *, int *,
	       int *);

static int test_kp, test_kp_div, test_ki, test_ki_div, test_kd, test_kd_div;

static void get_pid_constants_custom_fake(int *kp, int *kp_div, int *ki,
					  int *ki_div, int *kd, int *kd_div)
{
	*kp = test_kp;
	*kp_div = test_kp_div;
	*ki = test_ki;
	*ki_div = test_ki_div;
	*kd = test_kd;
	*kd_div = test_kd_div;
}

ZTEST_USER(ocpc, test_consolecmd_ocpcpid__read)
{
	const char *outbuffer;
	size_t buffer_size;

	/* With no args, print current state */
	shell_backend_dummy_clear_output(get_ec_shell());
	zassert_ok(shell_execute_cmd(get_ec_shell(), "ocpcpid"));
	outbuffer =
		shell_backend_dummy_get_output(get_ec_shell(), &buffer_size);

	/* Check for some expected lines */
	zassert_true(buffer_size > 0);
	zassert_ok(!strstr(outbuffer, "Kp = 1 / 4"), "Output was: `%s`",
		   outbuffer);
	zassert_ok(!strstr(outbuffer, "Ki = 1 / 15"), "Output was: `%s`",
		   outbuffer);
	zassert_ok(!strstr(outbuffer, "Kd = 1 / 10"), "Output was: `%s`",
		   outbuffer);
}

ZTEST_USER(ocpc, test_consolecmd_ocpcpid__write)
{
	const char *outbuffer;
	size_t buffer_size;

	/* Call a few times to change each parameter and examine output of final
	 * command.
	 */

	zassert_ok(shell_execute_cmd(get_ec_shell(), "ocpcpid p 2 3"));
	zassert_ok(shell_execute_cmd(get_ec_shell(), "ocpcpid i 4 5"));
	shell_backend_dummy_clear_output(get_ec_shell());
	zassert_ok(shell_execute_cmd(get_ec_shell(), "ocpcpid d 6 7"));
	outbuffer =
		shell_backend_dummy_get_output(get_ec_shell(), &buffer_size);

	zassert_true(buffer_size > 0);
	zassert_ok(!strstr(outbuffer, "Kp = 2 / 3"), "Output was: `%s`",
		   outbuffer);
	zassert_ok(!strstr(outbuffer, "Ki = 4 / 5"), "Output was: `%s`",
		   outbuffer);
	zassert_ok(!strstr(outbuffer, "Kd = 6 / 7"), "Output was: `%s`",
		   outbuffer);
}

ZTEST_USER(ocpc, test_consolecmd_ocpcpid__bad_param)
{
	zassert_equal(EC_ERROR_PARAM1,
		      shell_execute_cmd(get_ec_shell(), "ocpcpid y 0 0"));
}

ZTEST_USER(ocpc, test_consolecmd_ocpcdrvlmt)
{
	const char *outbuffer;
	size_t buffer_size;

	/* Set to 100mV */
	zassert_ok(shell_execute_cmd(get_ec_shell(), "ocpcdrvlmt 100"));

	/* Read back and verify */
	shell_backend_dummy_clear_output(get_ec_shell());
	zassert_ok(shell_execute_cmd(get_ec_shell(), "ocpcdrvlmt"));
	outbuffer =
		shell_backend_dummy_get_output(get_ec_shell(), &buffer_size);

	zassert_true(buffer_size > 0);
	zassert_ok(!strstr(outbuffer, "Drive Limit = 100"), "Output was: `%s`",
		   outbuffer);
}

ZTEST_USER(ocpc, test_consolecmd_ocpcdebug)
{
	zassert_ok(shell_execute_cmd(get_ec_shell(), "ocpcdebug ena"));
	zassert_true(test_ocpc_get_debug_output());
	zassert_false(test_ocpc_get_viz_output());

	zassert_ok(shell_execute_cmd(get_ec_shell(), "ocpcdebug dis"));
	zassert_false(test_ocpc_get_debug_output());
	zassert_false(test_ocpc_get_viz_output());

	zassert_ok(shell_execute_cmd(get_ec_shell(), "ocpcdebug viz"));
	zassert_false(test_ocpc_get_debug_output());
	zassert_true(test_ocpc_get_viz_output());

	zassert_ok(shell_execute_cmd(get_ec_shell(), "ocpcdebug all"));
	zassert_true(test_ocpc_get_debug_output());
	zassert_true(test_ocpc_get_viz_output());

	/* Bad param */
	zassert_equal(EC_ERROR_PARAM1,
		      shell_execute_cmd(get_ec_shell(), "ocpcdebug foo"));

	/* Missing param */
	zassert_equal(EC_ERROR_PARAM_COUNT,
		      shell_execute_cmd(get_ec_shell(), "ocpcdebug"));
}

ZTEST(ocpc, test_ocpc_config_secondary_charger__with_primary_charger)
{
	/* Should immediately return if a non-secondary charger is active,
	 * which is the default.
	 */

	charge_set_active_chg_chip(CHARGER_PRIMARY);

	zassert_equal(EC_ERROR_INVAL,
		      ocpc_config_secondary_charger(NULL, NULL, 0, 0));
}

ZTEST(ocpc, test_ocpc_config_secondary_charger__zero_desired_batt_curr)
{
	int expected_vsys_voltage = battery_get_info()->voltage_min;
	int desired_vsys_voltage = expected_vsys_voltage - 1;
	struct ocpc_data test_ocpc = {
		.last_vsys = expected_vsys_voltage - 10,
	};

	charge_set_active_chg_chip(CHARGER_SECONDARY);

	zassert_equal(EC_SUCCESS,
		      ocpc_config_secondary_charger(NULL, &test_ocpc,
						    desired_vsys_voltage, 0));

	/* Vsys should have been clamped to voltage_min */
	zassert_equal(test_ocpc.last_vsys, expected_vsys_voltage);
}

FAKE_VALUE_FUNC(int, battery_is_charge_fet_disabled);

ZTEST(ocpc, test_ocpc_config_secondary_charger__fet_disabled)
{
	charge_set_active_chg_chip(CHARGER_SECONDARY);

	/* A disabled FET should cause the function to abort */
	battery_is_charge_fet_disabled_fake.return_val = true;

	/* Use an arbitrary non-zero desired_batt_current_ma */
	zassert_equal(EC_ERROR_INVALID_CONFIG,
		      ocpc_config_secondary_charger(NULL, NULL, 0, 1000));

	/* Try again and we should hit the rate limiter */
	battery_is_charge_fet_disabled_fake.return_val = false;

	zassert_equal(EC_ERROR_BUSY,
		      ocpc_config_secondary_charger(NULL, NULL, 0, 1000));

	/* Allow the block to expire */
	k_sleep(K_SECONDS(6));
}

FAKE_VALUE_FUNC(enum ec_error_list, charger_set_vsys_compensation, int,
		struct ocpc_data *, int, int);

ZTEST(ocpc, test_ocpc_config_secondary_charger__happy)
{
	int desired_batt_voltage_mv = 123;
	int desired_batt_current_ma = 456;

	charge_set_active_chg_chip(CHARGER_SECONDARY);

	charger_set_vsys_compensation_fake.return_val = EC_SUCCESS;

	/* charger_set_vsys_compensation() will succeed and we will be
	 * done. Again use an arbitrary non-zero desired_current.
	 */
	zassert_equal(EC_SUCCESS, ocpc_config_secondary_charger(
					  NULL, NULL, desired_batt_voltage_mv,
					  desired_batt_current_ma));

	zassert_equal(1, charger_set_vsys_compensation_fake.call_count);
	zassert_equal(desired_batt_current_ma,
		      charger_set_vsys_compensation_fake.arg2_history[0]);
	zassert_equal(desired_batt_voltage_mv,
		      charger_set_vsys_compensation_fake.arg3_history[0]);
}

ZTEST(ocpc, test_ocpc_config_secondary_charger__unknown_return_code)
{
	charge_set_active_chg_chip(CHARGER_SECONDARY);

	charger_set_vsys_compensation_fake.return_val = 999;

	/* charger_set_vsys_compensation() will return an unhandled return
	 * value.
	 */
	zassert_equal(999, ocpc_config_secondary_charger(NULL, NULL, 123, 456));
}

ZTEST(ocpc, test_ocpc_config_secondary_charger__unimpl)
{
	int desired_charger_input_current;
	int desired_batt_voltage_mv = 10000;
	int desired_batt_current_ma = 1000;
	struct ocpc_data test_ocpc = {
		/* First run through loop */
		.last_vsys = OCPC_UNINIT,
	};

	charge_set_active_chg_chip(CHARGER_SECONDARY);

	/* Need to manually adjust Vsys */
	charger_set_vsys_compensation_fake.return_val = EC_ERROR_UNIMPLEMENTED;

	/* charger_set_vsys_compensation() will succeed and we will be
	 * done. Again use an arbitrary non-zero desired_current.
	 */
	zassert_equal(EC_SUCCESS, ocpc_config_secondary_charger(
					  &desired_charger_input_current,
					  &test_ocpc, desired_batt_voltage_mv,
					  desired_batt_current_ma));
}

ZTEST(ocpc, test_ocpc_config_secondary_charger__second_loop)
{
	int desired_charger_input_current = 2;
	int desired_batt_voltage_mv = 10000;
	int desired_batt_current_ma = 1000;
	int initial_integral = 123;
	struct ocpc_data test_ocpc = {
		/* Non-first run through loop */
		.last_vsys = 0,
		.integral = initial_integral,
	};

	/* Proportional controller only */
	test_ki = 0;
	test_ki_div = 1;
	test_kd = 0;
	test_kd_div = 1;
	ocpc_set_pid_constants();

	charge_set_active_chg_chip(CHARGER_SECONDARY);

	/* Need to manually adjust Vsys */
	charger_set_vsys_compensation_fake.return_val = EC_ERROR_UNIMPLEMENTED;

	/* charger_set_vsys_compensation() will succeed and we will be
	 * done. Again use an arbitrary non-zero desired_current.
	 */
	zassert_equal(EC_SUCCESS, ocpc_config_secondary_charger(
					  &desired_charger_input_current,
					  &test_ocpc, desired_batt_voltage_mv,
					  desired_batt_current_ma));

	/* Make sure the integral got updated */
	int expected_last_error =
		desired_charger_input_current - test_ocpc.secondary_ibus_ma;

	zassert_equal(expected_last_error, test_ocpc.last_error,
		      "Actual: %d, expected: %d", test_ocpc.last_error,
		      expected_last_error);
	zassert_equal(expected_last_error + initial_integral,
		      test_ocpc.integral, "Actual: %d, expected: %d",
		      test_ocpc.integral,
		      expected_last_error + initial_integral);
}

ZTEST(ocpc, test_ocpc_calc_resistances__not_charging)
{
	struct ocpc_data test_ocpc;
	struct batt_params test_batt_params;

	/* There are multiple conditions to exercise that qualify as charging */

	/* Battery current below 1666 */
	test_ocpc = (struct ocpc_data){ 0 };
	test_batt_params = (struct batt_params){
		.current = 0,
	};
	zassert_equal(EC_ERROR_INVALID_CONFIG,
		      ocpc_calc_resistances(&test_ocpc, &test_batt_params));

	/* Isys <= 0 */
	test_ocpc = (struct ocpc_data){ 0 };
	test_batt_params = (struct batt_params){
		.current = 1667,
	};
	zassert_equal(EC_ERROR_INVALID_CONFIG,
		      ocpc_calc_resistances(&test_ocpc, &test_batt_params));
}

ZTEST(ocpc, test_ocpc_calc_resistances__separate)
{
	struct ocpc_data test_ocpc;
	struct batt_params test_batt_params;

	/* Make Rsys = 1, Rbatt = 2 */
	test_ocpc = (struct ocpc_data){
		.vsys_aux_mv = 2005,
		.vsys_mv = 2000,
		.isys_ma = 1000,
	};
	test_batt_params = (struct batt_params){
		.current = 2000,
		.voltage = 1950,
	};

	/* Run enough times to become seeded. */
	for (int i = 0; i < 17; i++) {
		zassert_equal(EC_SUCCESS,
			      ocpc_calc_resistances(&test_ocpc,
						    &test_batt_params));
	}

	int expected_rbatt = (test_ocpc.vsys_mv - test_batt_params.voltage) *
			     1000 / test_batt_params.current;

	zassert_equal(expected_rbatt, test_ocpc.rbatt_mo,
		      "Actual: %d, expected: %d", test_ocpc.rbatt_mo,
		      expected_rbatt);

	int expected_rsys = (test_ocpc.vsys_aux_mv - test_ocpc.vsys_mv) * 1000 /
			    test_ocpc.isys_ma;

	zassert_equal(expected_rsys, test_ocpc.rsys_mo,
		      "Actual: %d, expected: %d", test_ocpc.rsys_mo,
		      expected_rsys);
}

static void reset(void *fixture)
{
	ARG_UNUSED(fixture);

	charge_set_active_chg_chip(CHARGER_PRIMARY);
	trigger_ocpc_reset();

	/* Reset fakes */
	RESET_FAKE(ocpc_get_pid_constants);
	RESET_FAKE(battery_is_charge_fet_disabled);
	RESET_FAKE(charger_set_vsys_compensation);

	/* Load values that match ocpc.c's defaults */
	test_kp = 1;
	test_kp_div = 4;
	test_ki = 1;
	test_ki_div = 15;
	test_kd = 1;
	test_kd_div = 10;

	ocpc_get_pid_constants_fake.custom_fake = get_pid_constants_custom_fake;

	/* Force an update which will use the above parameters. */
	ocpc_set_pid_constants();

	/* Reset the resistance calculation state */
	ocpc_calc_resistances(NULL, NULL);
	test_ocpc_reset_resistance_state();
}

ZTEST_SUITE(ocpc, NULL, NULL, reset, reset, NULL);
