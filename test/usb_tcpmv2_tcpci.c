/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "hooks.h"
#include "mock/tcpci_i2c_mock.h"
#include "mock/usb_mux_mock.h"
#include "task.h"
#include "tcpci.h"
#include "test_util.h"
#include "timer.h"
#include "usb_mux.h"
#include "usb_tc_sm.h"
#include "usb_prl_sm.h"

#define PORT0 0

enum mock_cc_state {
	MOCK_CC_SRC_OPEN = 0,
	MOCK_CC_SNK_OPEN = 0,
	MOCK_CC_SRC_RA = 1,
	MOCK_CC_SNK_RP_DEF = 1,
	MOCK_CC_SRC_RD = 2,
	MOCK_CC_SNK_RP_1_5 = 2,
	MOCK_CC_SNK_RP_3_0 = 3,
};
enum mock_connect_result {
	MOCK_CC_WE_ARE_SRC = 0,
	MOCK_CC_WE_ARE_SNK = 1,
};

__maybe_unused static void mock_set_cc(enum mock_connect_result cr,
	enum mock_cc_state cc1, enum mock_cc_state cc2)
{
	mock_tcpci_set_reg(TCPC_REG_CC_STATUS,
		TCPC_REG_CC_STATUS_SET(cr, cc1, cc2));
}

__maybe_unused static void mock_set_role(int drp, enum tcpc_rp_value rp,
	enum tcpc_cc_pull cc1, enum tcpc_cc_pull cc2)
{
	mock_tcpci_set_reg(TCPC_REG_ROLE_CTRL,
		TCPC_REG_ROLE_CTRL_SET(drp, rp, cc1, cc2));
}

static int mock_alert_count;

__maybe_unused static void mock_set_alert(int alert)
{
	mock_tcpci_set_reg(TCPC_REG_ALERT, alert);
	mock_alert_count = 1;
	schedule_deferred_pd_interrupt(PORT0);
}

uint16_t tcpc_get_alert_status(void)
{
	ccprints("mock_alert_count %d", mock_alert_count);
	if (mock_alert_count > 0) {
		mock_alert_count--;
		return PD_STATUS_TCPC_ALERT_0;
	}
	return 0;
}

static int rx_id;

const struct svdm_response svdm_rsp = {
	.identity = NULL,
	.svids = NULL,
	.modes = NULL,
};

bool vboot_allow_usb_pd(void)
{
	return 1;
}

int pd_check_vconn_swap(int port)
{
	return 1;
}

void board_reset_pd_mcu(void) {}

const struct tcpc_config_t tcpc_config[CONFIG_USB_PD_PORT_MAX_COUNT] = {
	{
		.bus_type = EC_BUS_TYPE_I2C,
		.i2c_info = {
			.port = I2C_PORT_HOST_TCPC,
			.addr_flags = MOCK_TCPCI_I2C_ADDR_FLAGS,
		},
		.drv = &tcpci_tcpm_drv,
		.flags = TCPC_FLAGS_TCPCI_REV2_0,
	},
};

const struct usb_mux usb_muxes[CONFIG_USB_PD_PORT_MAX_COUNT] = {
	{
		.driver = &mock_usb_mux_driver,
	}
};

__maybe_unused static int test_connect_as_nonpd_sink(void)
{
	task_wait_event(10 * SECOND);

	/* Simulate a non-PD power supply being plugged in. */
	mock_set_cc(MOCK_CC_WE_ARE_SNK, MOCK_CC_SNK_OPEN, MOCK_CC_SNK_RP_3_0);
	mock_set_alert(TCPC_REG_ALERT_CC_STATUS);

	task_wait_event(50 * MSEC);

	mock_tcpci_set_reg(TCPC_REG_POWER_STATUS,
			   TCPC_REG_POWER_STATUS_VBUS_PRES);
	mock_set_alert(TCPC_REG_ALERT_POWER_STATUS);

	task_wait_event(10 * SECOND);
	TEST_EQ(tc_is_attached_snk(PORT0), true, "%d");

	return EC_SUCCESS;
}

