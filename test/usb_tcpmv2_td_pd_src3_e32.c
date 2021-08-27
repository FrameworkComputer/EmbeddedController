/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "mock/tcpci_i2c_mock.h"
#include "task.h"
#include "tcpm/tcpci.h"
#include "test_util.h"
#include "timer.h"
#include "usb_tcpmv2_compliance.h"

#define PD_T_CHUNK_RECEIVER_REQUEST_MAX		(15 * MSEC)
#define PD_T_CHUNK_SENDER_RSP_MAX		(30 * MSEC)
#define PD_T_CHUNKING_NOT_SUPPORTED_MIN		(40 * MSEC)
#define PD_T_CHUNKING_NOT_SUPPORTED_MAX		(50 * MSEC)

static void setup_chunk_msg(int chunk, char *data)
{
	int i;
	int base_msg_byte = chunk * PD_MAX_EXTENDED_MSG_CHUNK_LEN;

	*(uint16_t *)data = PD_EXT_HEADER(chunk, 0,
					  PD_MAX_EXTENDED_MSG_LEN);

	for (i = 0; i < PD_MAX_EXTENDED_MSG_CHUNK_LEN; ++i) {
		int val = (i + base_msg_byte) % 256;

		data[i + sizeof(uint16_t)] = val;
	}
}

/*****************************************************************************
 * TD.PD.SRC3.E32 ChunkSenderResponseTimer Timeout
 *
 * Description:
 *	As Consumer (UFP), the Tester verifies that the UUT recovers correctly
 *	after the Tester stops sending chunked messages in the middle.
 */
