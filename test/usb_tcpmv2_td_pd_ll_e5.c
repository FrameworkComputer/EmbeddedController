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
 * TD.PD.LL.E5. Soft Reset
 *
 * Description:
 *	Check that the UUT will correctly complete the Soft Reset procedure.
 */
static int td_pd_ll_e5(enum pd_data_role data_role)
{
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
	 * b) Initiate a Soft Reset and check that the procedure is completed
	 *    successfully.
	 */
	partner_send_msg(TCPC_TX_SOP, PD_CTRL_SOFT_RESET, 0, 0, NULL);

	TEST_EQ(verify_tcpci_transmit(TCPC_TX_SOP, PD_CTRL_ACCEPT, 0),
		EC_SUCCESS, "%d");
	mock_set_alert(TCPC_REG_ALERT_TX_SUCCESS);

	TEST_EQ(proc_pd_e1(data_role, ALREADY_ATTACHED), EC_SUCCESS, "%d");

	return EC_SUCCESS;
}
int test_td_pd_ll_e5_dfp(void)
{
	return td_pd_ll_e5(PD_ROLE_DFP);
}
int test_td_pd_ll_e5_ufp(void)
{
	return td_pd_ll_e5(PD_ROLE_UFP);
}
