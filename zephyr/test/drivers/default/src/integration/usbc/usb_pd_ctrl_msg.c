/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "ec_tasks.h"
#include "emul/emul_isl923x.h"
#include "emul/tcpc/emul_tcpci_partner_drp.h"
#include "tcpm/tcpci.h"
#include "test/drivers/stubs.h"
#include "test/drivers/test_state.h"
#include "test/drivers/utils.h"
#include "test/usb_pe.h"
#include "usb_pd.h"

#include <stdint.h>

#include <zephyr/ztest.h>

#define TEST_USB_PORT 0
BUILD_ASSERT(TEST_USB_PORT == USBC_PORT_C0);

#define TEST_ADDED_PDO PDO_FIXED(10000, 3000, PDO_FIXED_UNCONSTRAINED)

struct usb_pd_ctrl_msg_test_fixture {
	struct tcpci_partner_data partner_emul;
	struct tcpci_snk_emul_data snk_ext;
	struct tcpci_src_emul_data src_ext;
	struct tcpci_drp_emul_data drp_ext;
	const struct emul *tcpci_emul;
	const struct emul *charger_emul;
	enum pd_power_role drp_partner_pd_role;
};

struct usb_pd_ctrl_msg_test_sink_fixture {
	struct usb_pd_ctrl_msg_test_fixture fixture;
};

struct usb_pd_ctrl_msg_test_source_fixture {
	struct usb_pd_ctrl_msg_test_fixture fixture;
};

static void
tcpci_drp_emul_connect_partner(struct tcpci_partner_data *partner_emul,
			       const struct emul *tcpci_emul,
			       const struct emul *charger_emul)
{
	isl923x_emul_set_adc_vbus(charger_emul, 0);
	zassert_ok(tcpci_emul_set_vbus_level(tcpci_emul, VBUS_SAFE0V));
	zassert_ok(tcpci_partner_connect_to_tcpci(partner_emul, tcpci_emul),
		   NULL);
}

static void disconnect_partner(struct usb_pd_ctrl_msg_test_fixture *fixture)
{
	zassert_ok(tcpci_emul_disconnect_partner(fixture->tcpci_emul));
	k_sleep(K_SECONDS(1));
}

static void *usb_pd_ctrl_msg_setup_emul(void)
{
	static struct usb_pd_ctrl_msg_test_fixture fixture;

	/* Get references for the emulators */
	fixture.tcpci_emul = EMUL_GET_USBC_BINDING(TEST_USB_PORT, tcpc);
	fixture.charger_emul = EMUL_GET_USBC_BINDING(TEST_USB_PORT, chg);

	return &fixture;
}

static void *usb_pd_ctrl_msg_sink_setup(void)
{
	struct usb_pd_ctrl_msg_test_fixture *fixture =
		usb_pd_ctrl_msg_setup_emul();

	fixture->drp_partner_pd_role = PD_ROLE_SINK;

	return fixture;
}

