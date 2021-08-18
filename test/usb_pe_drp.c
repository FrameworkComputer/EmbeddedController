/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Test USB PE module.
 */
#include "common.h"
#include "task.h"
#include "test_util.h"
#include "timer.h"
#include "usb_emsg.h"
#include "usb_mux.h"
#include "usb_pe.h"
#include "usb_pe_sm.h"
#include "usb_sm_checks.h"
#include "mock/charge_manager_mock.h"
#include "mock/usb_tc_sm_mock.h"
#include "mock/tcpc_mock.h"
#include "mock/usb_mux_mock.h"
#include "mock/usb_pd_dpm_mock.h"
#include "mock/dp_alt_mode_mock.h"
#include "mock/usb_prl_mock.h"

/* Install Mock TCPC and MUX drivers */
const struct tcpc_config_t tcpc_config[CONFIG_USB_PD_PORT_MAX_COUNT] = {
	{
		.drv = &mock_tcpc_driver,
	},
};

const struct usb_mux usb_muxes[CONFIG_USB_PD_PORT_MAX_COUNT] = {
	{
		.driver = &mock_usb_mux_driver,
	}
};

void before_test(void)
{
	mock_tc_port_reset();
	mock_tcpc_reset();
	mock_usb_mux_reset();
	mock_dpm_reset();
	mock_dp_alt_mode_reset();
	mock_prl_reset();
	pe_clear_port_data(PORT0);

	/* Restart the PD task and let it settle */
	task_set_event(TASK_ID_PD_C0, TASK_EVENT_RESET_DONE);
	task_wait_event(SECOND);
}

/*
 * This assumes data messages only contain a single data object (uint32_t data).
 * TODO: Add support for multiple data objects (when a test is added here that
 * needs it).
 */
test_static void rx_message(enum tcpm_sop_type sop,
			    enum pd_ctrl_msg_type ctrl_msg,
			    enum pd_data_msg_type data_msg,
			    enum pd_power_role prole,
			    enum pd_data_role drole,
			    uint32_t data)
{
	int type, cnt;

	if (ctrl_msg != 0) {
		type = ctrl_msg;
		cnt = 0;
	} else {
		type = data_msg;
		cnt = 1;
	}
	rx_emsg[PORT0].header = (PD_HEADER_SOP(sop)
		| PD_HEADER(type, prole, drole, 0, cnt, PD_REV30, 0));
	rx_emsg[PORT0].len = cnt * 4;
	*(uint32_t *)rx_emsg[PORT0].buf = data;
	mock_prl_message_received(PORT0);
}

/*
 * This sequence is used by multiple tests, so pull out into a function to
 * avoid duplication.
 *
 * Send in how many SOP' DiscoverIdentity requests have been processed so far,
 * as this may vary depending on startup sequencing as a source.
 */
test_static int finish_src_discovery(int startup_cable_probes)
{
	int i;

	/* Expect GET_SOURCE_CAP, reply NOT_SUPPORTED. */
	TEST_EQ(mock_prl_wait_for_tx_msg(PORT0, TCPC_TX_SOP,
					 PD_CTRL_GET_SOURCE_CAP, 0, 10 * MSEC),
		EC_SUCCESS, "%d");
	mock_prl_message_sent(PORT0);
	task_wait_event(10 * MSEC);
	rx_message(TCPC_TX_SOP, PD_CTRL_NOT_SUPPORTED, 0,
		   PD_ROLE_SINK, PD_ROLE_UFP, 0);

	/*
	 * Cable identity discovery is attempted 6 times total. 1 was done
	 * above, so expect 5 more now.
	 */
	for (i = startup_cable_probes; i < 6; i++) {
		TEST_EQ(mock_prl_wait_for_tx_msg(PORT0, TCPC_TX_SOP_PRIME,
						 0, PD_DATA_VENDOR_DEF,
						 60 * MSEC),
			EC_SUCCESS, "%d");
		mock_prl_report_error(PORT0, ERR_TCH_XMIT, TCPC_TX_SOP_PRIME);
	}

	/* Expect VENDOR_DEF for partner identity, reply NOT_SUPPORTED. */
	TEST_EQ(mock_prl_wait_for_tx_msg(PORT0, TCPC_TX_SOP,
					 0, PD_DATA_VENDOR_DEF, 10 * MSEC),
		EC_SUCCESS, "%d");
	mock_prl_message_sent(PORT0);
	task_wait_event(10 * MSEC);
	rx_message(TCPC_TX_SOP, PD_CTRL_NOT_SUPPORTED, 0,
		   PD_ROLE_SINK, PD_ROLE_UFP, 0);

	return EC_SUCCESS;
}

/*
 * Verify that, before connection, PE_SRC_Send_Capabilities goes to
 * PE_SRC_Discovery on send error, not PE_Send_Soft_Reset.
 */