__maybe_unused static int test_startup_and_resume(void)
{
	/* Should be in low power mode before AP boots. */
	TEST_EQ(mock_tcpci_get_reg(TCPC_REG_COMMAND),
		TCPC_REG_COMMAND_I2CIDLE, "%d");
	task_wait_event(10 * SECOND);

	hook_notify(HOOK_CHIPSET_STARTUP);
	task_wait_event(5 * MSEC);
	hook_notify(HOOK_CHIPSET_RESUME);

	task_wait_event(10 * SECOND);
	/* Should be in low power mode and DRP auto-toggling with AP in S0. */
	TEST_EQ((mock_tcpci_get_reg(TCPC_REG_ROLE_CTRL)
		 & TCPC_REG_ROLE_CTRL_DRP_MASK),
		TCPC_REG_ROLE_CTRL_DRP_MASK, "%d");
	/* TODO: check previous command was TCPC_REG_COMMAND_LOOK4CONNECTION */
	TEST_EQ(mock_tcpci_get_reg(TCPC_REG_COMMAND),
		TCPC_REG_COMMAND_I2CIDLE, "%d");

	return EC_SUCCESS;
}


__maybe_unused static int test_connect_as_pd3_source(void)
{
	uint32_t rdo = RDO_FIXED(1, 500, 500, 0);
	uint32_t pdo = PDO_FIXED(5000, 500,
				 PDO_FIXED_DUAL_ROLE |
				 PDO_FIXED_DATA_SWAP |
				 PDO_FIXED_COMM_CAP);

	/* DRP auto-toggling with AP in S0, source enabled. */
	TEST_EQ(test_startup_and_resume(), EC_SUCCESS, "%d");

	/*
	 * PROC.PD.E1. Bring-up procedure for DFP(Source) UUT:
	 * a) The test starts in a disconnected state.
	 */
	mock_tcpci_set_reg(TCPC_REG_EXT_STATUS, TCPC_REG_EXT_STATUS_SAFE0V);
	mock_set_alert(TCPC_REG_ALERT_EXT_STATUS);
	task_wait_event(10 * SECOND);

	/*
	 * b) The Tester applies Rd and waits for Vbus for tNoResponse max.
	 */
	mock_set_cc(MOCK_CC_WE_ARE_SRC, MOCK_CC_SRC_OPEN, MOCK_CC_SRC_RD);
	mock_set_alert(TCPC_REG_ALERT_CC_STATUS);

	/*
	 * c) The Tester waits for Source_Capabilities for tNoResponse max.
	 */
	TEST_EQ(verify_tcpci_transmit(TCPC_TX_SOP, 0, PD_DATA_SOURCE_CAP),
		EC_SUCCESS, "%d");
	/*
	 * d) The Tester replies GoodCrc on reception of the
	 * Source_Capabilities.
	 */
	mock_set_alert(TCPC_REG_ALERT_TX_SUCCESS);
	task_wait_event(10 * MSEC);
	/*
	 * e) The Tester requests 5V 0.5A.
	 */
	mock_tcpci_receive(PD_MSG_SOP,
		PD_HEADER(PD_DATA_REQUEST, PD_ROLE_SINK,
			PD_ROLE_UFP, rx_id,
			1, PD_REV30, 0),
		&rdo);
	rx_id++;
	mock_set_alert(TCPC_REG_ALERT_RX_STATUS);
	TEST_EQ(verify_tcpci_transmit(TCPC_TX_SOP, PD_CTRL_ACCEPT, 0),
		EC_SUCCESS, "%d");
	mock_set_alert(TCPC_REG_ALERT_TX_SUCCESS);
	/*
	 * f) The Tester waits for PS_RDY for tPSSourceOn max.
	 */
	TEST_EQ(verify_tcpci_transmit(TCPC_TX_SOP, PD_CTRL_PS_RDY, 0),
		EC_SUCCESS, "%d");
	mock_set_alert(TCPC_REG_ALERT_TX_SUCCESS);

	/*
	 * PROC.PD.E3. Wait to Start AMS for DFP(Source) UUT:
	 * a) The Tester keeps monitoring the Rp value and if the UUT doesn't
	 * set the value to SinkTXOK if it doesn't have anything to send in 1s,
	 * the test fails. During this period, the Tester replies any message
	 * sent from the UUT with a proper response.
	 */
	TEST_EQ(verify_tcpci_transmit(TCPC_TX_SOP_PRIME, 0, PD_DATA_VENDOR_DEF),
		EC_SUCCESS, "%d");
	mock_set_alert(TCPC_REG_ALERT_TX_SUCCESS);
	task_wait_event(10 * MSEC);
	mock_tcpci_receive(PD_MSG_SOP_PRIME,
		PD_HEADER(PD_CTRL_NOT_SUPPORTED, PD_PLUG_FROM_CABLE,
			PD_ROLE_UFP, rx_id,
			0, PD_REV30, 0),
		NULL);
	rx_id++;
	mock_set_alert(TCPC_REG_ALERT_RX_STATUS);

	TEST_EQ(verify_tcpci_transmit(TCPC_TX_SOP, 0, PD_DATA_VENDOR_DEF),
		EC_SUCCESS, "%d");
	mock_set_alert(TCPC_REG_ALERT_TX_SUCCESS);
	task_wait_event(10 * MSEC);
	mock_tcpci_receive(PD_MSG_SOP,
		PD_HEADER(PD_CTRL_NOT_SUPPORTED, PD_ROLE_SINK,
			PD_ROLE_UFP, rx_id,
			0, PD_REV30, 0),
		NULL);
	rx_id++;
	mock_set_alert(TCPC_REG_ALERT_RX_STATUS);

	TEST_EQ(verify_tcpci_transmit(TCPC_TX_SOP, PD_CTRL_GET_SOURCE_CAP, 0),
		EC_SUCCESS, "%d");
	mock_set_alert(TCPC_REG_ALERT_TX_SUCCESS);
	task_wait_event(10 * MSEC);
	mock_tcpci_receive(PD_MSG_SOP,
		PD_HEADER(PD_DATA_SOURCE_CAP, PD_ROLE_SINK,
			PD_ROLE_UFP, rx_id,
			1, PD_REV30, 0),
		&pdo);
	rx_id++;
	mock_set_alert(TCPC_REG_ALERT_RX_STATUS);

	task_wait_event(1 * SECOND);
	TEST_EQ(tc_is_attached_src(PORT0), true, "%d");
	TEST_EQ(TCPC_REG_ROLE_CTRL_RP(mock_tcpci_get_reg(TCPC_REG_ROLE_CTRL)),
		SINK_TX_OK, "%d");

	task_wait_event(10 * SECOND);
	return EC_SUCCESS;
}

