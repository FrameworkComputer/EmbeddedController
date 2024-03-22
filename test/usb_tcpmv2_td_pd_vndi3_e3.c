/* Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "mock/tcpci_i2c_mock.h"
#include "task.h"
#include "tcpm/tcpci.h"
#include "test_util.h"
#include "timer.h"
#include "usb_tc_sm.h"
#include "usb_tcpmv2_compliance.h"

uint32_t vdo = VDO(USB_SID_PD, 1,
		   VDO_SVDM_VERS_MAJOR(SVDM_VER_2_0) | CMD_DISCOVER_IDENT);

/*****************************************************************************
 * TD.PD.VNDI3.E3.VDM Identity
 *
 * Description:
 *	This test verifies that the VDM Information is as specified in the
 *	vendor-supplied information.
 */
static int td_pd_vndi3_e3(enum pd_data_role data_role)
{
	partner_set_pd_rev(PD_REV30);

	TEST_EQ(tcpci_startup(), EC_SUCCESS, "%d");

	/*
	 * a) Run PROC.PD.E1 Bring-up according to the UUT role.
	 */
	TEST_EQ(proc_pd_e1(data_role, INITIAL_AND_ALREADY_ATTACHED), EC_SUCCESS,
		"%d");

	/*
	 * Make sure we are idle. Reject everything that is pending
	 */
	TEST_EQ(handle_attach_expected_msgs(data_role), EC_SUCCESS, "%d");

	/*
	 * b) Tester executes a Discover Identity exchange
	 */
	partner_send_msg(TCPCI_MSG_SOP, PD_DATA_VENDOR_DEF, 1, 0, &vdo);

	/*
	 * c) If the UUT is not a cable and if Responds_To_Discov_SOP is set to
	 * No, the tester checks that the UUT replies Not_Supported.  The test
	 * stops here in this case.
	 */
	TEST_EQ(verify_tcpci_transmit(TCPCI_MSG_SOP, PD_CTRL_NOT_SUPPORTED, 0),
		EC_SUCCESS, "%d");
	mock_set_alert(TCPC_REG_ALERT_TX_SUCCESS);

	/*
	 * TODO: Items d)-i) could be verified if the unit tests are configured
	 * to reply to Identity messages.
	 *
	 * d) For Cables, the Tester checks the consistency of
	 * Specification_Revision
	 *
	 * e) For all devices, the Tester checks in the ID Header consistency
	 * of:
	 *	1. Product_Type(UFP)
	 *	2. Product Type(Cable Plug)
	 *	3. Product Type (DFP)
	 *	4. USB_VID(_SOP)
	 *	5. Modal_Operation_Supported(_SOP)
	 *	6. Data_Capable_as_USB_Host(_SOP)
	 *	7. Data_Capable_as_USB_Device(_SOP)
	 *
	 * f) For all devices, the Tester checks in the Cert Stat VDO
	 * consistency of:
	 *	1. XID(_SOP)
	 *
	 * g) For all devices, the Tester checks in the Product VDO consistency
	 * of:
	 *	1. PID(_SOP)
	 *	2. bcdDevice(_SOP)
	 *
	 * h) For Cables, the Tester checks in the Cable VDO consistency of:
	 *	1. Cable_HW_Vers
	 *	2. Cable_FW_Vers
	 *	3. Type_C_to_Type_C_Capt_Vdm_V2
	 *	4. Cable_Latency
	 *	5. Cable_Termination_Type
	 *	6. Max_VBUS_Voltage_Vdm_V2
	 *	7. Cable_VBUS_Current
	 *	8. VBUS_through_cable
	 *	9. Cable_SOP''_controller
	 *	10. Cable_Superspeed_Support
	 *
	 * i) For Alt Mode Adapters, the Tester checks in the AMA VDO
	 * consistency of:
	 *	1. AMA_HW_Vers
	 *	2. AMA_FW_Vers
	 *	3. AMA_VCONN_power
	 *	4. AMA_VCONN_reqd
	 *	5. AMA_VBUS_reqd
	 *	6. AMA_Superspeed_Support
	 */
	return EC_SUCCESS;
}
int test_td_pd_vndi3_e3_dfp(void)
{
	return td_pd_vndi3_e3(PD_ROLE_DFP);
}
int test_td_pd_vndi3_e3_ufp(void)
{
	return td_pd_vndi3_e3(PD_ROLE_UFP);
}
