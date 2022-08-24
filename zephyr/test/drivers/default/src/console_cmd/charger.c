/* Copyright 2022 The ChromiumOS Authors.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <zephyr/shell/shell.h>
#include <zephyr/ztest.h>

#include "console.h"
#include "dptf.h"
#include "ec_commands.h"
#include "test/drivers/test_state.h"
#include "test/drivers/utils.h"

/* Tests which need no fixture */
ZTEST_USER(console_cmd_charger, test_default_dump)
{
	zassert_ok(shell_execute_cmd(get_ec_shell(), "charger"),
		   "Failed default print");
}

ZTEST_USER(console_cmd_charger, test_good_index)
{
	zassert_ok(shell_execute_cmd(get_ec_shell(), "charger 0"),
		   "Failed index 0 print");
}

/* Bad parameter tests */
ZTEST_USER(console_cmd_charger, test_bad_index)
{
	int rv = shell_execute_cmd(get_ec_shell(), "charger 55");

	zassert_equal(rv, EC_ERROR_PARAM1, "Expected %d, but got %d",
		      EC_ERROR_PARAM1, rv);
}

ZTEST_USER(console_cmd_charger, test_bad_command)
{
	int rv = shell_execute_cmd(get_ec_shell(), "charger fish");

	zassert_equal(rv, EC_ERROR_PARAM1, "Expected %d, but got %d",
		      EC_ERROR_PARAM1, rv);
}

ZTEST_USER(console_cmd_charger, test_bad_input_current)
{
	int rv = shell_execute_cmd(get_ec_shell(), "charger input fish");

	zassert_equal(rv, EC_ERROR_PARAM2, "Expected %d, but got %d",
		      EC_ERROR_PARAM1, rv);
}

ZTEST_USER(console_cmd_charger, test_bad_current)
{
	int rv = shell_execute_cmd(get_ec_shell(), "charger current fish");

	zassert_equal(rv, EC_ERROR_PARAM2, "Expected %d, but got %d",
		      EC_ERROR_PARAM1, rv);
}

ZTEST_USER(console_cmd_charger, test_bad_voltage)
{
	int rv = shell_execute_cmd(get_ec_shell(), "charger voltage fish");

	zassert_equal(rv, EC_ERROR_PARAM2, "Expected %d, but got %d",
		      EC_ERROR_PARAM1, rv);
}

ZTEST_USER(console_cmd_charger, test_bad_dptf_current)
{
	int rv = shell_execute_cmd(get_ec_shell(), "charger dptf fish");

	zassert_equal(rv, EC_ERROR_PARAM2, "Expected %d, but got %d",
		      EC_ERROR_PARAM1, rv);
}

/* Good parameter sub-command tests */
ZTEST_USER(console_cmd_charger, test_good_input_current)
{
	int input_current;

	zassert_ok(shell_execute_cmd(get_ec_shell(), "charger input 1000"),
		   "Failed to set input current");
	zassume_ok(charger_get_input_current_limit(0, &input_current),
		   "Failed to get input current");
	zassert_equal(input_current, 1000,
		      "Input current not set in charger: %d", input_current);
}

ZTEST_USER(console_cmd_charger, test_good_dptf)
{
	zassert_ok(shell_execute_cmd(get_ec_shell(), "charger dptf 1000"),
		   "Failed to set dptf current");
	zassert_equal(dptf_get_charging_current_limit(), 1000,
		      "Unexpected dptf current");
}

ZTEST_USER(console_cmd_charger, test_unsupported_dump)
{
	/* Must define CONFIG_CMD_CHARGER_DUMP for this sub-command */
	int rv = shell_execute_cmd(get_ec_shell(), "charger dump");

	zassert_equal(rv, EC_ERROR_PARAM1, "Expected %d, but got %d",
		      EC_ERROR_PARAM1, rv);
}

/* Fixture needed to supply AC for manual current/voltage set */
struct console_cmd_charger_fixture {
	struct tcpci_partner_data source_5v_3a;
	struct tcpci_src_emul_data source_ext;
	const struct emul *tcpci_emul;
	const struct emul *charger_emul;
};

static void *console_cmd_charger_setup(void)
{
	static struct console_cmd_charger_fixture fixture;

	/* Assume we have one charger at index 0 */
	zassume_true(board_get_charger_chip_count() > 0,
		     "Insufficient chargers found");

	/* Get references for the emulators */
	fixture.tcpci_emul =
		emul_get_binding(DT_LABEL(DT_NODELABEL(tcpci_emul)));
	fixture.charger_emul = EMUL_DT_GET(DT_NODELABEL(isl923x_emul));

	/* Initialized the source to supply 5V and 3A */
	tcpci_partner_init(&fixture.source_5v_3a, PD_REV20);
	fixture.source_5v_3a.extensions = tcpci_src_emul_init(
		&fixture.source_ext, &fixture.source_5v_3a, NULL);
	fixture.source_ext.pdo[1] =
		PDO_FIXED(5000, 3000, PDO_FIXED_UNCONSTRAINED);

	return &fixture;
}

static void console_cmd_charger_after(void *data)
{
	struct console_cmd_charger_fixture *fixture = data;

	/* Disconnect the source, and ensure we reset charge params */
	disconnect_source_from_port(fixture->tcpci_emul, fixture->charger_emul);
	host_cmd_charge_control(CHARGE_CONTROL_NORMAL,
				EC_CHARGE_CONTROL_CMD_SET);
}

/* Tests that need the fixture */
ZTEST_USER_F(console_cmd_charger, test_good_current)
{
	int current;

	/* Connect a source so we start charging */
	connect_source_to_port(&fixture->source_5v_3a, &fixture->source_ext, 1,
			       fixture->tcpci_emul, fixture->charger_emul);
	zassert_ok(shell_execute_cmd(get_ec_shell(), "charger current 1000"),
		   "Failed to set current");

	/* Give the charger task time to pick up the manual current */
	k_sleep(K_SECONDS(1));

	zassume_ok(charger_get_current(0, &current), "Failed to get current");
	zassert_equal(current, 1000, "Current not set in charger: %d", current);
}

ZTEST_USER_F(console_cmd_charger, test_good_voltage)
{
	int voltage;

	/* Connect a source so we start charging */
	connect_source_to_port(&fixture->source_5v_3a, &fixture->source_ext, 1,
			       fixture->tcpci_emul, fixture->charger_emul);
	/* Note: select a fake voltage larger than the charger's minimum */
	zassert_ok(shell_execute_cmd(get_ec_shell(), "charger voltage 3000"),
		   "Failed to set voltage");

	/* Give the charger task time to pick up the manual voltage */
	k_sleep(K_SECONDS(1));

	zassume_ok(charger_get_voltage(0, &voltage), "Failed to get voltage");
	zassert_equal(voltage, 3000, "Voltage not set in charger: %d", voltage);
}

ZTEST_SUITE(console_cmd_charger, drivers_predicate_post_main,
	    console_cmd_charger_setup, NULL, console_cmd_charger_after, NULL);