test_static int test_send_caps_error_before_connected(void)
{
	/* Enable PE as source, expect SOURCE_CAP. */
	mock_tc_port[PORT0].power_role = PD_ROLE_SOURCE;
	mock_tc_port[PORT0].pd_enable = 1;
	mock_tc_port[PORT0].vconn_src = true;
	TEST_EQ(mock_prl_wait_for_tx_msg(PORT0, TCPC_TX_SOP,
					 0, PD_DATA_SOURCE_CAP, 10 * MSEC),
		EC_SUCCESS, "%d");

	/*
	 * Simulate error sending SOURCE_CAP, to test that before connection,
	 * PE_SRC_Send_Capabilities goes to PE_SRC_Discovery on send error (and
	 * does not send soft reset).
	 */
	mock_prl_report_error(PORT0, ERR_TCH_XMIT, TCPC_TX_SOP);

	/*
	 * We should have gone to PE_SRC_Discovery on above error, so expect
	 * VENDOR_DEF for cable identity, simulate no cable.
	 */
	TEST_EQ(mock_prl_wait_for_tx_msg(PORT0, TCPC_TX_SOP_PRIME,
					 0, PD_DATA_VENDOR_DEF, 10 * MSEC),
		EC_SUCCESS, "%d");
	mock_prl_report_error(PORT0, ERR_TCH_XMIT, TCPC_TX_SOP_PRIME);

	/*
	 * Expect SOURCE_CAP again. This is a retry since the first one above
	 * got ERR_TCH_XMIT. Now simulate success (ie GoodCRC).
	 */
	TEST_EQ(mock_prl_wait_for_tx_msg(PORT0, TCPC_TX_SOP,
					 0, PD_DATA_SOURCE_CAP, 110 * MSEC),
		EC_SUCCESS, "%d");
	mock_prl_message_sent(PORT0);
	task_wait_event(10 * MSEC);

	/*
	 * From here, the sequence is very similar between
	 * test_send_caps_error_before_connected and
	 * test_send_caps_error_when_connected. We could end the test now, but
	 * keep going just to check that the slightly different ordering of
	 * cable identity discovery doesn't cause any issue below.
	 */

	/* REQUEST 5V, expect ACCEPT, PS_RDY. */
	rx_message(TCPC_TX_SOP, 0, PD_DATA_REQUEST,
		   PD_ROLE_SINK, PD_ROLE_UFP, RDO_FIXED(1, 500, 500, 0));
	TEST_EQ(mock_prl_wait_for_tx_msg(PORT0, TCPC_TX_SOP,
					 PD_CTRL_ACCEPT, 0, 10 * MSEC),
		EC_SUCCESS, "%d");
	mock_prl_message_sent(PORT0);
	TEST_EQ(mock_prl_wait_for_tx_msg(PORT0, TCPC_TX_SOP,
					 PD_CTRL_PS_RDY, 0, 35 * MSEC),
		EC_SUCCESS, "%d");
	mock_prl_message_sent(PORT0);

	TEST_EQ(finish_src_discovery(1), EC_SUCCESS, "%d");

	task_wait_event(5 * SECOND);

	return EC_SUCCESS;
}

/*
 * Verify that, after connection, PE_SRC_Send_Capabilities goes to
 * PE_Send_Soft_Reset on send error, not PE_SRC_Discovery.
 */