__maybe_unused static int test_retry_count_sop(void)
{
	/* DRP auto-toggling with AP in S0, source enabled. */
	TEST_EQ(test_startup_and_resume(), EC_SUCCESS, "%d");

	/*
	 * The test starts in a disconnected state.
	 */
	mock_tcpci_set_reg(TCPC_REG_EXT_STATUS, TCPC_REG_EXT_STATUS_SAFE0V);
	mock_set_alert(TCPC_REG_ALERT_EXT_STATUS);
	task_wait_event(10 * SECOND);

	/*
	 * The Tester applies Rd and waits for Vbus for tNoResponse max.
	 */
	mock_set_cc(MOCK_CC_WE_ARE_SRC, MOCK_CC_SRC_OPEN, MOCK_CC_SRC_RD);
	mock_set_alert(TCPC_REG_ALERT_CC_STATUS);

	/*
	 * The Tester waits for Source_Capabilities for tNoResponse max.
	 *
	 * Source Caps is SOP message which should be retried at TCPC layer
	 */
	TEST_EQ(verify_tcpci_tx_retry_count(TCPC_TX_SOP, CONFIG_PD_RETRY_COUNT),
		EC_SUCCESS, "%d");
	return EC_SUCCESS;
}

__maybe_unused static int test_retry_count_hard_reset(void)
{
	/* DRP auto-toggling with AP in S0, source enabled. */
	TEST_EQ(test_startup_and_resume(), EC_SUCCESS, "%d");

	/*
	 * The test starts in a disconnected state.
	 */
	mock_tcpci_set_reg(TCPC_REG_EXT_STATUS, TCPC_REG_EXT_STATUS_SAFE0V);
	mock_set_alert(TCPC_REG_ALERT_EXT_STATUS);
	task_wait_event(10 * SECOND);

	/*
	 * The Tester applies Rd and waits for Vbus for tNoResponse max.
	 */
	mock_set_cc(MOCK_CC_WE_ARE_SRC, MOCK_CC_SRC_OPEN, MOCK_CC_SRC_RD);
	mock_set_alert(TCPC_REG_ALERT_CC_STATUS);

	/*
	 * The Tester waits for Source_Capabilities for tNoResponse max.
	 */
	TEST_EQ(verify_tcpci_transmit(TCPC_TX_SOP, 0, PD_DATA_SOURCE_CAP),
		EC_SUCCESS, "%d");
	/*
	 * The Tester replies GoodCrc on reception of the Source_Capabilities.
	 */
	mock_set_alert(TCPC_REG_ALERT_TX_SUCCESS);
	task_wait_event(10 * MSEC);

	/*
	 * Now that PRL is running since we are connected, we can send a hard
	 * reset.
	 */

	/* Request that DUT send hard reset */
	prl_execute_hard_reset(PORT0);

	/* The retry count for hard resets should be 0 */
	TEST_EQ(verify_tcpci_tx_retry_count(TCPC_TX_HARD_RESET, 0),
		EC_SUCCESS, "%d");

	return EC_SUCCESS;
}

