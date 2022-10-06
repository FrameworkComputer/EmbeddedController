/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <zephyr/shell/shell.h>
#include <zephyr/ztest.h>

#include "charge_state.h"
#include "charge_state_v2.h"
#include "console.h"
#include "ec_commands.h"
#include "test/drivers/test_state.h"
#include "test/drivers/utils.h"

ZTEST_USER(console_cmd_charge_state, test_idle_too_few_args)
{
	int rv;

	rv = shell_execute_cmd(get_ec_shell(), "chgstate idle");
	zassert_equal(rv, EC_ERROR_PARAM_COUNT, "Expected %d, but got %d",
		      EC_ERROR_PARAM_COUNT, rv);
}

ZTEST_USER(console_cmd_charge_state, test_idle_arg_not_a_bool)
{
	int rv;

	/*
	 * There are many strings that will fail parse_bool(), just test one to
	 * test the code path in the command, other tests for parse_bool are
	 * done in the respective unit test.
	 */
	rv = shell_execute_cmd(get_ec_shell(), "chgstate idle g");
	zassert_equal(rv, EC_ERROR_PARAM2, "Expected %d, but got %d",
		      EC_ERROR_PARAM2, rv);
}

ZTEST_USER(console_cmd_charge_state, test_idle_on__no_ac)
{
	int rv;

	rv = shell_execute_cmd(get_ec_shell(), "chgstate idle on");
	zassert_equal(rv, EC_ERROR_NOT_POWERED, "Expected %d, but got %d",
		      EC_ERROR_NOT_POWERED, rv);
}

ZTEST_USER(console_cmd_charge_state, test_discharge_on__no_ac)
{
	int rv;

	rv = shell_execute_cmd(get_ec_shell(), "chgstate discharge on");
	zassert_equal(rv, EC_ERROR_NOT_POWERED, "Expected %d, but got %d",
		      EC_ERROR_NOT_POWERED, rv);
}

ZTEST_USER(console_cmd_charge_state, test_discharge_too_few_args)
{
	int rv;

	rv = shell_execute_cmd(get_ec_shell(), "chgstate discharge");
	zassert_equal(rv, EC_ERROR_PARAM_COUNT, "Expected %d, but got %d",
		      EC_ERROR_PARAM_COUNT, rv);
}

ZTEST_USER(console_cmd_charge_state, test_discharge_arg_not_a_bool)
{
	int rv;

	/*
	 * There are many strings that will fail parse_bool(), just test one to
	 * test the code path in the command, other tests for parse_bool are
	 * done in the respective unit test.
	 */
	rv = shell_execute_cmd(get_ec_shell(), "chgstate discharge g");
	zassert_equal(rv, EC_ERROR_PARAM2, "Expected %d, but got %d",
		      EC_ERROR_PARAM2, rv);
}

ZTEST_USER(console_cmd_charge_state, test_debug_too_few_args)
{
	int rv;

	rv = shell_execute_cmd(get_ec_shell(), "chgstate debug");
	zassert_equal(rv, EC_ERROR_PARAM_COUNT, "Expected %d, but got %d",
		      EC_ERROR_PARAM_COUNT, rv);
}

ZTEST_USER(console_cmd_charge_state, test_debug_arg_not_bool)
{
	int rv;

	/*
	 * There are many strings that will fail parse_bool(), just test one to
	 * test the code path in the command, other tests for parse_bool are
	 * done in the respective unit test.
	 */
	rv = shell_execute_cmd(get_ec_shell(), "chgstate debug g");
	zassert_equal(rv, EC_ERROR_PARAM2, "Expected %d, but got %d",
		      EC_ERROR_PARAM2, rv);
}

ZTEST_USER(console_cmd_charge_state, test_debug_on)
{
	zassert_ok(shell_execute_cmd(get_ec_shell(), "chgstate debug on"),
		   NULL);
}

ZTEST_USER(console_cmd_charge_state, test_debug_on_show_charging_progress)
{
	/*
	 * Force reset the previous display charge so the charge state task
	 * prints on the next iteration.
	 */
	reset_prev_disp_charge();
	charging_progress_displayed();

	/* Enable debug printing */
	zassume_ok(shell_execute_cmd(get_ec_shell(), "chgstate debug on"),
		   NULL);

	/* Sleep at least 1 full iteration of the charge state loop */
	k_sleep(K_USEC(CHARGE_MAX_SLEEP_USEC + 1));

	zassert_true(charging_progress_displayed(), NULL);
}

ZTEST_USER(console_cmd_charge_state, test_sustain_too_few_args__2_args)
{
	int rv;

	rv = shell_execute_cmd(get_ec_shell(), "chgstate sustain");
	zassert_equal(rv, EC_ERROR_PARAM_COUNT, "Expected %d, but got %d",
		      EC_ERROR_PARAM_COUNT, rv);
}

ZTEST_USER(console_cmd_charge_state, test_sustain_too_few_args__3_args)
{
	int rv;

	rv = shell_execute_cmd(get_ec_shell(), "chgstate sustain 5");
	zassert_equal(rv, EC_ERROR_PARAM_COUNT, "Expected %d, but got %d",
		      EC_ERROR_PARAM_COUNT, rv);
}

ZTEST_USER(console_cmd_charge_state, test_sustain_invalid_params)
{
	/* Verify that lower bound is less than upper bound */
	zassert_equal(shell_execute_cmd(get_ec_shell(),
					"chgstate sustain 50 30"),
		      EC_ERROR_INVAL, NULL);

	/* Verify that lower bound is at least 0 (when upper bound is given) */
	zassert_equal(shell_execute_cmd(get_ec_shell(),
					"chgstate sustain -5 30"),
		      EC_ERROR_INVAL, NULL);

	/* Verify that upper bound is at most 100 */
	zassert_equal(shell_execute_cmd(get_ec_shell(),
					"chgstate sustain 50 101"),
		      EC_ERROR_INVAL, NULL);
}

