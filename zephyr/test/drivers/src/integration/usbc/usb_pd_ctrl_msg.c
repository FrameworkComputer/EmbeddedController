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

#define TEST_USB_PORT USBC_PORT_C0

#define TCPCI_EMUL_LABEL DT_NODELABEL(tcpci_emul)

#define TEST_ADDED_PDO PDO_FIXED(10000, 3000, PDO_FIXED_UNCONSTRAINED)

struct usb_pd_ctrl_msg_test_fixture {
	struct tcpci_drp_emul partner_emul;
	const struct emul *tcpci_emul;
	const struct emul *charger_emul;
	bool drp_partner_is_sink;
};

struct usb_pd_ctrl_msg_test_sink_fixture {
	struct usb_pd_ctrl_msg_test_fixture fixture;
};

struct usb_pd_ctrl_msg_test_source_fixture {
	struct usb_pd_ctrl_msg_test_fixture fixture;
};

static void tcpci_drp_emul_connect_partner(struct tcpci_drp_emul *partner_emul,
					   const struct emul *tcpci_emul,
					   const struct emul *charger_emul)
{
	/*
	 * TODO(b/221439302) Updating the TCPCI emulator registers, updating the
	 *   vbus, as well as alerting should all be a part of the connect
	 *   function.
	 */
	isl923x_emul_set_adc_vbus(charger_emul, 0);
	tcpci_emul_set_reg(tcpci_emul, TCPC_REG_POWER_STATUS,
			   TCPC_REG_POWER_STATUS_VBUS_DET);

	tcpci_emul_set_reg(tcpci_emul, TCPC_REG_EXT_STATUS,
			   TCPC_REG_EXT_STATUS_SAFE0V);

	tcpci_tcpc_alert(TEST_USB_PORT);

	zassume_ok(tcpci_drp_emul_connect_to_tcpci(
			   &partner_emul->data, &partner_emul->src_data,
			   &partner_emul->snk_data, &partner_emul->common_data,
			   &partner_emul->ops, tcpci_emul),
		   NULL);
}

static void disconnect_partner(struct usb_pd_ctrl_msg_test_fixture *fixture)
{
	zassume_ok(tcpci_emul_disconnect_partner(fixture->tcpci_emul), NULL);
	k_sleep(K_SECONDS(1));
}

static void *usb_pd_ctrl_msg_setup_emul(void)
{
	static struct usb_pd_ctrl_msg_test_fixture fixture;

	/* Get references for the emulators */
	fixture.tcpci_emul =
		emul_get_binding(DT_LABEL(DT_NODELABEL(tcpci_emul)));
	fixture.charger_emul =
		emul_get_binding(DT_LABEL(DT_NODELABEL(isl923x_emul)));

	tcpci_emul_set_rev(fixture.tcpci_emul, TCPCI_EMUL_REV2_0_VER1_1);

	return &fixture;
}

static void *usb_pd_ctrl_msg_sink_setup(void)
{
	struct usb_pd_ctrl_msg_test_fixture *fixture =
		usb_pd_ctrl_msg_setup_emul();

	fixture->drp_partner_is_sink = true;

	return fixture;
}

static void *usb_pd_ctrl_msg_source_setup(void)
{
	struct usb_pd_ctrl_msg_test_fixture *fixture =
		usb_pd_ctrl_msg_setup_emul();

	fixture->drp_partner_is_sink = false;

	return fixture;
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

	tcpci_drp_emul_init(&fixture->partner_emul, PD_REV20);

	/* Add additional Sink PDO to partner to verify
	 * PE_DR_SNK_Get_Sink_Cap/PE_SRC_Get_Sink_Cap (these are shared PE
	 * states) state was reached
	 */
	fixture->partner_emul.snk_data.pdo[1] = TEST_ADDED_PDO;

	fixture->partner_emul.data.sink = fixture->drp_partner_is_sink;

	/* Turn TCPCI rev 2 ON */
	tcpc_config[TEST_USB_PORT].flags |= TCPC_FLAGS_TCPCI_REV2_0;

	/* Reset to disconnected state */
	disconnect_partner(fixture);

	tcpci_drp_emul_connect_partner(&fixture->partner_emul,
				       fixture->tcpci_emul,
				       fixture->charger_emul);

	k_sleep(K_SECONDS(10));
}

