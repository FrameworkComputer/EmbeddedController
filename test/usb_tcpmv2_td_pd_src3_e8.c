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

#define EXT_MSG_CHUNKED BIT(15)
#define EXT_MSG_DATA_SIZE_1 1

#define HEADER_BYTE_OFFSET 1
#define HEADER_NUM_BYTES 2

#define SCEDB_NUM_BATTERY_OFFSET 22
#define SCEDB_NUM_BYTES 24

#define BSDO_NUM_BYTES 4
#define BSDO_INV_BATTERY_REF(bsdo)	(((bsdo) >> 8) & 1)
#define BSDO_BATTERY_PRESENT(bsdo)	(((bsdo) >> 9) & 1)
#define BSDO_BATTERY_CHRG_STS(bsdo)	(((bsdo) >> 10) & 3)
#define BSDO_BATTERY_INFO(bsdo)		(((bsdo) >> 8) & 0xFF)

static int number_of_fixed_batteries(void)
{
	return CONFIG_NUM_FIXED_BATTERIES;
}

static int number_of_swappable_batteries(void)
{
	return 0;
}

/*****************************************************************************
 * TD.PD.SRC3.E8 Battery Status Field Checks
 *
 * Description:
 *	As Consumer (UFP), the Tester sends a Get_Battery_Status message to
 *	the UUT, verifies the UUT respond with a Battery_Status or
 *	Not_Supported message. If a Battery_Status message is received, the
 *	Tester verifies correct field values.
 */
