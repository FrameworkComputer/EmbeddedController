/* Copyright 2022 The ChromiumOS Authors.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <zephyr/shell/shell.h>
#include <zephyr/ztest.h>

#include "console.h"
#include "ec_commands.h"
#include "emul/emul_isl923x.h"
#include "emul/tcpc/emul_ps8xxx.h"
#include "emul/tcpc/emul_tcpci.h"
#include "emul/tcpc/emul_tcpci_partner_snk.h"
#include "test/drivers/stubs.h"
#include "test/drivers/test_state.h"
#include "test/drivers/utils.h"

#define TEST_PORT USBC_PORT_C0
#define BAD_PORT 42

struct host_cmd_usb_pd_control_fixture {
	const struct emul *tcpci_emul;
	const struct emul *charger_emul;
	struct tcpci_partner_data partner;
	struct tcpci_snk_emul_data snk_ext;
};

static enum ec_status
run_usb_pd_control(int port, struct ec_response_usb_pd_control_v2 *resp)
{
	/*
	 * Note: while arguments exist to change the PD state, their use is
	 * discouraged as that causes the response to have non-deterministic
	 * results.  The kernel only uses the "no change" parameters, so that is
	 * what we shall test here.
	 */
	struct ec_params_usb_pd_control params = {
		.port = port,
		.role = USB_PD_CTRL_ROLE_NO_CHANGE,
		.mux = USB_PD_CTRL_MUX_NO_CHANGE,
		.swap = USB_PD_CTRL_SWAP_NONE
	};
	struct host_cmd_handler_args args =
		BUILD_HOST_COMMAND(EC_CMD_USB_PD_CONTROL, 2, *resp, params);

	return host_command_process(&args);
}

ZTEST_USER(host_cmd_usb_pd_control, test_good_index_no_partner)
{
	struct ec_response_usb_pd_control_v2 response;

	zassert_ok(run_usb_pd_control(TEST_PORT, &response),
		   "Failed to process usb_pd_control for port %d", TEST_PORT);

	/* Verify basic not-connected expectations */
	zassert_equal(response.enabled, 0,
		      "Failed to find nothing enabled: 0x%02x",
		      response.enabled);
	/* Don't verify role, cc, or polarity as it isn't meaningful */
	zassert_equal(response.control_flags, 0, "Failed to see flags cleared");
}

ZTEST_USER_F(host_cmd_usb_pd_control, test_good_index_sink_partner)
{
	struct ec_response_usb_pd_control_v2 response;

	/* Attach simple sink that shouldn't do any swaps */
	connect_sink_to_port(&fixture->partner, fixture->tcpci_emul,
			     fixture->charger_emul);

	/* Wait for connection to settle */
	k_sleep(K_SECONDS(1));

	zassert_ok(run_usb_pd_control(TEST_PORT, &response),
		   "Failed to process usb_pd_control for port %d", TEST_PORT);

	/* Verify basic sink expectations */
	zassert_equal(
		response.enabled,
		(PD_CTRL_RESP_ENABLED_COMMS | PD_CTRL_RESP_ENABLED_CONNECTED |
		 PD_CTRL_RESP_ENABLED_PD_CAPABLE),
		"Failed to see full connection: 0x%02x", response.enabled);
	/*
	 * We should be source, DFP, Vconn source, and we set our sink caps
	 * to USB comms
	 */
	zassert_equal(response.role,
		      (PD_CTRL_RESP_ROLE_USB_COMM | PD_CTRL_RESP_ROLE_POWER |
		       PD_CTRL_RESP_ROLE_DATA | PD_CTRL_RESP_ROLE_VCONN),
		      "Failed to see expected role: 0x%02x", response.role);
	zassert_equal(response.cc_state, PD_CC_UFP_ATTACHED,
		      "Failed to see UFP attached");
	zassert_equal(response.control_flags, 0, "Failed to see flags cleared");
}

ZTEST_USER(host_cmd_usb_pd_control, test_bad_index)
{
	struct ec_response_usb_pd_control_v2 response;

	zassume_true(board_get_usb_pd_port_count() < BAD_PORT,
		     "Intended bad port exists");
	zassert_equal(run_usb_pd_control(BAD_PORT, &response),
		      EC_RES_INVALID_PARAM,
		      "Failed to fail usb_pd_control for port %d", BAD_PORT);
}

static void *host_cmd_usb_pd_control_setup(void)
{
	static struct host_cmd_usb_pd_control_fixture fixture;
	struct tcpci_partner_data *partner = &fixture.partner;
	struct tcpci_snk_emul_data *snk_ext = &fixture.snk_ext;

	tcpci_partner_init(partner, PD_REV30);
	partner->extensions = tcpci_snk_emul_init(snk_ext, partner, NULL);

	/* Get references for the emulators */
	fixture.tcpci_emul = EMUL_DT_GET(DT_NODELABEL(tcpci_emul));
	fixture.charger_emul = EMUL_DT_GET(DT_NODELABEL(isl923x_emul));

	/* Sink 5V 3A. */
	snk_ext->pdo[0] = PDO_FIXED(5000, 3000, PDO_FIXED_COMM_CAP);

	return &fixture;
}

static void host_cmd_usb_pd_control_before(void *data)
{
	ARG_UNUSED(data);

	/* Assume we have at least one USB-C port */
	zassume_true(board_get_usb_pd_port_count() > 0,
		     "Insufficient TCPCs found");

	/* Set the system into S0, since the AP would drive these commands */
	test_set_chipset_to_s0();
	k_sleep(K_SECONDS(1));
}

static void host_cmd_usb_pd_control_after(void *data)
{
	struct host_cmd_usb_pd_control_fixture *fixture = data;

	disconnect_sink_from_port(fixture->tcpci_emul);
	k_sleep(K_SECONDS(1));
}

ZTEST_SUITE(host_cmd_usb_pd_control, drivers_predicate_post_main,
	    host_cmd_usb_pd_control_setup, host_cmd_usb_pd_control_before,
	    host_cmd_usb_pd_control_after, NULL);
