/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "compile_time_macros.h"
#include "ec_commands.h"
#include "ec_tasks.h"
#include "emul/tcpc/emul_ps8xxx.h"
#include "emul/tcpc/emul_tcpci.h"
#include "emul/tcpc/emul_tcpci_partner_common.h"
#include "emul/tcpc/emul_tcpci_partner_drp.h"
#include "emul/tcpc/emul_tcpci_partner_snk.h"
#include "host_command.h"
#include "tcpm/tcpci.h"
#include "test/drivers/stubs.h"
#include "test/drivers/test_state.h"
#include "test/drivers/utils.h"
#include "usb_tc_sm.h"

#include <stdint.h>

#include <zephyr/fff.h>
#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

#define TEST_PORT 0

FAKE_VALUE_FUNC(int, dpm_get_source_pdo, const uint32_t **, const int);

struct usbc_fake_pdos_fixture {
	struct tcpci_partner_data source_5v_3a;
	struct tcpci_src_emul_data src_ext;
	const struct emul *tcpci_emul;
	const struct emul *charger_emul;
};

static void *usbc_fake_pdos_setup(void)
{
	static struct usbc_fake_pdos_fixture test_fixture;

	/* Get references for the emulators */
	test_fixture.tcpci_emul = EMUL_GET_USBC_BINDING(0, tcpc);
	test_fixture.charger_emul = EMUL_GET_USBC_BINDING(0, chg);

	/* Initialized the charger to supply 5V and 3A */
	tcpci_partner_init(&test_fixture.source_5v_3a, PD_REV30);
	test_fixture.source_5v_3a.extensions = tcpci_src_emul_init(
		&test_fixture.src_ext, &test_fixture.source_5v_3a, NULL);
	test_fixture.src_ext.pdo[1] =
		PDO_FIXED(5000, 3000, PDO_FIXED_UNCONSTRAINED);

	return &test_fixture;
}

static void usbc_fake_pdos_before(void *data)
{
	struct usbc_fake_pdos_fixture *fixture = data;

	RESET_FAKE(dpm_get_source_pdo);

	/* Set chipset to ON, this will set TCPM to DRP */
	test_set_chipset_to_s0();

	/* TODO(b/214401892): Check why need to give time TCPM to spin */
	k_sleep(K_SECONDS(1));

	connect_source_to_port(&fixture->source_5v_3a, &fixture->src_ext, 1,
			       fixture->tcpci_emul, fixture->charger_emul);

	/* Clear Alert and Status receive checks; clear message log */
	tcpci_src_emul_clear_alert_received(&fixture->src_ext);
	tcpci_src_emul_clear_status_received(&fixture->src_ext);
	zassert_false(fixture->src_ext.alert_received);
	zassert_false(fixture->src_ext.status_received);
	tcpci_partner_common_clear_logged_msgs(&fixture->source_5v_3a);

	/* Initial check on power state */
	zassert_true(chipset_in_state(CHIPSET_STATE_ON));
}

static void usbc_fake_pdos_after(void *data)
{
	struct usbc_fake_pdos_fixture *fixture = data;

	disconnect_source_from_port(fixture->tcpci_emul, fixture->charger_emul);
}

ZTEST_SUITE(usbc_fake_pdos, drivers_predicate_post_main, usbc_fake_pdos_setup,
	    usbc_fake_pdos_before, usbc_fake_pdos_after, NULL);

ZTEST_F(usbc_fake_pdos, test_give_source_info_0_pdos)
{
	const union sido expected_sido = {
		.port_type = 0,
		.port_maximum_pdp = CONFIG_USB_PD_3A_PORTS > 0 ? 15 : 7,
		.port_present_pdp = CONFIG_USB_PD_3A_PORTS > 0 ? 15 : 7,
		.port_reported_pdp = 0,
	};

	dpm_get_source_pdo_fake.return_val = 0;

	tcpci_partner_send_control_msg(&fixture->source_5v_3a,
				       PD_CTRL_GET_SOURCE_INFO, 0);
	k_sleep(K_SECONDS(2));

	const union sido *actual_sido = &fixture->source_5v_3a.tcpm_sido;
	zexpect_equal(actual_sido->port_reported_pdp,
		      expected_sido.port_reported_pdp,
		      "Unexpected reported PDP %u",
		      actual_sido->port_reported_pdp);
}
