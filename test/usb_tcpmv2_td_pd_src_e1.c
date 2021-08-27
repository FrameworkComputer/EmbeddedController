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
 * TD.PD.SRC.E1 Source Capabilities sent timely
 *
 * Description:
 *	As Consumer (UFP), the Tester verifies a Source Capabilities message
 *	from the Provider (DFP, UUT) is received timely
 */
int test_td_pd_src_e1(void)
{
	partner_set_pd_rev(PD_REV20);

	TEST_EQ(tcpci_startup(), EC_SUCCESS, "%d");

	/*
	 * a) Run PROC.PD.E1 Bring-up according to the UUT role.
	 *
	 * NOTE: Calling PROC.PD.E1 with INITIAL_ATTACH will stop just before
	 * the PD_DATA_SOURCE_CAP is verified.  We need to stop the process
	 * there to use the timeout verify.
	 */
	TEST_EQ(proc_pd_e1(PD_ROLE_DFP, INITIAL_ATTACH), EC_SUCCESS, "%d");

	/*
	 * b) The test fails if the first bit of a Source Capabilities message
	 *    is not received from the Provider within 250 ms (tFirstSourceCap
	 *    max) after VBus present.
	 */
	TEST_EQ(verify_tcpci_tx_timeout(TCPCI_MSG_SOP, 0,
					PD_DATA_SOURCE_CAP,
					250 * MSEC),
		EC_SUCCESS, "%d");

	return EC_SUCCESS;
}
