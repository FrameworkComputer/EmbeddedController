/* Copyright 2022 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <shell/shell.h>
#include <ztest.h>

#include "charge_state_v2.h"
#include "console.h"
#include "ec_commands.h"
#include "test_state.h"
#include "utils.h"

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

struct console_cmd_charge_state_fixture {
	struct tcpci_src_emul source_5v_3a;
	const struct emul *tcpci_emul;
	const struct emul *charger_emul;
};

static void *console_cmd_charge_state_setup(void)
{
	static struct console_cmd_charge_state_fixture fixture;

	/* Get references for the emulators */
	fixture.tcpci_emul =
		emul_get_binding(DT_LABEL(DT_NODELABEL(tcpci_emul)));
	fixture.charger_emul =
		emul_get_binding(DT_LABEL(DT_NODELABEL(isl923x_emul)));

	/* Initialized the source to supply 5V and 3A */
	tcpci_src_emul_init(&fixture.source_5v_3a);
	fixture.source_5v_3a.data.pdo[1] =
		PDO_FIXED(5000, 3000, PDO_FIXED_UNCONSTRAINED);

	return &fixture;
}

static void console_cmd_charge_state_after(void *data)
{
	struct console_cmd_charge_state_fixture *fixture = data;

	disconnect_source_from_port(fixture->tcpci_emul, fixture->charger_emul);
}

ZTEST_SUITE(console_cmd_charge_state, drivers_predicate_post_main,
	    console_cmd_charge_state_setup, NULL,
	    console_cmd_charge_state_after, NULL);

ZTEST_USER_F(console_cmd_charge_state, test_idle_on_from_normal)
{
	/* Connect a source so we start charging */
	connect_source_to_port(&this->source_5v_3a, 1, this->tcpci_emul,
			       this->charger_emul);

	/* Verify that we're in "normal" mode */
	zassume_equal(get_chg_ctrl_mode(), CHARGE_CONTROL_NORMAL, NULL);

	/* Move to idle */
	zassert_ok(shell_execute_cmd(get_ec_shell(), "chgstate idle on"), NULL);
	zassert_equal(get_chg_ctrl_mode(), CHARGE_CONTROL_IDLE, NULL);
}

ZTEST_USER_F(console_cmd_charge_state, test_normal_from_idle)
{
	/* Connect a source so we start charging */
	connect_source_to_port(&this->source_5v_3a, 1, this->tcpci_emul,
			       this->charger_emul);

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
	connect_source_to_port(&this->source_5v_3a, 1, this->tcpci_emul,
			       this->charger_emul);

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
	connect_source_to_port(&this->source_5v_3a, 1, this->tcpci_emul,
			       this->charger_emul);

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