__maybe_unused static int test_pd3_source_send_soft_reset(void)
{
	/*
	 * TD.PD.SRC3.E26.Soft_Reset sent regardless of Rp value
	 * a) Run PROC.PD.E1 Bring-up according to the UUT role.
	 * b) The Tester waits until it can start an AMS (Run PROC.PD.E3)...
	 */
	TEST_EQ(test_connect_as_pd3_source(), EC_SUCCESS, "%d");

	/*
	 * ...and sends a Get_Source_Cap message to the UUT.
	 */
	mock_tcpci_receive(PD_MSG_SOP,
		PD_HEADER(PD_CTRL_GET_SOURCE_CAP, PD_ROLE_SINK,
			PD_ROLE_UFP, rx_id,
			0, PD_REV30, 0),
		NULL);
	rx_id++;
	mock_set_alert(TCPC_REG_ALERT_RX_STATUS);

	/*
	 * c) Upon receipt of the Source_Capabilities Message, the Tester
	 * doesn’t reply with GoodCRC.
	 */
	TEST_EQ(verify_tcpci_transmit(TCPC_TX_SOP, 0, PD_DATA_SOURCE_CAP),
		EC_SUCCESS, "%d");
	mock_set_alert(TCPC_REG_ALERT_TX_FAILED);

	/*
	 * d) The Tester verifies that a Soft_Reset message is sent by the UUT
	 * within tReceive max (1.1 ms) + tSoftReset max (15 ms).
	 */
	TEST_EQ(verify_tcpci_tx_timeout(
			TCPC_TX_SOP, PD_CTRL_SOFT_RESET, 0, 15 * MSEC),
		EC_SUCCESS, "%d");
	mock_set_alert(TCPC_REG_ALERT_TX_SUCCESS);

	return EC_SUCCESS;
}

void before_test(void)
{
	rx_id = 0;

	mock_usb_mux_reset();
	mock_tcpci_reset();

	/* Restart the PD task and let it settle */
	task_set_event(TASK_ID_PD_C0, TASK_EVENT_RESET_DONE, 0);
	task_wait_event(SECOND);
}

void run_test(int argc, char **argv)
{
	test_reset();

	RUN_TEST(test_connect_as_nonpd_sink);
	RUN_TEST(test_startup_and_resume);
	RUN_TEST(test_connect_as_pd3_source);
	RUN_TEST(test_retry_count_sop);
	RUN_TEST(test_retry_count_hard_reset);
	RUN_TEST(test_pd3_source_send_soft_reset);

	test_print_result();
}
