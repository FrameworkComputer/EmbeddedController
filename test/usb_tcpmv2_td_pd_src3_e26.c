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
 * TD.PD.SRC3.E26.Soft_Reset sent regardless of Rp value
 *
 * Description:
 *	As Consumer (UFP), the Tester forces the UUT to send Soft_Reset and
 *	verifies Soft_Reset is sent regardless of the Rp value is SinkTxOK or
 *	SinkTxNG.
 */
int test_td_pd_src3_e26(void)
{
	/*
	 * TD.PD.SRC3.E26.Soft_Reset sent regardless of Rp value
	 * a) Run PROC.PD.E1 Bring-up according to the UUT role.
	 * b) The Tester waits until it can start an AMS (Run PROC.PD.E3)...
	 */
	TEST_EQ(tcpci_startup(), EC_SUCCESS, "%d");
	TEST_EQ(proc_pd_e1(PD_ROLE_DFP, INITIAL_AND_ALREADY_ATTACHED),
		EC_SUCCESS, "%d");
	TEST_EQ(proc_pd_e3(), EC_SUCCESS, "%d");

	/*
	 * ...and sends a Get_Source_Cap message to the UUT.
	 */
	partner_send_msg(TCPC_TX_SOP, PD_CTRL_GET_SOURCE_CAP, 0, 0, NULL);

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
