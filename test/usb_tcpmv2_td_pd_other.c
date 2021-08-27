/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "mock/tcpci_i2c_mock.h"
#include "task.h"
#include "tcpm/tcpci.h"
#include "test_util.h"
#include "timer.h"
#include "usb_tcpmv2_compliance.h"
#include "usb_tc_sm.h"
#include "usb_prl_sm.h"

int test_connect_as_nonpd_sink(void)
{
	task_wait_event(10 * SECOND);

	/* Simulate a non-PD power supply being plugged in. */
	mock_set_cc(MOCK_CC_DUT_IS_SNK, MOCK_CC_SNK_OPEN, MOCK_CC_SNK_RP_3_0);
	mock_set_alert(TCPC_REG_ALERT_CC_STATUS);

	task_wait_event(50 * MSEC);

	mock_tcpci_set_reg(TCPC_REG_POWER_STATUS,
			   TCPC_REG_POWER_STATUS_VBUS_PRES);
	mock_set_alert(TCPC_REG_ALERT_POWER_STATUS);

	task_wait_event(10 * SECOND);
	TEST_EQ(tc_is_attached_snk(PORT0), true, "%d");

	return EC_SUCCESS;
}

int test_retry_count_sop(void)
{
	/* DRP auto-toggling with AP in S0, source enabled. */
	TEST_EQ(tcpci_startup(), EC_SUCCESS, "%d");

	/*
	 * The test starts in a disconnected state.
	 */
	mock_tcpci_set_reg(TCPC_REG_EXT_STATUS, TCPC_REG_EXT_STATUS_SAFE0V);
	mock_set_alert(TCPC_REG_ALERT_EXT_STATUS);
	task_wait_event(10 * SECOND);

	/*
	 * The Tester applies Rd and waits for Vbus for tNoResponse max.
	 */
	mock_set_cc(MOCK_CC_DUT_IS_SRC, MOCK_CC_SRC_OPEN, MOCK_CC_SRC_RD);
	mock_set_alert(TCPC_REG_ALERT_CC_STATUS);

	/*
	 * The Tester waits for Source_Capabilities for tNoResponse max.
	 *
	 * Source Caps is SOP message which should be retried at TCPC layer.
	 * The retry count for PD3 should be 2.
	 */
	TEST_EQ(verify_tcpci_tx_retry_count(TCPCI_MSG_SOP, 0,
				PD_DATA_SOURCE_CAP, 2),
		EC_SUCCESS, "%d");
	return EC_SUCCESS;
}

int test_retry_count_hard_reset(void)
{
	/* DRP auto-toggling with AP in S0, source enabled. */
	TEST_EQ(tcpci_startup(), EC_SUCCESS, "%d");

	/*
	 * The test starts in a disconnected state.
	 */
	mock_tcpci_set_reg(TCPC_REG_EXT_STATUS, TCPC_REG_EXT_STATUS_SAFE0V);
	mock_set_alert(TCPC_REG_ALERT_EXT_STATUS);
	task_wait_event(10 * SECOND);

	/*
	 * The Tester applies Rd and waits for Vbus for tNoResponse max.
	 */
	mock_set_cc(MOCK_CC_DUT_IS_SRC, MOCK_CC_SRC_OPEN, MOCK_CC_SRC_RD);
	mock_set_alert(TCPC_REG_ALERT_CC_STATUS);

	/*
	 * The Tester waits for Source_Capabilities for tNoResponse max.
	 */
	TEST_EQ(verify_tcpci_transmit(TCPCI_MSG_SOP, 0, PD_DATA_SOURCE_CAP),
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
	TEST_EQ(verify_tcpci_tx_retry_count(TCPCI_MSG_TX_HARD_RESET, 0, 0, 0),
		EC_SUCCESS, "%d");

	return EC_SUCCESS;
}
