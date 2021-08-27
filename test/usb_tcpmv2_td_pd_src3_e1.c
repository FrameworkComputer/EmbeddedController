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

#define BUFFER_SIZE 100

#define HEADER_BYTE_OFFSET 1
#define HEADER_BYTE_CNT 2
#define PDO_BYTE_CNT 4

enum pd_revision {
	REVISION_1 = 0,
	REVISION_2 = 1,
	REVISION_3 = 2,
	REVISION_RESERVED = 3
};

/*****************************************************************************
 * TD.PD.SRC3.E1 Source Capabilities Fields Checks
 *
 * Description:
 *	As Consumer (UFP), the Tester waits for a Source_Capabilities message
 *	from the Provider (DFP,UUT) and verifies correct field values.
 */
int test_td_pd_src3_e1(void)
{
	int i;
	int msg_len;
	uint8_t data[BUFFER_SIZE];
	uint16_t header;
	uint16_t pd_cnt;
	uint32_t pdo;
	uint32_t type;
	uint32_t last_fixed_voltage = 0;
	uint32_t last_battery_voltage = 0;
	uint32_t last_variable_voltage = 0;
	uint32_t last_programmable_voltage = 0;

	partner_set_pd_rev(PD_REV30);

	TEST_EQ(tcpci_startup(), EC_SUCCESS, "%d");

	/*
	 * a) Run PROC.PD.E1 Bring-up For DFP UUT steps a and b.
	 *
	 * NOTE: Calling PROC.PD.E1 with INITIAL_ATTACH will stop just before
	 * the PD_DATA_SOURCE_CAP is verified.  We need to stop the process
	 * there to gather the actual message data.
	 */
	TEST_EQ(proc_pd_e1(PD_ROLE_DFP, INITIAL_ATTACH), EC_SUCCESS, "%d");

	/*
	 * b) Upon receipt of the Source_Capabilities message from the
	 *    Provider, the Tester verifies:
	 *    1. Number of Data Objects field equals the number of Src_PDOs in
	 *       the message and is not 000b.
	 *    2. Port Power Role field = 1b (Source)
	 *    3. Specification Revision field = 10b (Rev 3.0)
	 *    4. Port Data Role field = 1b (DFP)
	 *    5. Message Type field = 00001b (Source Capabilities)
	 *    6. Extended field = 0b
	 */
	TEST_EQ(verify_tcpci_tx_with_data(TCPCI_MSG_SOP,
					  PD_DATA_SOURCE_CAP,
					  data,
					  sizeof(data),
					  &msg_len,
					  0),
		EC_SUCCESS, "%d");
	TEST_GE(msg_len, HEADER_BYTE_CNT, "%d");

	header = UINT16_FROM_BYTE_ARRAY_LE(data, HEADER_BYTE_OFFSET);
	pd_cnt = PD_HEADER_CNT(header);
	TEST_NE(pd_cnt, 0, "%d");
	TEST_EQ(msg_len, HEADER_BYTE_OFFSET +
			 HEADER_BYTE_CNT +
			 (pd_cnt * PDO_BYTE_CNT), "%d");
	TEST_EQ(PD_HEADER_PROLE(header), PD_ROLE_SOURCE, "%d");
	TEST_EQ(PD_HEADER_REV(header), REVISION_3, "%d");
	TEST_EQ(PD_HEADER_DROLE(header), PD_ROLE_DFP, "%d");
	TEST_EQ(PD_HEADER_TYPE(header), PD_DATA_SOURCE_CAP, "%d");
	TEST_EQ(PD_HEADER_EXT(header), 0, "%d");

	/*
	 * c) For the first PDO, the Tester verifies:
	 *    1. Bits 31..30 (PDO type) are 00b (Fixed Supply).
	 *    2. Voltage field = 100 (5 V)
	 *    3. Bits 23..22 = 000b (Reserved)
	 */
	pdo = UINT32_FROM_BYTE_ARRAY_LE(data, HEADER_BYTE_OFFSET +
					      HEADER_BYTE_CNT);

	type = pdo & PDO_TYPE_MASK;
	TEST_EQ(type, PDO_TYPE_FIXED, "%d");

	last_fixed_voltage = PDO_FIXED_VOLTAGE(pdo);
	TEST_EQ(last_fixed_voltage, 5000, "%d");
	TEST_EQ(pdo & GENMASK(23, 22), 0, "%d");

	/*
	 * d) For the other PDOs (if any), the Tester verifies:
	 *    1. If Bits 31..30 are 00b
	 *       -- Bits 29..22 are set to 0.
	 *       NOTE: Bit 29 is Dual Role Power and looks correct for this
	 *             to not be 0.  Bit 25 is Dual Role Data and looks
	 *             correct for this to not be 0.
	 *    2. If Bits 31..30 are 11b
	 *       --  Bits 29..28 are 00b (Programmable Power Supply)
	 *       --  Bits 26..25 are 00b (Reserved)
	 *       --  Bit 16 is 0b (Reserved)
	 *       --  Bit 7 is 0b (Reserved)
	 *    3. PDOs are in the order of Fixed Supply Objects (if present),
	 *       Battery Supply Objects (if present), Variable Supply Objects
	 *       (if present) and then Programmable Power Supply Objects (if
	 *       present).
	 *    4. Fixed Supply Objects (if present) are in voltage order; lowest
	 *       to highest.
	 *    5. Battery Supply Objects (if present) are in Minimum Voltage
	 *       order; lowest to highest.
	 *    6. Variable Supply Objects (if present) are in Minimum Voltage
	 *       order; lowest to highest.
	 *    7. Programmable Power Supply Objects (if present) are in Maximum
	 *       Voltage order; lowest to highest.
	 */
	for (i = 1; i < pd_cnt; ++i) {
		int offset;
		uint32_t voltage;

		offset = HEADER_BYTE_OFFSET +
			 HEADER_BYTE_CNT +
			 (i * PDO_BYTE_CNT);
		pdo = UINT32_FROM_BYTE_ARRAY_LE(data, offset);

		type = pdo & PDO_TYPE_MASK;
		if (type == PDO_TYPE_FIXED) {
			TEST_EQ(pdo & (GENMASK(28, 26)|GENMASK(24, 22)),
				0, "%d");
			TEST_EQ(last_battery_voltage, 0, "%d");
			TEST_EQ(last_variable_voltage, 0, "%d");
			TEST_EQ(last_programmable_voltage, 0, "%d");
			voltage = PDO_FIXED_VOLTAGE(pdo);
			TEST_GE(voltage, last_fixed_voltage, "%d");
			last_fixed_voltage = voltage;
		} else if (type == PDO_TYPE_BATTERY) {
			TEST_EQ(last_variable_voltage, 0, "%d");
			TEST_EQ(last_programmable_voltage, 0, "%d");
			voltage = PDO_BATT_MIN_VOLTAGE(pdo);
			TEST_GE(voltage, last_battery_voltage, "%d");
			last_battery_voltage = voltage;
		} else if (type == PDO_TYPE_VARIABLE) {
			TEST_EQ(last_programmable_voltage, 0, "%d");
			voltage = PDO_VAR_MIN_VOLTAGE(pdo);
			TEST_GE(voltage, last_variable_voltage, "%d");
			last_variable_voltage = voltage;
		} else {
			TEST_EQ(pdo & GENMASK(29, 28), 0, "%d");
			TEST_EQ(pdo & GENMASK(26, 25), 0, "%d");
			TEST_EQ(pdo & BIT(16), 0, "%d");
			TEST_EQ(pdo & BIT(7), 0, "%d");
			voltage = PDO_AUG_MAX_VOLTAGE(pdo);
			TEST_GE(voltage, last_programmable_voltage, "%d");
			last_programmable_voltage = voltage;
		}
	}

	return EC_SUCCESS;
}