int test_td_pd_src3_e8(void)
{
	int msg_len;
	uint8_t data[BUFFER_SIZE];
	uint32_t ext_msg;
	int num_fixed_batteries;
	int num_swappable_battery_slots;

	int ref;
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
	 * c) If a Source_Capabilities_Extended message is received, the
	 *    Tester record the Number of Batteries/Battery Slots field.
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
	if (found_index == 1) {
		mock_set_alert(TCPC_REG_ALERT_TX_SUCCESS);
		task_wait_event(10 * MSEC);

		TEST_EQ(msg_len, HEADER_BYTE_OFFSET +
				 HEADER_NUM_BYTES +
				 SCEDB_NUM_BYTES,
			"%d");

		num_fixed_batteries =
				data[HEADER_BYTE_OFFSET +
				     HEADER_NUM_BYTES +
				     SCEDB_NUM_BATTERY_OFFSET] &
				0x0F;
		num_swappable_battery_slots =
				(data[HEADER_BYTE_OFFSET +
				      HEADER_NUM_BYTES +
				      SCEDB_NUM_BATTERY_OFFSET] >> 4) &
				0x0F;
	}
	/*
	 *    If a Not_Supported message is received, the Tester reads the
	 *    Number of Batteries/Battery Slots field (combine
	 *    Num_Fixed_Batteries and Num_Swappable_Battery_Slots) from the
	 *    VIF.
	 */
	else {
		TEST_EQ(found_index, 0, "%d");
		mock_set_alert(TCPC_REG_ALERT_TX_SUCCESS);
		task_wait_event(10 * MSEC);

		num_fixed_batteries = number_of_fixed_batteries();
		num_swappable_battery_slots = number_of_swappable_batteries();
	}

	/*
	 * d) The Tester waits until it can start an AMS (Run PROC.PD.E3) and
	 *    sends a Get_Battery_Status message to the UUT, with Battery
	 *    Status Ref set to 0 (step g includes doing d-f with Battery
	 *    Status Ref set to 1 - 7).
	 */
	for (ref = 0; ref <= 7; ++ref) {
		uint16_t header;
		uint32_t bsdo;

		ext_msg = EXT_MSG_CHUNKED | EXT_MSG_DATA_SIZE_1 |
			  (ref << 16);
		partner_send_msg(TCPC_TX_SOP, PD_EXT_GET_BATTERY_STATUS, 1, 1,
				 &ext_msg);

		/*
		 * e) If a Battery_Status message is received, the Tester
		 *    verifies:
		 */
		TEST_EQ(verify_tcpci_tx_with_data(TCPC_TX_SOP,
						  PD_DATA_BATTERY_STATUS,
						  data,
						  sizeof(data),
						  &msg_len,
						  0),
			EC_SUCCESS, "%d");
		mock_set_alert(TCPC_REG_ALERT_TX_SUCCESS);
		task_wait_event(10 * MSEC);
		TEST_EQ(msg_len, HEADER_BYTE_OFFSET +
				 HEADER_NUM_BYTES +
				 BSDO_NUM_BYTES,
			"%d");

		/*
		 *    1. Number of Data Objects field = 001b
		 */
		header = UINT16_FROM_BYTE_ARRAY_LE(data, HEADER_BYTE_OFFSET);
		TEST_EQ(PD_HEADER_CNT(header), 1, "%d");

		/*
		 *    2. Port Power Role field = 1b (Source)
		 */
		TEST_EQ(PD_HEADER_PROLE(header), 1, "%d");

		/*
		 *    3. Specification Revision field = 10b (Rev 3.0)
		 */
		TEST_EQ(PD_HEADER_REV(header), PD_REV30, "%d");

		/*
		 *    4. Port Data Role field = 1b (DFP)
		 */
		TEST_EQ(PD_HEADER_DROLE(header), PD_ROLE_DFP, "%d");

		/*
		 *    5. Extended = 0b
		 */
		TEST_EQ(PD_HEADER_EXT(header), 0, "%d");

		/*
		 *    6. Invalid Battery Reference field (Bit 0) of the
		 *       Battery Info field in the BSDO matches with the
		 *       recorded Number of Batteries/Battery Slots field
		 *    7. If Battery Status Ref referred to a fixed battery
		 *       and Invalid Battery Reference field is 0, the Battery
		 *       is present field (Bit 1) shall be 1
		 *    8. If Invalid Battery Reference field is 1, Battery is
		 *       present field shall be 0
		 */
		bsdo = UINT32_FROM_BYTE_ARRAY_LE(data, HEADER_BYTE_OFFSET +
						       HEADER_NUM_BYTES);

		/* FIXED BATTERY */
		if (ref < 4) {
			if (ref < num_fixed_batteries) {
				TEST_EQ(BSDO_INV_BATTERY_REF(bsdo),
					0, "%d");
				TEST_EQ(BSDO_BATTERY_PRESENT(bsdo),
					1, "%d");
			} else {
				TEST_EQ(BSDO_INV_BATTERY_REF(bsdo),
					1, "%d");
				TEST_EQ(BSDO_BATTERY_PRESENT(bsdo),
					0, "%d");
			}
		}
		/* BATTERY SLOT */
		else {
			if ((ref - 4) < num_swappable_battery_slots) {
				TEST_EQ(BSDO_INV_BATTERY_REF(bsdo),
					0, "%d");
			} else {
				TEST_EQ(BSDO_INV_BATTERY_REF(bsdo),
					1, "%d");
				TEST_EQ(BSDO_BATTERY_PRESENT(bsdo),
					0, "%d");
			}
		}

		/*
		 *    9. If Battery is present, Battery charging status
		 *       (Bits 3..2) of Battery Info field is not 11b
		 *   10. If Battery is not present, Bits 3..2 of Battery Info
		 *       field is 00b
		 *   11. Bits 7..4 of Battery Info field are 0
		 *   12. Bits 7..0 of the BSDO are 0
		 */
		if (BSDO_BATTERY_PRESENT(bsdo))
			TEST_NE(BSDO_BATTERY_CHRG_STS(bsdo), 3, "%d");
		else
			TEST_EQ(BSDO_BATTERY_CHRG_STS(bsdo), 0, "%d");

		TEST_EQ(BSDO_BATTERY_INFO(bsdo) & GENMASK(7, 4), 0, "%d");
		TEST_EQ(bsdo & GENMASK(7, 0), 0, "%d");
	}

	return EC_SUCCESS;
}
