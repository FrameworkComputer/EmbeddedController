/* Copyright 2022 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <shell/shell.h>
#include <ztest.h>

#include "charge_manager.h"
#include "console.h"
#include "emul/emul_isl923x.h"
#include "emul/tcpc/emul_tcpci_partner_snk.h"
#include "tcpm/tcpci.h"
#include "test_state.h"
#include "utils.h"

static void connect_sink_to_port(const struct emul *charger_emul,
				 const struct emul *tcpci_emul,
				 struct tcpci_snk_emul *sink)
{
	isl923x_emul_set_adc_vbus(charger_emul, 0);
	tcpci_emul_set_reg(tcpci_emul, TCPC_REG_POWER_STATUS,
			   TCPC_REG_POWER_STATUS_VBUS_DET);
	tcpci_emul_set_reg(tcpci_emul, TCPC_REG_EXT_STATUS,
			   TCPC_REG_EXT_STATUS_SAFE0V);
	tcpci_tcpc_alert(0);
	zassume_ok(tcpci_snk_emul_connect_to_tcpci(&sink->data,
						   &sink->common_data,
						   &sink->ops, tcpci_emul),
		   NULL);

	/* Wait for PD negotiation and current ramp.
	 * TODO(b/213906889): Check message timing and contents.
	 */
	k_sleep(K_SECONDS(10));
}

static inline void disconnect_sink_from_port(const struct emul *tcpci_emul)
{
	zassume_ok(tcpci_emul_disconnect_partner(tcpci_emul), NULL);
	k_sleep(K_SECONDS(1));
}

struct console_cmd_charge_manager_fixture {
	struct tcpci_snk_emul sink_5v_3a;
	const struct emul *tcpci_emul;
	const struct emul *charger_emul;
};

static void *console_cmd_charge_manager_setup(void)
{
	static struct console_cmd_charge_manager_fixture test_fixture;

	/* Get references for the emulators */
	test_fixture.tcpci_emul =
		emul_get_binding(DT_LABEL(DT_NODELABEL(tcpci_emul)));
	test_fixture.charger_emul =
		emul_get_binding(DT_LABEL(DT_NODELABEL(isl923x_emul)));
	tcpci_emul_set_rev(test_fixture.tcpci_emul, TCPCI_EMUL_REV2_0_VER1_1);

	/* Initialized the sink to request 5V and 3A */
	tcpci_snk_emul_init(&test_fixture.sink_5v_3a);
	test_fixture.sink_5v_3a.data.pdo[1] =
		PDO_FIXED(5000, 3000, PDO_FIXED_UNCONSTRAINED);

	return &test_fixture;
}

static void console_cmd_charge_manager_after(void *state)
{
	struct console_cmd_charge_manager_fixture *fixture = state;

	shell_execute_cmd(get_ec_shell(), "chgoverride -1");
	disconnect_sink_from_port(fixture->tcpci_emul);
}

ZTEST_SUITE(console_cmd_charge_manager, drivers_predicate_post_main,
	    console_cmd_charge_manager_setup, NULL,
	    console_cmd_charge_manager_after, NULL);

/**
 * Test the chgsup (charge supplier info) command. This command only prints to
 * console some information which is not yet possible to verify. So just check
 * that the console command ran successfully.
 */
ZTEST_USER(console_cmd_charge_manager, test_chgsup)
{
	zassert_ok(shell_execute_cmd(get_ec_shell(), "chgsup"), NULL);
}

/**
 * Test chgoverride command with no arguments. This should just print the
 * current override port.
 */
ZTEST_USER(console_cmd_charge_manager, test_chgoverride_missing_port)
{
	zassert_ok(shell_execute_cmd(get_ec_shell(), "chgoverride"), NULL);
}

ZTEST_USER(console_cmd_charge_manager, test_chgoverride_off_from_off)
{
	zassert_ok(shell_execute_cmd(get_ec_shell(), "chgoverride -1"), NULL);
	zassert_equal(charge_manager_get_override(), OVERRIDE_OFF, NULL);
}

ZTEST_USER(console_cmd_charge_manager, test_chgoverride_disable_from_off)
{
	zassert_ok(shell_execute_cmd(get_ec_shell(), "chgoverride -2"), NULL);
	zassert_equal(charge_manager_get_override(), OVERRIDE_DONT_CHARGE,
		      NULL);
}

ZTEST_USER(console_cmd_charge_manager, test_chgoverride_0_from_off)
{
	zassert_ok(shell_execute_cmd(get_ec_shell(), "chgoverride 0"), NULL);
	zassert_equal(charge_manager_get_override(), 0, NULL);
}

ZTEST_USER_F(console_cmd_charge_manager, test_chgoverride_0_from_sink)
{
	/* Set chipset to ON, this will set TCPM to DRP */
	test_set_chipset_to_s0();

	/* TODO(b/214401892): Check why need to give time TCPM to spin */
	k_sleep(K_SECONDS(1));

	connect_sink_to_port(this->charger_emul, this->tcpci_emul,
			     &this->sink_5v_3a);
	zassert_equal(shell_execute_cmd(get_ec_shell(), "chgoverride 0"),
		      EC_ERROR_INVAL, NULL);
}
