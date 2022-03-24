/* Copyright 2022 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <stdint.h>
#include <ztest.h>

#include "common.h"
#include "ec_tasks.h"
#include "emul/emul_isl923x.h"
#include "emul/tcpc/emul_tcpci_partner_drp.h"
#include "tcpm/tcpci.h"
#include "test/drivers/stubs.h"
#include "test/drivers/test_state.h"
#include "test/drivers/utils.h"
#include "usb_pd.h"

#define SNK_PORT USBC_PORT_C0

#define TCPCI_EMUL_LABEL DT_NODELABEL(tcpci_emul)

struct usb_pd_ctrl_msg_test_fixture {
	struct tcpci_drp_emul partner_emul;
	const struct emul *tcpci_emul;
	const struct emul *charger_emul;
};

static void connect_sink_to_port(struct usb_pd_ctrl_msg_test_fixture *fixture)
{
	/*
	 * TODO(b/221439302) Updating the TCPCI emulator registers, updating the
	 *   vbus, as well as alerting should all be a part of the connect
	 *   function.
	 */
	isl923x_emul_set_adc_vbus(fixture->charger_emul, 0);
	zassume_ok(tcpci_emul_set_reg(fixture->tcpci_emul,
				      TCPC_REG_POWER_STATUS,
				      TCPC_REG_POWER_STATUS_VBUS_DET),
		   NULL);
	zassume_ok(tcpci_emul_set_reg(fixture->tcpci_emul, TCPC_REG_EXT_STATUS,
				      TCPC_REG_EXT_STATUS_SAFE0V),
		   NULL);
	zassume_ok(tcpci_drp_emul_connect_to_tcpci(
			   &fixture->partner_emul.data,
			   &fixture->partner_emul.src_data,
			   &fixture->partner_emul.snk_data,
			   &fixture->partner_emul.common_data,
			   &fixture->partner_emul.ops, fixture->tcpci_emul),
		   NULL);

	tcpci_tcpc_alert(SNK_PORT);
	/* Wait for PD negotiation and current ramp.
	 * TODO(b/213906889): Check message timing and contents.
	 */
	k_sleep(K_SECONDS(10));
}

static void
disconnect_sink_from_port(struct usb_pd_ctrl_msg_test_fixture *fixture)
{
	zassume_ok(tcpci_emul_disconnect_partner(fixture->tcpci_emul), NULL);
	k_sleep(K_SECONDS(1));
}

static void *usb_pd_ctrl_msg_setup(void)
{
	static struct usb_pd_ctrl_msg_test_fixture fixture;

	/* Get references for the emulators */
	fixture.tcpci_emul =
		emul_get_binding(DT_LABEL(DT_NODELABEL(tcpci_emul)));
	fixture.charger_emul =
		emul_get_binding(DT_LABEL(DT_NODELABEL(isl923x_emul)));

	tcpci_emul_set_rev(fixture.tcpci_emul, TCPCI_EMUL_REV2_0_VER1_1);

	/* Turn TCPCI rev 2 ON */
	tcpc_config[SNK_PORT].flags |= TCPC_FLAGS_TCPCI_REV2_0;

	return &fixture;
}

static void usb_pd_ctrl_msg_before(void *data)
{
	struct usb_pd_ctrl_msg_test_fixture *fixture = data;

	set_test_runner_tid();

	test_set_chipset_to_g3();
	k_sleep(K_SECONDS(1));

	/* Set chipset to ON, this will set TCPM to DRP */
	test_set_chipset_to_s0();

	/* TODO(b/214401892): Check why need to give time TCPM to spin */
	k_sleep(K_SECONDS(1));

	/* Initialized the sink to request 5V and 3A */
	tcpci_drp_emul_init(&fixture->partner_emul);

	connect_sink_to_port(fixture);

	k_sleep(K_SECONDS(10));
}

static void usb_pd_ctrl_msg_after(void *data)
{
	struct usb_pd_ctrl_msg_test_fixture *fixture = data;

	disconnect_sink_from_port(fixture);
}

ZTEST_SUITE(usb_pd_ctrl_msg_test, drivers_predicate_post_main,
	    usb_pd_ctrl_msg_setup, NULL, usb_pd_ctrl_msg_after, NULL);

ZTEST_F(usb_pd_ctrl_msg_test, verify_vconn_swap)
{
	struct ec_response_typec_status snk_resp = { 0 };
	int rv = 0;

	/* TODO(b/228593065): Revert this once ZTEST fix before ordering
	 * is pulled in
	 */
	usb_pd_ctrl_msg_before(this);

	snk_resp = host_cmd_typec_status(SNK_PORT);
	zassert_equal(PD_ROLE_VCONN_SRC, snk_resp.vconn_role,
		      "SNK Returned vconn_role=%u", snk_resp.vconn_role);

	/* Send VCONN_SWAP request */
	rv = tcpci_partner_send_control_msg(&this->partner_emul.common_data,
					    PD_CTRL_VCONN_SWAP, 0);
	zassert_ok(rv, "Failed to send VCONN_SWAP request, rv=%d", rv);

	k_sleep(K_SECONDS(1));

	snk_resp = host_cmd_typec_status(SNK_PORT);
	zassert_equal(PD_ROLE_VCONN_OFF, snk_resp.vconn_role,
		      "SNK Returned vconn_role=%u", snk_resp.vconn_role);
}

ZTEST_F(usb_pd_ctrl_msg_test, verify_pr_swap)
{
	struct ec_response_typec_status snk_resp = { 0 };
	int rv = 0;

	/* TODO(b/228593065): Revert this once ZTEST fix before ordering
	 * is pulled in
	 */
	usb_pd_ctrl_msg_before(this);

	snk_resp = host_cmd_typec_status(SNK_PORT);
	zassert_equal(PD_ROLE_SINK, snk_resp.power_role,
		      "SNK Returned power_role=%u", snk_resp.power_role);

	/* Ignore ACCEPT in common handler for PR Swap request,
	 * causes soft reset
	 */
	tcpci_partner_common_handler_mask_msg(&this->partner_emul.common_data,
					      PD_CTRL_ACCEPT, true);

	/* Send PR_SWAP request */
	rv = tcpci_partner_send_control_msg(&this->partner_emul.common_data,
					    PD_CTRL_PR_SWAP, 0);
	zassert_ok(rv, "Failed to send PR_SWAP request, rv=%d", rv);

	/* Send PS_RDY request */
	rv = tcpci_partner_send_control_msg(&this->partner_emul.common_data,
					    PD_CTRL_PS_RDY, 15);
	zassert_ok(rv, "Failed to send PS_RDY request, rv=%d", rv);

	k_sleep(K_MSEC(20));

	snk_resp = host_cmd_typec_status(SNK_PORT);
	zassert_equal(PD_ROLE_SOURCE, snk_resp.power_role,
		      "SNK Returned power_role=%u", snk_resp.power_role);
}
