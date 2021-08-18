/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "battery.h"
#include "mock/tcpci_i2c_mock.h"
#include "task.h"
#include "driver/tcpm/tcpci.h"
#include "test_util.h"
#include "timer.h"
#include "usb_tcpmv2_compliance.h"
#include "usb_tc_sm.h"

#define BUFFER_SIZE 100

#define HEADER_BYTE_OFFSET 1
#define HEADER_BYTE_CNT 2
#define SRC_CAP_EXT_NUM_BATTERY_OFFSET 22

#define EXT_MSG_CHUNKED BIT(15)
#define EXT_MSG_DATA_SIZE_1 1
#define GBSDB_FIXED_BATTERY_0 (0 << 16)


static int number_of_fixed_batteries(void)
{
	return CONFIG_NUM_FIXED_BATTERIES;
}

static int number_of_swappable_batteries(void)
{
	return 0;
}

/*****************************************************************************
 * TD.PD.SRC3.E7 Battery Status sent timely
 *
 * Description:
 *	As Consumer (UFP), the Tester verifies that the UUT replies
 *	Get_Battery_Status message with a Battery_Status message timely.
 */
int test_td_pd_src3_e7(void)
{
	int msg_len;
	uint8_t data[BUFFER_SIZE];
	uint32_t ext_msg;

	int found_index;
	struct possible_tx possible[2];

	TEST_EQ(tcpci_startup(), EC_SUCCESS, "%d");

	/*
	 * a) Run PROC.PD.E1 Bring-up according to the UUT role.
	 */
	TEST_EQ(proc_pd_e1(PD_ROLE_DFP, INITIAL_AND_ALREADY_ATTACHED),
		EC_SUCCESS, "%d");

	/*
	 * b) The Tester waits until it can start an AMS (Run PROC.PD.E3) and
	 *    sends a Get_Source_Cap_Extended message to the UUT.
	 */
	TEST_EQ(proc_pd_e3(), EC_SUCCESS, "%d");

	partner_send_msg(TCPC_TX_SOP, PD_CTRL_GET_SOURCE_CAP_EXT, 0, 0, NULL);

	/*
	 * c) If a Not_Supported message is received, and Num_Fixed_Batteries
	 *    and Num_Swappable_Battery_Slots in the VIF are 0, the test
	 *    passes and stops here.
	 */
	possible[0].tx_type = TCPC_TX_SOP;
	possible[0].ctrl_msg = PD_CTRL_NOT_SUPPORTED;
	possible[0].data_msg = 0;

	possible[1].tx_type = TCPC_TX_SOP;
	possible[1].ctrl_msg = 0;
	possible[1].data_msg = PD_EXT_SOURCE_CAP;

	TEST_EQ(verify_tcpci_possible_tx(possible,
					 2,
					 &found_index,
					 data,
					 sizeof(data),
					 &msg_len,
					 0),
		EC_SUCCESS, "%d");
	if (found_index == 0) {
		mock_set_alert(TCPC_REG_ALERT_TX_SUCCESS);
		task_wait_event(10 * MSEC);

		if (number_of_fixed_batteries() == 0 &&
		    number_of_swappable_batteries() == 0)
			return EC_SUCCESS;
	}
	/*
	 * d) If the Number of Batteries/Battery Slots field in the returned
	 *    Source_Capabilities_Extended message is 0, the test passes and
	 *    stops here.
	 */
	else {
		TEST_EQ(found_index, 1, "%d");
		mock_set_alert(TCPC_REG_ALERT_TX_SUCCESS);
		task_wait_event(10 * MSEC);

		if (data[HEADER_BYTE_OFFSET +
			 HEADER_BYTE_CNT +
			 SRC_CAP_EXT_NUM_BATTERY_OFFSET] == 0)
			return EC_SUCCESS;
	}

	/*
	 * e) The Tester waits until it can start an AMS (Run PROC.PD.E3) and
	 *    sends a Get_Battery_Status message to the UUT
	 */
	ext_msg = EXT_MSG_CHUNKED |
		  EXT_MSG_DATA_SIZE_1 |
		  GBSDB_FIXED_BATTERY_0;
	partner_send_msg(TCPC_TX_SOP, PD_EXT_GET_BATTERY_STATUS, 1, 1,
			&ext_msg);

	/*
	 * f) If a Battery_Status message is not received within
	 *    tReceiverResponse max, the test fails. This delay is measured
	 *    from the time the last bit of Get_Battery_Status message EOP has
	 *    been transmitted to the time the first bit of the Battery_Status
	 *    message preamble has been received.
	 */
	TEST_EQ(verify_tcpci_tx_timeout(TCPC_TX_SOP,
					0,
					PD_DATA_BATTERY_STATUS,
					(15 * MSEC)),
		EC_SUCCESS, "%d");
	mock_set_alert(TCPC_REG_ALERT_TX_SUCCESS);
	task_wait_event(10 * MSEC);

	return EC_SUCCESS;
}
