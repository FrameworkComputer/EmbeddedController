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

/*****************************************************************************
 * TD.PD.SRC.E5 SenderResponseTimer Timeout - Request
 *
 * Description:
 *	As Consumer (UFP), the Tester intentionally does not send the Request
 *	message, which is intended to cause a SenderResponseTimer timeout on
 *	the Provider (DFP, UUT). The Tester verifies correct implementation
 *	of this timer
 */
int test_td_pd_src_e5(void)
{
	uint64_t end_time;

	partner_set_pd_rev(PD_REV20);

	TEST_EQ(tcpci_startup(), EC_SUCCESS, "%d");

	/*
	 * a) Run PROC.PD.E1 Bring-up according to the UUT role.
	 *
	 * NOTE: Calling PROC.PD.E1 with INITIAL_ATTACH will stop just before
	 * the PD_DATA_SOURCE_CAP is verified.  We need to stop the process
	 * there to stop the REQUEST message.
	 */
	TEST_EQ(proc_pd_e1(PD_ROLE_DFP, INITIAL_ATTACH), EC_SUCCESS, "%d");

	/*
	 * b) Upon receipt of the Source Capabilities message from the
	 *    Provider, the Tester replies with a GoodCRC message.
	 */
	TEST_EQ(verify_tcpci_transmit(TCPCI_MSG_SOP, 0, PD_DATA_SOURCE_CAP),
		EC_SUCCESS, "%d");
	mock_set_alert(TCPC_REG_ALERT_TX_SUCCESS);

	/* Save time GoodCRC was sent */
	end_time = get_time().val;

	/*
	 * c) The Tester intentionally does not send a Request message and
	 *    waits for a Hard Reset.
	 */

	/*
	 * d) If a Hard Reset is not detected within 30 ms from the time the
	 *    last bit of the GoodCRC message EOP has been sent, the test
	 *    fails.
	 * e) If a Hard Reset is detected before 24 ms from the time the
	 *    last bit of the GoodCRC message EOP has been sent, the test
	 *    fails.
	 */
	end_time += 24 * MSEC;
	while (get_time().val < end_time) {
		TEST_NE(mock_tcpci_get_reg(TCPC_REG_TRANSMIT),
			TCPCI_MSG_TX_HARD_RESET, "%d");
		task_wait_event(1 * MSEC);
	}

	end_time += 6 * MSEC;
	while (get_time().val < end_time) {
		if (mock_tcpci_get_reg(TCPC_REG_TRANSMIT) ==
						TCPCI_MSG_TX_HARD_RESET)
			break;
		task_wait_event(1 * MSEC);
	}
	TEST_EQ(mock_tcpci_get_reg(TCPC_REG_TRANSMIT),
		TCPCI_MSG_TX_HARD_RESET, "%d");
	mock_set_alert(TCPC_REG_ALERT_TX_SUCCESS | TCPC_REG_ALERT_TX_FAILED);
	mock_tcpci_set_reg(TCPC_REG_TRANSMIT, 0);
	task_wait_event(1 * MSEC);

	return EC_SUCCESS;
}