struct console_cmd_charge_state_fixture {
	struct tcpci_partner_data source_5v_3a;
	struct tcpci_src_emul_data source_ext;
	const struct emul *tcpci_emul;
	const struct emul *charger_emul;
};

static void *console_cmd_charge_state_setup(void)
{
	static struct console_cmd_charge_state_fixture fixture;

	/* Get references for the emulators */
	fixture.tcpci_emul = EMUL_GET_USBC_BINDING(0, tcpc);
	fixture.charger_emul = EMUL_GET_USBC_BINDING(0, chg);

	/* Initialized the source to supply 5V and 3A */
	tcpci_partner_init(&fixture.source_5v_3a, PD_REV20);
	fixture.source_5v_3a.extensions = tcpci_src_emul_init(
		&fixture.source_ext, &fixture.source_5v_3a, NULL);
	fixture.source_ext.pdo[1] =
		PDO_FIXED(5000, 3000, PDO_FIXED_UNCONSTRAINED);

	return &fixture;
}

static void console_cmd_charge_state_after(void *data)
{
	struct console_cmd_charge_state_fixture *fixture = data;

	disconnect_source_from_port(fixture->tcpci_emul, fixture->charger_emul);
	shell_execute_cmd(get_ec_shell(), "chgstate debug off");
	shell_execute_cmd(get_ec_shell(), "chgstate sustain -1 -1");
}

ZTEST_SUITE(console_cmd_charge_state, drivers_predicate_post_main,
	    console_cmd_charge_state_setup, NULL,
	    console_cmd_charge_state_after, NULL);

ZTEST_USER_F(console_cmd_charge_state, test_idle_on_from_normal)
{
	/* Connect a source so we start charging */
	connect_source_to_port(&fixture->source_5v_3a, &fixture->source_ext, 1,
			       fixture->tcpci_emul, fixture->charger_emul);

	/* Verify that we're in "normal" mode */
	zassume_equal(get_chg_ctrl_mode(), CHARGE_CONTROL_NORMAL, NULL);

	/* Move to idle */
	zassert_ok(shell_execute_cmd(get_ec_shell(), "chgstate idle on"), NULL);
	zassert_equal(get_chg_ctrl_mode(), CHARGE_CONTROL_IDLE, NULL);
}

ZTEST_USER_F(console_cmd_charge_state, test_normal_from_idle)
{
	/* Connect a source so we start charging */
	connect_source_to_port(&fixture->source_5v_3a, &fixture->source_ext, 1,
			       fixture->tcpci_emul, fixture->charger_emul);

	/* Verify that we're in "normal" mode */
	zassume_equal(get_chg_ctrl_mode(), CHARGE_CONTROL_NORMAL, NULL);

	/* Move to idle */
	zassume_ok(shell_execute_cmd(get_ec_shell(), "chgstate idle on"), NULL);
	zassume_equal(get_chg_ctrl_mode(), CHARGE_CONTROL_IDLE, NULL);

	/* Move back to normal */
	zassert_ok(shell_execute_cmd(get_ec_shell(), "chgstate idle off"),
		   NULL);
	zassert_equal(get_chg_ctrl_mode(), CHARGE_CONTROL_NORMAL, NULL);
}

ZTEST_USER_F(console_cmd_charge_state, test_discharge_on)
{
	/* Connect a source so we start charging */
	connect_source_to_port(&fixture->source_5v_3a, &fixture->source_ext, 1,
			       fixture->tcpci_emul, fixture->charger_emul);

	/* Verify that we're in "normal" mode */
	zassume_equal(get_chg_ctrl_mode(), CHARGE_CONTROL_NORMAL, NULL);

	/* Enable discharge */
	zassert_ok(shell_execute_cmd(get_ec_shell(), "chgstate discharge on"),
		   NULL);
	zassert_equal(get_chg_ctrl_mode(), CHARGE_CONTROL_DISCHARGE, NULL);
}

ZTEST_USER_F(console_cmd_charge_state, test_discharge_off)
{
	/* Connect a source so we start charging */
	connect_source_to_port(&fixture->source_5v_3a, &fixture->source_ext, 1,
			       fixture->tcpci_emul, fixture->charger_emul);

	/* Verify that we're in "normal" mode */
	zassume_equal(get_chg_ctrl_mode(), CHARGE_CONTROL_NORMAL, NULL);

	/* Enable discharge */
	zassume_ok(shell_execute_cmd(get_ec_shell(), "chgstate discharge on"),
		   NULL);
	zassume_equal(get_chg_ctrl_mode(), CHARGE_CONTROL_DISCHARGE, NULL);

	/* Disable discharge */
	zassert_ok(shell_execute_cmd(get_ec_shell(), "chgstate discharge off"),
		   NULL);
	zassert_equal(get_chg_ctrl_mode(), CHARGE_CONTROL_NORMAL, NULL);
}

ZTEST_USER(console_cmd_charge_state, test_sustain)
{
	struct ec_response_charge_control charge_control_values;

	/* Verify that lower bound is less than upper bound */
	zassert_ok(shell_execute_cmd(get_ec_shell(), "chgstate sustain 30 50"),
		   NULL);

	charge_control_values = host_cmd_charge_control(
		CHARGE_CONTROL_NORMAL, EC_CHARGE_CONTROL_CMD_GET);
	zassert_equal(charge_control_values.sustain_soc.lower, 30, NULL);
	zassert_equal(charge_control_values.sustain_soc.upper, 50, NULL);
}