test_static int test_send_caps_error_when_connected(void)
{
	/* Enable PE as source, expect SOURCE_CAP. */
	mock_tc_port[PORT0].power_role = PD_ROLE_SOURCE;
	mock_tc_port[PORT0].pd_enable = 1;
	mock_tc_port[PORT0].vconn_src = true;
	TEST_EQ(mock_prl_wait_for_tx_msg(PORT0, TCPC_TX_SOP,
					 0, PD_DATA_SOURCE_CAP, 10 * MSEC),
		EC_SUCCESS, "%d");
	mock_prl_message_sent(PORT0);
	task_wait_event(10 * MSEC);

	/* REQUEST 5V, expect ACCEPT, PS_RDY. */
	rx_message(TCPC_TX_SOP, 0, PD_DATA_REQUEST,
		   PD_ROLE_SINK, PD_ROLE_UFP, RDO_FIXED(1, 500, 500, 0));
	TEST_EQ(mock_prl_wait_for_tx_msg(PORT0, TCPC_TX_SOP,
					 PD_CTRL_ACCEPT, 0, 10 * MSEC),
		EC_SUCCESS, "%d");
	mock_prl_message_sent(PORT0);
	TEST_EQ(mock_prl_wait_for_tx_msg(PORT0, TCPC_TX_SOP,
					 PD_CTRL_PS_RDY, 0, 35 * MSEC),
		EC_SUCCESS, "%d");
	mock_prl_message_sent(PORT0);

	TEST_EQ(finish_src_discovery(0), EC_SUCCESS, "%d");

	task_wait_event(5 * SECOND);

	/*
	 * Now connected. Send GET_SOURCE_CAP, to check how error sending
	 * SOURCE_CAP is handled.
	 */
	rx_message(TCPC_TX_SOP, PD_CTRL_GET_SOURCE_CAP, 0,
		   PD_ROLE_SINK, PD_ROLE_UFP, 0);
	TEST_EQ(mock_prl_wait_for_tx_msg(PORT0, TCPC_TX_SOP,
					 0, PD_DATA_SOURCE_CAP, 10 * MSEC),
		EC_SUCCESS, "%d");

	/* Simulate error sending SOURCE_CAP. */
	mock_prl_report_error(PORT0, ERR_TCH_XMIT, TCPC_TX_SOP);

	/*
	 * Expect SOFT_RESET.
	 * See section 8.3.3.4.1.1 PE_SRC_Send_Soft_Reset State and section
	 * 8.3.3.2.3 PE_SRC_Send_Capabilities State.
	 * "The PE_SRC_Send_Soft_Reset state Shall be entered from any state
	 * when ... A Message has not been sent after retries to the Sink"
	 */
	TEST_EQ(mock_prl_wait_for_tx_msg(PORT0, TCPC_TX_SOP,
					 PD_CTRL_SOFT_RESET, 0, 10 * MSEC),
		EC_SUCCESS, "%d");
	mock_prl_message_sent(PORT0);

	task_wait_event(5 * SECOND);

	return EC_SUCCESS;
}

/*
 * Verify that when PR swap is interrupted during power transitiong, hard
 * reset is sent
 */
test_static int test_interrupting_pr_swap(void)
{
	/* Enable PE as source, expect SOURCE_CAP. */
	mock_tc_port[PORT0].power_role = PD_ROLE_SOURCE;
	mock_tc_port[PORT0].pd_enable = 1;
	mock_tc_port[PORT0].vconn_src = true;
	TEST_EQ(mock_prl_wait_for_tx_msg(PORT0, TCPC_TX_SOP,
					 0, PD_DATA_SOURCE_CAP, 10 * MSEC),
		EC_SUCCESS, "%d");
	mock_prl_message_sent(PORT0);
	task_wait_event(10 * MSEC);

	/* REQUEST 5V, expect ACCEPT, PS_RDY. */
	rx_message(TCPC_TX_SOP, 0, PD_DATA_REQUEST,
		   PD_ROLE_SINK, PD_ROLE_UFP, RDO_FIXED(1, 500, 500, 0));
	TEST_EQ(mock_prl_wait_for_tx_msg(PORT0, TCPC_TX_SOP,
					 PD_CTRL_ACCEPT, 0, 10 * MSEC),
		EC_SUCCESS, "%d");
	mock_prl_message_sent(PORT0);
	TEST_EQ(mock_prl_wait_for_tx_msg(PORT0, TCPC_TX_SOP,
					 PD_CTRL_PS_RDY, 0, 35 * MSEC),
		EC_SUCCESS, "%d");
	mock_prl_message_sent(PORT0);

	TEST_EQ(finish_src_discovery(0), EC_SUCCESS, "%d");

	task_wait_event(5 * SECOND);

	/*
	 * Now connected.  Initiate a PR swap and then interrupt it after the
	 * Accept, when power is transitioning to off.
	 */
	rx_message(TCPC_TX_SOP, PD_CTRL_PR_SWAP, 0,
		   PD_ROLE_SINK, PD_ROLE_UFP, 0);

	TEST_EQ(mock_prl_wait_for_tx_msg(PORT0, TCPC_TX_SOP,
					 PD_CTRL_ACCEPT, 0, 10 * MSEC),
		EC_SUCCESS, "%d");
	mock_prl_message_sent(PORT0);

	task_wait_event(5 * SECOND);

	/* Interrupt the non-interruptible AMS */
	rx_message(TCPC_TX_SOP, PD_CTRL_PR_SWAP, 0,
		   PD_ROLE_SINK, PD_ROLE_UFP, 0);

	/*
	 * Expect a hard reset since power was transitioning during this
	 * interruption
	 */
	TEST_EQ(mock_prl_wait_for_tx_msg(PORT0, TCPC_TX_HARD_RESET,
					 0, 0, 10 * MSEC),
		EC_SUCCESS, "%d");

	return EC_SUCCESS;
}

void run_test(int argc, char **argv)
{
	test_reset();

	RUN_TEST(test_send_caps_error_before_connected);
	RUN_TEST(test_send_caps_error_when_connected);
	RUN_TEST(test_interrupting_pr_swap);

	/* Do basic state machine validity checks last. */
	RUN_TEST(test_pe_no_parent_cycles);

	test_print_result();
}