static void usb_pd_ctrl_msg_after(void *data)
{
	struct usb_pd_ctrl_msg_test_fixture *fixture = data;

	disconnect_partner(fixture);
}

/** ZTEST_SUITE to setup DRP partner_emul as SINK */
ZTEST_SUITE(usb_pd_ctrl_msg_test_sink, drivers_predicate_post_main,
	    usb_pd_ctrl_msg_sink_setup, usb_pd_ctrl_msg_before,
	    usb_pd_ctrl_msg_after, NULL);

/** ZTEST_SUITE to setup DRP partner_emul as SOURCE  */
ZTEST_SUITE(usb_pd_ctrl_msg_test_source, drivers_predicate_post_main,
	    usb_pd_ctrl_msg_source_setup, usb_pd_ctrl_msg_before,
	    usb_pd_ctrl_msg_after, NULL);

ZTEST_F(usb_pd_ctrl_msg_test_sink, verify_vconn_swap)
{
	struct usb_pd_ctrl_msg_test_fixture *fixture = &this->fixture;
	struct ec_response_typec_status snk_resp = { 0 };
	int rv = 0;

	snk_resp = host_cmd_typec_status(TEST_USB_PORT);

	zassert_equal(PD_ROLE_VCONN_SRC, snk_resp.vconn_role,
		      "SNK Returned vconn_role=%u", snk_resp.vconn_role);

	/* Send VCONN_SWAP request */
	rv = tcpci_partner_send_control_msg(&fixture->partner_emul.common_data,
					    PD_CTRL_VCONN_SWAP, 0);
	zassert_ok(rv, "Failed to send VCONN_SWAP request, rv=%d", rv);

	k_sleep(K_SECONDS(1));

	snk_resp = host_cmd_typec_status(TEST_USB_PORT);
	zassert_equal(PD_ROLE_VCONN_OFF, snk_resp.vconn_role,
		      "SNK Returned vconn_role=%u", snk_resp.vconn_role);
}

ZTEST_F(usb_pd_ctrl_msg_test_sink, verify_pr_swap)
{
	struct usb_pd_ctrl_msg_test_fixture *fixture = &this->fixture;
	struct ec_response_typec_status snk_resp = { 0 };
	int rv = 0;

	snk_resp = host_cmd_typec_status(TEST_USB_PORT);

	zassert_equal(PD_ROLE_SINK, snk_resp.power_role,
		      "SNK Returned power_role=%u", snk_resp.power_role);

	/* Ignore ACCEPT in common handler for PR Swap request,
	 * causes soft reset
	 */
	tcpci_partner_common_handler_mask_msg(
		&fixture->partner_emul.common_data, PD_CTRL_ACCEPT, true);

	/* Send PR_SWAP request */
	rv = tcpci_partner_send_control_msg(&fixture->partner_emul.common_data,
					    PD_CTRL_PR_SWAP, 0);
	zassert_ok(rv, "Failed to send PR_SWAP request, rv=%d", rv);

	/* Send PS_RDY request */
	rv = tcpci_partner_send_control_msg(&fixture->partner_emul.common_data,
					    PD_CTRL_PS_RDY, 15);
	zassert_ok(rv, "Failed to send PS_RDY request, rv=%d", rv);

	k_sleep(K_MSEC(20));

	snk_resp = host_cmd_typec_status(TEST_USB_PORT);
	zassert_equal(PD_ROLE_SOURCE, snk_resp.power_role,
		      "SNK Returned power_role=%u", snk_resp.power_role);
}

/**
 * @brief TestPurpose: Verify DR Swap when DRP partner is configured as sink.
 *
 * @details
 *  - TCPM is brought up as Sink/UFP
 *  - TCPM over time will evaluate and trigger DR Swap to Sink/DFP
 *
 * Expected Results
 *  - TypeC status query returns PD_ROLE_DFP
 */
ZTEST_F(usb_pd_ctrl_msg_test_sink, verify_dr_swap)
{
	struct ec_response_typec_status typec_status =
		host_cmd_typec_status(TEST_USB_PORT);

	zassert_equal(PD_ROLE_DFP, typec_status.data_role,
		      "Returned data_role=%u", typec_status.data_role);
}

