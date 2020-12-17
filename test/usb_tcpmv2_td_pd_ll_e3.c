/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "mock/tcpci_i2c_mock.h"
#include "task.h"
#include "tcpci.h"
#include "test_util.h"
#include "timer.h"
#include "usb_tcpmv2_compliance.h"

/*****************************************************************************
 * TD.PD.LL.E3. Soft Reset Usage
 *
 * Description:
 *	Check that the UUT will issue a Soft Reset after unsuccessful retries,
 *	and that the link can be successfully recovered after that.
 */
static int td_pd_ll_e3(enum pd_data_role data_role)
{
	int retry, retries;

	TEST_EQ(tcpci_startup(), EC_SUCCESS, "%d");

	/*
	 * a) Run PROC.PD.E1 Bring-up according to the UUT role.
	 */
	TEST_EQ(proc_pd_e1(data_role), EC_SUCCESS, "%d");

	/*
	 * Make sure we are idle. Reject everything that is pending
	 */
	for (;;) {
		if (mock_rm_if_tx(TCPC_TX_SOP,
				  PD_CTRL_DR_SWAP, 0)) {
			mock_set_alert(TCPC_REG_ALERT_TX_SUCCESS);

			partner_send_msg(PD_MSG_SOP,
					 PD_CTRL_REJECT,
					 0, 0, NULL);
			task_wait_event(1 * MSEC);
			continue;
		}
		if (mock_rm_if_tx(TCPC_TX_SOP,
				  PD_CTRL_VCONN_SWAP, 0)) {
			mock_set_alert(TCPC_REG_ALERT_TX_SUCCESS);

			partner_send_msg(PD_MSG_SOP,
					 PD_CTRL_REJECT,
					 0, 0, NULL);
			task_wait_event(1 * MSEC);
			continue;
		}
		if (mock_rm_if_tx(TCPC_TX_SOP,
				  0, PD_DATA_VENDOR_DEF)) {
			mock_set_alert(TCPC_REG_ALERT_TX_SUCCESS);

			partner_send_msg(PD_MSG_SOP,
					 PD_CTRL_NOT_SUPPORTED,
					 0, 0, NULL);
			task_wait_event(1 * MSEC);
			continue;
		}
		if (mock_rm_if_tx(TCPC_TX_SOP,
				  PD_CTRL_GET_SINK_CAP, 0)) {
			mock_set_alert(TCPC_REG_ALERT_TX_SUCCESS);

			partner_send_msg(PD_MSG_SOP,
					 PD_CTRL_REJECT,
					 0, 0, NULL);
			task_wait_event(1 * MSEC);
			continue;
		}
		break;
	}

	/*
	 * b) Send a Get_Sink_Cap message to the UUT, wait for a reply
	 *    and do not send GoodCrc for nRetryCount + 1 times
	 *    (nRetryCount equals 3 since PD 2.1).
	 */
	partner_send_msg(PD_MSG_SOP,
			 PD_CTRL_GET_SINK_CAP,
			 0, 0, NULL);

	retries = (partner_get_pd_rev() == PD_REV30) ? 2 : 3;
	for (retry = 0; retry <= retries; retry++) {
		TEST_EQ(verify_tcpci_ignore_transmit(
				TCPC_TX_SOP,
				0, PD_DATA_SINK_CAP),
			EC_SUCCESS, "%d");
	}

	/*
	 * c) Check that the UUT issues a Soft Reset.
	 */
	TEST_EQ(verify_tcpci_transmit(TCPC_TX_SOP, PD_CTRL_SOFT_RESET, 0),
		EC_SUCCESS, "%d");
	mock_set_alert(TCPC_REG_ALERT_TX_SUCCESS);
	task_wait_event(10 * MSEC);

	/*
	 * d) Handle correctly the Soft Reset procedure.
	 */
	partner_send_msg(PD_MSG_SOP, PD_CTRL_ACCEPT, 0, 0, NULL);
	task_wait_event(1 * MSEC);

	/*
	 * e) Continue the bring-up procedure and check that the link is
	 *    successfully established.
	 */
	if (partner_get_power_role() == PD_ROLE_SINK) {
		TEST_EQ(verify_tcpci_transmit(TCPC_TX_SOP,
					      0,
					      PD_DATA_SOURCE_CAP),
			EC_SUCCESS, "%d");

		mock_set_alert(TCPC_REG_ALERT_TX_SUCCESS);
		task_wait_event(10 * MSEC);

		partner_send_msg(PD_MSG_SOP, PD_DATA_REQUEST, 1, 0, &rdo);

		TEST_EQ(verify_tcpci_transmit(TCPC_TX_SOP, PD_CTRL_ACCEPT, 0),
			EC_SUCCESS, "%d");
		mock_set_alert(TCPC_REG_ALERT_TX_SUCCESS);

		TEST_EQ(verify_tcpci_transmit(TCPC_TX_SOP, PD_CTRL_PS_RDY, 0),
			EC_SUCCESS, "%d");
		mock_set_alert(TCPC_REG_ALERT_TX_SUCCESS);
		task_wait_event(5 * MSEC);

		partner_send_msg(PD_MSG_SOP, PD_CTRL_ACCEPT, 0, 0, NULL);
		task_wait_event(1 * MSEC);
	} else {
		partner_send_msg(PD_MSG_SOP, PD_DATA_SOURCE_CAP, 1, 0, &pdo);

		TEST_EQ(verify_tcpci_transmit(TCPC_TX_SOP, 0, PD_DATA_REQUEST),
			EC_SUCCESS, "%d");
		mock_set_alert(TCPC_REG_ALERT_TX_SUCCESS);

		partner_send_msg(PD_MSG_SOP, PD_CTRL_ACCEPT, 0, 0, NULL);
		task_wait_event(10 * MSEC);
		partner_send_msg(PD_MSG_SOP, PD_CTRL_PS_RDY, 0, 0, NULL);
		task_wait_event(1 * MSEC);
	}
	return EC_SUCCESS;
}
int test_td_pd_ll_e3_dfp(void)
{
	return td_pd_ll_e3(PD_ROLE_DFP);
}
int test_td_pd_ll_e3_ufp(void)
{
	return td_pd_ll_e3(PD_ROLE_UFP);
}
