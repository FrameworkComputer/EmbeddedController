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

/*****************************************************************************
 * TD.PD.SNK3.E12.Soft_Reset sent regardless of Rp value
 *
 * Description:
 *	As Provider (DFP), the Tester forces the UUT to send Soft_Reset and
 *	verifies Soft_Reset is sent regardless even though the Rp value is
 *	SinkTxNG.
 */
int test_td_pd_snk3_e12(void)
{
	/*
	 * TD.PD.SNK3.E12.Soft_Reset sent regardless of Rp value
	 * a) Run PROC.PD.E1 Bring-up according to the UUT role.
	 */
	TEST_EQ(tcpci_startup(), EC_SUCCESS, "%d");
	TEST_EQ(proc_pd_e1(PD_ROLE_UFP, INITIAL_AND_ALREADY_ATTACHED),
		EC_SUCCESS, "%d");

	/*
	 * b) The Tester keeps the Rp value as SinkTXNG and sends a
	 * Get_Sink_Cap message to the UUT.
	 */
	partner_send_msg(TCPCI_MSG_SOP, PD_CTRL_GET_SINK_CAP, 0, 0, NULL);

	/*
	 * c) Upon receipt of the Sink_Capabilities Message, the Tester doesn't
	 * reply with GoodCRC.
	 */
	TEST_EQ(verify_tcpci_transmit(TCPCI_MSG_SOP, 0, PD_DATA_SINK_CAP),
		EC_SUCCESS, "%d");
	mock_set_alert(TCPC_REG_ALERT_TX_FAILED);

	/*
	 * d) The Tester verifies that a Soft_Reset message is sent by the UUT
	 * within tReceive max + tSoftReset max
	 */
	TEST_EQ(verify_tcpci_tx_timeout(
			TCPCI_MSG_SOP, PD_CTRL_SOFT_RESET, 0, 16 * MSEC),
		EC_SUCCESS, "%d");
	mock_set_alert(TCPC_REG_ALERT_TX_SUCCESS);

	return EC_SUCCESS;
}