static void *usb_pd_ctrl_msg_source_setup(void)
{
	struct usb_pd_ctrl_msg_test_fixture *fixture =
		usb_pd_ctrl_msg_setup_emul();

	fixture->drp_partner_pd_role = PD_ROLE_SOURCE;

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

	/* Initialized DRP */
	tcpci_partner_init(&fixture->partner_emul, PD_REV20);
	fixture->partner_emul.extensions = tcpci_drp_emul_init(
		&fixture->drp_ext, &fixture->partner_emul,
		fixture->drp_partner_pd_role,
		tcpci_src_emul_init(&fixture->src_ext, &fixture->partner_emul,
				    NULL),
		tcpci_snk_emul_init(&fixture->snk_ext, &fixture->partner_emul,
				    NULL));
	/* Add additional Sink PDO to partner to verify
	 * PE_DR_SNK_Get_Sink_Cap/PE_SRC_Get_Sink_Cap (these are shared PE
	 * states) state was reached
	 */
	fixture->snk_ext.pdo[1] = TEST_ADDED_PDO;

	/* Turn TCPCI rev 2 ON */
	tcpc_config[TEST_USB_PORT].flags |= TCPC_FLAGS_TCPCI_REV2_0;

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

/**
 * @brief Verifies TCPM accepts Vconn swap when it is Vconn Source
 *
 * @details
 *  - TCPM is configured initially as Vconn Source
 *  - Partner requests VConn Swap
 *
 * Expected Results
 *  - VCONN Swap accepted
 */
ZTEST_F(usb_pd_ctrl_msg_test_sink, test_verify_vconn_swap)
{
	struct usb_pd_ctrl_msg_test_fixture *super_fixture = &fixture->fixture;
	struct ec_response_typec_status snk_resp = { 0 };
	int rv = 0;

	snk_resp = host_cmd_typec_status(TEST_USB_PORT);

	zassert_equal(PD_ROLE_VCONN_SRC, snk_resp.vconn_role,
		      "SNK Returned vconn_role=%u", snk_resp.vconn_role);

	/* Send VCONN_SWAP request */
	rv = tcpci_partner_send_control_msg(&super_fixture->partner_emul,
					    PD_CTRL_VCONN_SWAP, 0);
	zassert_ok(rv, "Failed to send VCONN_SWAP request, rv=%d", rv);

	k_sleep(K_SECONDS(1));

	snk_resp = host_cmd_typec_status(TEST_USB_PORT);
	zassert_equal(PD_ROLE_VCONN_OFF, snk_resp.vconn_role,
		      "SNK Returned vconn_role=%u", snk_resp.vconn_role);
}

/**
 * @brief Verifies TCPM obeys the board policy when it is Vconn Sink
 *
 * @details
 *  - TCPM is configured initially as Vconn Sink
 *  - Partner requests VConn Swap
 *  - Board policy rejects Vconn Swap
 *
 * Expected Results
 *  - VCONN Swap rejected
 */
ZTEST_F(usb_pd_ctrl_msg_test_source, test_verify_vconn_swap_reject)
{
	struct usb_pd_ctrl_msg_test_fixture *super_fixture = &fixture->fixture;
	struct ec_response_typec_status typec_status = { 0 };
	int rv = 0;

	/* pd_check_vconn_swap() in the test environment rejects Vconn swap in
	 * G3 */
	test_set_chipset_to_g3();
	k_sleep(K_SECONDS(1));

	typec_status = host_cmd_typec_status(TEST_USB_PORT);

	zassert_equal(PD_ROLE_VCONN_OFF, typec_status.vconn_role,
		      "Returned vconn_role=%u", typec_status.vconn_role);

	/* Send VCONN_SWAP request, pd_check_vconn_swap() should reject
	 * this because device is in G3 */
	rv = tcpci_partner_send_control_msg(&super_fixture->partner_emul,
					    PD_CTRL_VCONN_SWAP, 0);
	zassert_ok(rv, "Failed to send VCONN_SWAP request, rv=%d", rv);

	k_sleep(K_SECONDS(1));

	typec_status = host_cmd_typec_status(TEST_USB_PORT);

	zassert_equal(PD_ROLE_VCONN_OFF, typec_status.vconn_role,
		      "Returned vconn_role=%u", typec_status.vconn_role);
}

ZTEST_F(usb_pd_ctrl_msg_test_sink, test_verify_pr_swap)
{
	struct usb_pd_ctrl_msg_test_fixture *super_fixture = &fixture->fixture;
	struct ec_response_typec_status snk_resp = { 0 };
	int rv = 0;

	snk_resp = host_cmd_typec_status(TEST_USB_PORT);

	zassert_equal(PD_ROLE_SINK, snk_resp.power_role,
		      "SNK Returned power_role=%u", snk_resp.power_role);

	/* Ignore ACCEPT in common handler for PR Swap request,
	 * causes soft reset
	 */
	tcpci_partner_common_handler_mask_msg(&super_fixture->partner_emul,
					      PD_CTRL_ACCEPT, true);

	/* Send PR_SWAP request */
	rv = tcpci_partner_send_control_msg(&super_fixture->partner_emul,
					    PD_CTRL_PR_SWAP, 0);
	zassert_ok(rv, "Failed to send PR_SWAP request, rv=%d", rv);

	/* Send PS_RDY request */
	rv = tcpci_partner_send_control_msg(&super_fixture->partner_emul,
					    PD_CTRL_PS_RDY, 15);
	zassert_ok(rv, "Failed to send PS_RDY request, rv=%d", rv);

	k_sleep(K_MSEC(20));

	snk_resp = host_cmd_typec_status(TEST_USB_PORT);
	zassert_equal(PD_ROLE_SOURCE, snk_resp.power_role,
		      "SNK Returned power_role=%u", snk_resp.power_role);

	tcpci_partner_common_handler_mask_msg(&super_fixture->partner_emul,
					      PD_CTRL_ACCEPT, false);
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
ZTEST_F(usb_pd_ctrl_msg_test_sink, test_verify_dr_swap)
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
ZTEST_F(usb_pd_ctrl_msg_test_source, test_verify_dr_swap_rejected)
{
	struct usb_pd_ctrl_msg_test_fixture *super_fixture = &fixture->fixture;
	struct ec_response_typec_status typec_status = { 0 };
	int rv = 0;

	typec_status = host_cmd_typec_status(TEST_USB_PORT);
	zassert_equal(PD_ROLE_DFP, typec_status.data_role,
		      "Returned data_role=%u", typec_status.data_role);

	/* Send DR_SWAP request */
	rv = tcpci_partner_send_control_msg(&super_fixture->partner_emul,
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
ZTEST_F(usb_pd_ctrl_msg_test_source, test_verify_dpm_dr_swap)
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
ZTEST(usb_pd_ctrl_msg_test_source, test_verify_dpm_get_sink_cap)
{
	struct ec_response_typec_status typec_status = { 0 };

	typec_status = host_cmd_typec_status(TEST_USB_PORT);

	zassert_true(typec_status.sink_cap_count > 1);
	zassert_equal(typec_status.sink_cap_pdos[1], TEST_ADDED_PDO);
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
ZTEST(usb_pd_ctrl_msg_test_sink, test_verify_get_sink_cap)
{
	struct ec_response_typec_status typec_status = { 0 };

	typec_status = host_cmd_typec_status(TEST_USB_PORT);

	zassert_true(typec_status.sink_cap_count > 1);
	zassert_equal(typec_status.sink_cap_pdos[1], TEST_ADDED_PDO);
}

/**
 * @brief TestPurpose: Verify BIST TX MODE 2.
 *
 * @details
 *  - TCPM is configured initially as Sink
 *  - Initiate BIST TX
 *
 * Expected Results
 *  - BIST occurs and we transition back to READY state
 */
ZTEST_F(usb_pd_ctrl_msg_test_source, test_verify_bist_tx_mode2)
{
	struct usb_pd_ctrl_msg_test_fixture *super_fixture = &fixture->fixture;
	uint32_t bdo = BDO(BDO_MODE_CARRIER2, 0);

	tcpci_partner_send_data_msg(&super_fixture->partner_emul, PD_DATA_BIST,
				    &bdo, 1, 0);

	pd_dpm_request(TEST_USB_PORT, DPM_REQUEST_BIST_TX);
	k_sleep(K_MSEC(10));
	zassert_equal(get_state_pe(TEST_USB_PORT), PE_BIST_TX);

	k_sleep(K_SECONDS(5));
	zassert_equal(get_state_pe(TEST_USB_PORT), PE_SNK_READY);
}

/**
 * @brief TestPurpose: Verify BIST TX TEST DATA.
 *
 * @details
 *  - TCPM is configured initially as Sink
 *  - Initiate BIST TX
 *  - End testing via signaling a Hard Reset
 *
 * Expected Results
 *  - Partner remains in BIST_TX state until hard reset is received.
 */
ZTEST_F(usb_pd_ctrl_msg_test_source, test_verify_bist_tx_test_data)
{
	struct usb_pd_ctrl_msg_test_fixture *super_fixture = &fixture->fixture;
	uint32_t bdo = BDO(BDO_MODE_TEST_DATA, 0);

	tcpci_partner_send_data_msg(&super_fixture->partner_emul, PD_DATA_BIST,
				    &bdo, 1, 0);

	pd_dpm_request(TEST_USB_PORT, DPM_REQUEST_BIST_TX);
	k_sleep(K_SECONDS(5));
	zassert_equal(get_state_pe(TEST_USB_PORT), PE_BIST_TX);

	tcpci_partner_common_send_hard_reset(&super_fixture->partner_emul);
	k_sleep(K_SECONDS(2));
	zassert_equal(get_state_pe(TEST_USB_PORT), PE_SNK_READY);
}
