/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * This test suite verifies integration of upstream BC1.2 drivers operating
 * in host mode mode (port partner is a portable device) with the EC
 * application. This test suite is driver agnostic, and should not perform any
 * driver specific checks.
 */

#include "charge_manager.h"
#include "ec_commands.h"
#include "emul/tcpc/emul_tcpci_partner_faulty_ext.h"
#include "emul/tcpc/emul_tcpci_partner_snk.h"
#include "test/drivers/test_state.h"
#include "test/drivers/utils.h"
#include "timer.h"
#include "usb_charge.h"
#include "usbc/bc12_upstream.h"
#include "usbc/utils.h"

#include <zephyr/drivers/usb/emul_bc12.h>
#include <zephyr/drivers/usb/usb_bc12.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/ztest.h>

LOG_MODULE_REGISTER(test_drivers_bc12_upstream_host_mode, LOG_LEVEL_DBG);

#define CHARGE_DETECT_DELAY_MS (CHARGE_DETECT_DELAY / 1000)

struct bc12_upstream_host_mode_fixture {
	const struct device *bc12_dev;
	const struct emul *bc12_emul;
	unsigned int typec_port;

	struct tcpci_partner_data sink;
	struct tcpci_faulty_ext_data faulty_snk_ext;
	struct tcpci_snk_emul_data snk_ext;
	const struct emul *tcpci_emul;
	const struct emul *charger_emul;
	struct tcpci_faulty_ext_action actions;
};

ZTEST_F(bc12_upstream_host_mode, test_bc12_host_mode)
{
	struct ec_response_usb_pd_power_info response;

	/* Initial state should be disconnected */
	response = host_cmd_power_info(0);
	zassert_equal(response.role, USB_PD_PORT_POWER_DISCONNECTED,
		      "Expected power role %d, but PD reports role %d",
		      USB_PD_PORT_POWER_DISCONNECTED, response.role);

	zassert_equal(response.type, USB_CHG_TYPE_NONE,
		      "Expected charger type %d, but PD reports type %d",
		      USB_CHG_TYPE_NONE, response.type);

	/*
	 * Setup a PD sink that always fails to respond to SRC caps.
	 * This mimics a sink that doesn't support PD.
	 */
	fixture->actions.action_mask = TCPCI_FAULTY_EXT_FAIL_SRC_CAP;
	fixture->actions.count = TCPCI_FAULTY_EXT_INFINITE_ACTION;
	tcpci_faulty_ext_append_action(&fixture->faulty_snk_ext,
				       &fixture->actions);

	connect_sink_to_port(&fixture->sink, fixture->tcpci_emul,
			     fixture->charger_emul);

	/* Emulate connection of a port device partner */
	bc12_emul_set_pd_partner(fixture->bc12_emul, true);

	/* Don't query the power info until the charge detect delay expires. */
	k_sleep(K_MSEC(CHARGE_DETECT_DELAY_MS * 2));

	response = host_cmd_power_info(0);
	zassert_equal(response.role, USB_PD_PORT_POWER_SOURCE,
		      "Expected power role %d, but PD reports role %d",
		      USB_PD_PORT_POWER_SOURCE, response.role);

	LOG_INF("BC1.2 configured for SRC, current %d mA",
		response.meas.current_max);

	/* Emulate connection of a port device partner */
	bc12_emul_set_pd_partner(fixture->bc12_emul, false);
}

static void *bc12_host_mode_setup(void)
{
	static struct bc12_upstream_host_mode_fixture fixture = {
		.bc12_dev = DEVICE_GET_USBC_BINDING(0, bc12),
		.bc12_emul = EMUL_GET_USBC_BINDING(0, bc12),
		.typec_port = 0,
	};

	zassert_equal(bc12_ports[fixture.typec_port].drv, &bc12_upstream_drv);
	zassert_not_null(fixture.bc12_dev);
	zassert_not_null(fixture.bc12_emul);
	zassert_true(device_is_ready(fixture.bc12_dev));

	/* Get references for the emulators */
	fixture.tcpci_emul = EMUL_GET_USBC_BINDING(0, tcpc);
	fixture.charger_emul = EMUL_GET_USBC_BINDING(0, chg);

	tcpci_partner_init(&fixture.sink, PD_REV20);
	fixture.sink.extensions = tcpci_faulty_ext_init(
		&fixture.faulty_snk_ext, &fixture.sink,
		tcpci_snk_emul_init(&fixture.snk_ext, &fixture.sink, NULL));
	fixture.snk_ext.pdo[1] = PDO_FIXED(5000, 3000, PDO_FIXED_UNCONSTRAINED);

	return &fixture;
}

static void bc12_host_mode_before(void *f)
{
	struct bc12_upstream_host_mode_fixture *fixture = f;

	set_ac_enabled(true);
	/* Set chipset to ON, this will set TCPM to DRP */
	test_set_chipset_to_s0();

	/* Wait long enough for TCPMv2 to be idle. */
	k_sleep(K_MSEC(2000));

	usb_charger_task_set_event(fixture->typec_port, USB_CHG_EVENT_CC_OPEN);
	k_sleep(K_MSEC(1));
}

static void bc12_host_mode_after(void *f)
{
	struct bc12_upstream_host_mode_fixture *fixture = f;

	tcpci_faulty_ext_clear_actions_list(&fixture->faulty_snk_ext);
	disconnect_sink_from_port(fixture->tcpci_emul);
	tcpci_partner_common_clear_logged_msgs(&fixture->sink);

	bc12_emul_set_pd_partner(fixture->bc12_emul, false);
	set_ac_enabled(false);
}

ZTEST_SUITE(bc12_upstream_host_mode, drivers_predicate_post_main,
	    bc12_host_mode_setup, bc12_host_mode_before, bc12_host_mode_after,
	    NULL);