int test_td_pd_src3_e32(void)
{
	int chunk = 0;
	int msg_len;
	uint32_t header;
	char data[PD_MAX_EXTENDED_MSG_CHUNK_LEN + sizeof(uint16_t)];

	int found_index;
	struct possible_tx possible[2];

	uint64_t start_time;

	/*
	 * a) Run PROC.PD.E1 Bring-up according to the UUT role. The Tester
	 * sets Unchunked Extended Messages Supported set to 0 in Request
	 * message during this process.
	 * b) The Tester waits until it can start an AMS (Run PROC.PD.E3)
	 */
	TEST_EQ(tcpci_startup(), EC_SUCCESS, "%d");
	TEST_EQ(proc_pd_e1(PD_ROLE_DFP, INITIAL_AND_ALREADY_ATTACHED),
		EC_SUCCESS, "%d");
	TEST_EQ(proc_pd_e3(), EC_SUCCESS, "%d");

	/*
	 * ...and sends the first chunk of a chunked extended message
	 * to the UUT, with Data Size set to 260 and Message Type set
	 * to 11111b.  Bytes 0 to 259 of data block contain
	 * incrementing values (mod 256) starting at 0x00.
	 */
	setup_chunk_msg(0, data);
	partner_send_msg(TCPCI_MSG_SOP, 0x1F, 7, 1, (uint32_t *)data);
	start_time = get_time().val;

	/*
	 * c) If a message is not received within tChunkingNotSupported
	 * max (50ms), this test fails. The delay is messaged from the
	 * time the last bit of the EOP of the chunk has been
	 * transmitted until the first bit of the response Message
	 * Preamble has been received.
	 */
	possible[0].tx_type = TCPCI_MSG_SOP;
	possible[0].ctrl_msg = PD_CTRL_NOT_SUPPORTED;
	possible[0].data_msg = 0;

	possible[1].tx_type = TCPCI_MSG_SOP;
	possible[1].ctrl_msg = 0;
	possible[1].data_msg = 0x1F;

	TEST_EQ(verify_tcpci_possible_tx(possible,
					 2,
					 &found_index,
					 data,
					 sizeof(data),
					 &msg_len,
					 PD_T_CHUNKING_NOT_SUPPORTED_MAX),
		EC_SUCCESS, "%d");
	mock_set_alert(TCPC_REG_ALERT_TX_SUCCESS);

	/*
	 * d) If the received message is Not_Supported, the Tester verifies
	 * the message is received after tChunkingNotSupported min (40ms)
	 * and stops here.
	 */
	if (found_index == 0) {
		TEST_ASSERT((get_time().val - start_time) >=
			    PD_T_CHUNKING_NOT_SUPPORTED_MIN);
		return EC_SUCCESS;
	}
	TEST_EQ(found_index, 1, "%d");

	/*
	 * e) If the message is not received within
	 * tChunkReceiverRequest max (15ms), the test fails.
	 */
	TEST_ASSERT((get_time().val - start_time) <=
					PD_T_CHUNK_RECEIVER_REQUEST_MAX);

	while (chunk < 4) {
		int next_chunk;

		/*
		 * f) Upon receipt of the message from the UUT to request for
		 * the next chunk, the Tester sends the requested chunk to the
		 * UUT.
		 */
		header = *(uint16_t *)&data[3];
		next_chunk = PD_EXT_HEADER_CHUNK_NUM(header);
		TEST_EQ(chunk + 1, next_chunk, "%d");
		chunk = next_chunk;

		setup_chunk_msg(chunk, data);
		partner_send_msg(TCPCI_MSG_SOP, 0x1F, 7, 1, (uint32_t *)data);

		TEST_EQ(verify_tcpci_tx_with_data(TCPCI_MSG_SOP,
					0x1F,
					data,
					sizeof(data),
					&msg_len,
					PD_T_CHUNK_RECEIVER_REQUEST_MAX),
			EC_SUCCESS, "%d");
		mock_set_alert(TCPC_REG_ALERT_TX_SUCCESS);

		/*
		 * g) Repeat f) until the Tester has finished sending 4 chunks
		 * and intentionally does not send the 5th chunk to the UUT.
		 */
	}

	/*
	 * h) The Tester waits for tChunkSenderResponse max + 5 ms, waits until
	 * it can start an AMS (Run PROC.PD.E3) and sends the first chunk to
	 * the UUT.
	 */
	task_wait_event(PD_T_CHUNK_SENDER_RSP_MAX + (5 * MSEC));

	setup_chunk_msg(0, data);
	partner_send_msg(TCPCI_MSG_SOP, 0x1F, 7, 1, (uint32_t *)data);

	/*
	 * i) If a message is not received within tChunkReceiverRequest max,
	 * the test fails.
	 */
	TEST_EQ(verify_tcpci_tx_with_data(TCPCI_MSG_SOP,
				0x1F,
				data,
				sizeof(data),
				&msg_len,
				PD_T_CHUNK_RECEIVER_REQUEST_MAX),
		EC_SUCCESS, "%d");
	mock_set_alert(TCPC_REG_ALERT_TX_SUCCESS);

	/*
	 * j) Upon receipt of the message, the Tester verifies the following:
	 *	1. For Message Header
	 *	  Extended = 1
	 *	   Number of Data Objects = 1
	 *	  Port Power Role field = 1b (Source)
	 *	  Port Data Role field = 1b (DFP)
	 *	  Specification Revision = 10b (Rev 3.0)
	 *	  Message Type = 11111b
	 */
	TEST_EQ(msg_len, 7, "%d");
	header = *(uint32_t *)&data[1];
	TEST_EQ(PD_HEADER_EXT(header), 1, "%d");
	TEST_EQ(PD_HEADER_CNT(header), 1, "%d");
	TEST_EQ(PD_HEADER_PROLE(header), 1, "%d");
	TEST_EQ(PD_HEADER_DROLE(header), 1, "%d");
	TEST_EQ(PD_HEADER_REV(header), PD_REV30, "%d");
	TEST_EQ(PD_HEADER_TYPE(header), 0x1F, "%d");

	/*
	 *	2. For Extended Message Header
	 *	  Chunked = 1
	 *	  Chunk Number = 1
	 *	  Request Chunk = 1
	 *	  Bit 9 = 0 (Reserved)
	 *	  Data Size = 0
	 */
	header = *(uint16_t *)&data[3];
	TEST_EQ(PD_EXT_HEADER_CHUNKED(header), 1, "%d");
	TEST_EQ(PD_EXT_HEADER_CHUNK_NUM(header), 1, "%d");
	TEST_EQ(PD_EXT_HEADER_REQ_CHUNK(header), 1, "%d");
	TEST_EQ(header & BIT(9), 0, "%d");
	TEST_EQ(PD_EXT_HEADER_DATA_SIZE(header), 0, "%d");

	/*
	 *	3. The total number of data bytes is consistent with the
	 *	   Number of Data Objects field
	 */
	header = *(uint32_t *)&data[1];
	TEST_EQ(msg_len - 3,
		PD_HEADER_CNT(header) * 4,
		"%d");

	/*
	 *	4. The last 2 bytes of the Data Object are 0
	 */
	TEST_EQ(data[5], 0, "%d");
	TEST_EQ(data[6], 0, "%d");

	return EC_SUCCESS;
}