/**
 * @brief TestPurpose: Verify DR Swap is rejected when DRP partner is
 * configured as source.
 *
 * @details
 *  - TCPM is configured initially as Sink/UFP.
 *  - TCPM initiates DR swap according to policy (Sink/DFP)
 *  - Partner requests DR Swap.
 *  - Verify Request is rejected due the TCPM not being UFP.
 *
 * Expected Results
 *  - Data role does not change on TEST_USB_PORT after DR Swap request.
 */
ZTEST_F(usb_pd_ctrl_msg_test_source, verify_dr_swap_rejected)
{
	struct usb_pd_ctrl_msg_test_fixture *fixture = &this->fixture;
	struct ec_response_typec_status typec_status = { 0 };
	int rv = 0;

	typec_status = host_cmd_typec_status(TEST_USB_PORT);
	zassert_equal(PD_ROLE_DFP, typec_status.data_role,
		      "Returned data_role=%u", typec_status.data_role);

	/* Send DR_SWAP request */
	rv = tcpci_partner_send_control_msg(&fixture->partner_emul.common_data,
					    PD_CTRL_DR_SWAP, 0);
	zassert_ok(rv, "Failed to send DR_SWAP request, rv=%d", rv);

	k_sleep(K_MSEC(20));

	/* Verify DR_Swap request is REJECTED */
	typec_status = host_cmd_typec_status(TEST_USB_PORT);
	zassert_equal(PD_ROLE_DFP, typec_status.data_role,
		      "Returned data_role=%u", typec_status.data_role);
}

/**
 * @brief TestPurpose: Verify DR Swap via DPM request when DRP is configured
 * as source
 *
 * @details
 *  - TCPM is configured initially as Sink/UFP.
 *  - TCPM initiates DR swap according to policy (Sink/DFP)
 *  - Test case initiates DPM DR Swap.
 *  - Verify DR Swap Request is processed.
 *
 * Expected Results
 *  - Data role changes after DPM DR Swap request
 */
ZTEST_F(usb_pd_ctrl_msg_test_source, verify_dpm_dr_swap)
{
	struct ec_response_typec_status typec_status = { 0 };

	typec_status = host_cmd_typec_status(TEST_USB_PORT);
	zassert_equal(PD_ROLE_DFP, typec_status.data_role,
		      "Returned data_role=%u", typec_status.data_role);

	pd_dpm_request(TEST_USB_PORT, DPM_REQUEST_DR_SWAP);
	k_sleep(K_SECONDS(1));

	typec_status = host_cmd_typec_status(TEST_USB_PORT);
	zassert_equal(PD_ROLE_UFP, typec_status.data_role,
		      "Returned data_role=%u", typec_status.data_role);
}

/**
 * @brief TestPurpose: Verify TCPM initiates Get_Sink_Cap message during a typec
 * status host command and receives sink_capabilities message.
 *
 * @details
 *  - TCPM is configured initially as Sink
 *  - TypeC Status Host Command is Invoked
 *
 * Expected Results
 *  - TypeC Status Host Command reveals sink capabilility PDOs.
 */
ZTEST(usb_pd_ctrl_msg_test_source, verify_dpm_get_sink_cap)
{
	struct ec_response_typec_status typec_status = { 0 };

	typec_status = host_cmd_typec_status(TEST_USB_PORT);

	zassert_true(typec_status.sink_cap_count > 1, NULL);
	zassert_equal(typec_status.sink_cap_pdos[1], TEST_ADDED_PDO, NULL);
}

/**
 * @brief TestPurpose: Verify TCPM initiates Get_Sink_Cap message during a typec
 * status host command and receives sink_capabilities message.
 *
 * @details
 *  - TCPM is configured initially as Source
 *  - TypeC Status Host Command is Invoked
 *
 * Expected Results
 *  - TypeC Status Host Command reveals sink capabilility PDOs.
 */
ZTEST(usb_pd_ctrl_msg_test_sink, verify_get_sink_cap)
{
	struct ec_response_typec_status typec_status = { 0 };

	typec_status = host_cmd_typec_status(TEST_USB_PORT);

	zassert_true(typec_status.sink_cap_count > 1, NULL);
	zassert_equal(typec_status.sink_cap_pdos[1], TEST_ADDED_PDO, NULL);
}
