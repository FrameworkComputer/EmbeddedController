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
 * TD.PD.LL.E4. Hard Reset Usage
 *
 * Description:
 *	Check that the UUT will issue a Soft Reset after unsuccessful retries,
 *	and that the link can be successfully recovered after that.
 *	Check that the UUT will issue a Hard Reset if the Soft Reset fails,
 *	and that the link can be successfully recovered after that.
 */
static int td_pd_ll_e4(enum pd_data_role data_role)
{
	int retries;

	partner_set_pd_rev(PD_REV20);

	TEST_EQ(tcpci_startup(), EC_SUCCESS, "%d");

	/*
	 * a) Run PROC.PD.E1 Bring-up according to the UUT role.
	 */
	TEST_EQ(proc_pd_e1(data_role, INITIAL_AND_ALREADY_ATTACHED),
		EC_SUCCESS, "%d");

	/*
	 * Make sure we are idle. Reject everything that is pending
	 */
	TEST_EQ(handle_attach_expected_msgs(data_role), EC_SUCCESS, "%d");

	/*
	 * b) Send a Get_Sink_Cap message to the UUT, wait for a reply
	 *    and do not send GoodCrc for nRetryCount + 1 times
	 *    (nRetryCount equals 3 since PD 2.1).
	 */
	partner_send_msg(TCPC_TX_SOP,
			 PD_CTRL_GET_SINK_CAP,
			 0, 0, NULL);

	retries = 3;
	TEST_EQ(verify_tcpci_tx_retry_count(TCPC_TX_SOP, 0, PD_DATA_SINK_CAP,
			retries),
		EC_SUCCESS, "%d");
	mock_set_alert(TCPC_REG_ALERT_TX_FAILED);

	/*
	 * c) Wait the nRetryCount + 1 (four) Soft Resets from the UUT and do
	 *    not reply GoodCrc.
	 */
	retries = 3;
	TEST_EQ(verify_tcpci_tx_retry_count(TCPC_TX_SOP, PD_CTRL_SOFT_RESET, 0,
			retries),
		EC_SUCCESS, "%d");
	mock_set_alert(TCPC_REG_ALERT_TX_FAILED);
	task_wait_event(1 * MSEC);

	/*
	 * d) Check that the UUT issues a Hard Reset.
	 */
	TEST_EQ(mock_tcpci_get_reg(TCPC_REG_TRANSMIT),
		TCPC_TX_HARD_RESET, "%d");
	mock_set_alert(TCPC_REG_ALERT_TX_SUCCESS | TCPC_REG_ALERT_TX_FAILED);
	mock_tcpci_set_reg(TCPC_REG_TRANSMIT, 0);
	task_wait_event(1 * MSEC);

	/*
	 * e) Do the bring-up procedure for Link tests and check that the link
	 *    is successfully established.
	 */
	TEST_EQ(proc_pd_e1(data_role, ALREADY_ATTACHED), EC_SUCCESS, "%d");

	return EC_SUCCESS;
}
int test_td_pd_ll_e4_dfp(void)
{
	return td_pd_ll_e4(PD_ROLE_DFP);
}
int test_td_pd_ll_e4_ufp(void)
{
	return td_pd_ll_e4(PD_ROLE_UFP);
}
