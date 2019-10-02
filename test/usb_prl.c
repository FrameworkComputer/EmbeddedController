/* Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Test USB Protocol Layer module.
 */
#include "common.h"
#include "crc.h"
#include "task.h"
#include "test_util.h"
#include "timer.h"
#include "tcpm.h"
#include "usb_emsg.h"
#include "usb_pe_sm.h"
#include "usb_pd.h"
#include "usb_pd_test_util.h"
#include "usb_prl_sm.h"
#include "usb_sm_checks.h"
#include "util.h"

#define PORT0 0
#define PORT1 1

/*
 * These enum definitions are declared in usb_prl_sm and are private to that
 * file. If those definitions are re-ordered, then we need to update these
 * definitions (should be very rare).
 */
enum usb_prl_tx_state {
	PRL_TX_PHY_LAYER_RESET,
	PRL_TX_WAIT_FOR_MESSAGE_REQUEST,
	PRL_TX_LAYER_RESET_FOR_TRANSMIT,
	PRL_TX_WAIT_FOR_PHY_RESPONSE,
	PRL_TX_SRC_SOURCE_TX,
	PRL_TX_SNK_START_AMS,
	PRL_TX_SRC_PENDING,
	PRL_TX_SNK_PENDING,
	PRL_TX_DISCARD_MESSAGE,
};

enum usb_prl_hr_state {
	PRL_HR_WAIT_FOR_REQUEST,
	PRL_HR_RESET_LAYER,
	PRL_HR_WAIT_FOR_PHY_HARD_RESET_COMPLETE,
	PRL_HR_WAIT_FOR_PE_HARD_RESET_COMPLETE,
};

enum usb_rch_state {
	RCH_WAIT_FOR_MESSAGE_FROM_PROTOCOL_LAYER,
	RCH_PASS_UP_MESSAGE,
	RCH_PROCESSING_EXTENDED_MESSAGE,
	RCH_REQUESTING_CHUNK,
	RCH_WAITING_CHUNK,
	RCH_REPORT_ERROR,
};

enum usb_tch_state {
	TCH_WAIT_FOR_MESSAGE_REQUEST_FROM_PE,
	TCH_WAIT_FOR_TRANSMISSION_COMPLETE,
	TCH_CONSTRUCT_CHUNKED_MESSAGE,
	TCH_SENDING_CHUNKED_MESSAGE,
	TCH_WAIT_CHUNK_REQUEST,
	TCH_MESSAGE_RECEIVED,
	TCH_MESSAGE_SENT,
	TCH_REPORT_ERROR,
};

/* Defined in implementation */
enum usb_prl_tx_state prl_tx_get_state(const int port);
enum usb_prl_hr_state prl_hr_get_state(const int port);
enum usb_rch_state rch_get_state(const int port);
enum usb_tch_state tch_get_state(const int port);


static uint32_t test_data[] = {
	0x00010203, 0x04050607, 0x08090a0b, 0x0c0d0e0f,
	0x10111213, 0x14151617, 0x1819a0b0, 0xc0d0e0f0,
	0x20212223, 0x24252627, 0x28292a2b, 0x2c2d2e2f,
	0x30313233, 0x34353637, 0x38393a3b, 0x3c3d3e3f,
	0x40414243, 0x44454647, 0x48494a4b, 0x4c4d4e4f,
	0x50515253, 0x54555657, 0x58595a5b, 0x5c5d5e5f,
	0x60616263, 0x64656667, 0x68696a6b, 0x6c6d6e6f,
	0x70717273, 0x74757677, 0x78797a7b, 0x7c7d7e7f,
	0x80818283, 0x84858687, 0x88898a8b, 0x8c8d8e8f,
	0x90919293, 0x94959697, 0x98999a9b, 0x9c9d9e9f,
	0xa0a1a2a3, 0xa4a5a6a7, 0xa8a9aaab, 0xacadaeaf,
	0xb0b1b2b3, 0xb4b5b6b7, 0xb8b9babb, 0xbcbdbebf,
	0xc0c1c2c3, 0xc4c5c6c7, 0xc8c9cacb, 0xcccdcecf,
	0xd0d1d2d3, 0xd4d5d6d7, 0xd8d9dadb, 0xdcdddedf,
	0xe0e1e2e3, 0xe4e5e6e7, 0xe8e9eaeb, 0xecedeeef,
	0xf0f1f2f3, 0xf4f5f6f7, 0xf8f9fafb, 0xfcfdfeff,
	0x11223344
};

static struct pd_prl {
	int rev;
	int pd_enable;
	enum pd_power_role power_role;
	enum pd_data_role data_role;
	int msg_tx_id;
	int msg_rx_id;

	int mock_pe_message_sent;
	int mock_pe_error;
	int mock_pe_hard_reset_sent;
	int mock_pe_got_hard_reset;
	int mock_pe_message_received;
	int mock_got_soft_reset;
} pd_port[CONFIG_USB_PD_PORT_MAX_COUNT];

static void init_port(int port, int rev)
{
	pd_port[port].rev = rev;
	pd_port[port].pd_enable = 0;
	pd_port[port].power_role = PD_ROLE_SINK;
	pd_port[port].data_role = PD_ROLE_UFP;
	pd_port[port].msg_tx_id = 0;
	pd_port[port].msg_rx_id = 0;

	tcpm_init(port);
	tcpm_set_polarity(port, 0);
	tcpm_set_rx_enable(port, 0);
}

static inline uint32_t pending_pd_task_events(int port)
{
	return *task_get_event_bitmap(PD_PORT_TO_TASK_ID(port));
}

void inc_tx_id(int port)
{
	pd_port[port].msg_tx_id = (pd_port[port].msg_tx_id + 1) & 7;
}

void inc_rx_id(int port)
{
	pd_port[port].msg_rx_id = (pd_port[port].msg_rx_id + 1) % 7;
}

static int verify_goodcrc(int port, int role, int id)
{
	return pd_test_tx_msg_verify_sop(port) &&
		pd_test_tx_msg_verify_short(port, PD_HEADER(PD_CTRL_GOOD_CRC,
				role, role, id, 0, 0, 0)) &&
		pd_test_tx_msg_verify_crc(port) &&
		pd_test_tx_msg_verify_eop(port);
}

static void simulate_rx_msg(int port, uint16_t header, int cnt,
							const uint32_t *data)
{
	int i;

	pd_test_rx_set_preamble(port, 1);
	pd_test_rx_msg_append_sop(port);
	pd_test_rx_msg_append_short(port, header);

	crc32_init();
	crc32_hash16(header);

	for (i = 0; i < cnt; ++i) {
		pd_test_rx_msg_append_word(port, data[i]);
		crc32_hash32(data[i]);
	}

	pd_test_rx_msg_append_word(port, crc32_result());

	pd_test_rx_msg_append_eop(port);
	pd_test_rx_msg_append_last_edge(port);

	pd_simulate_rx(port);
}

static void simulate_goodcrc(int port, int role, int id)
{
	simulate_rx_msg(port, PD_HEADER(PD_CTRL_GOOD_CRC, role, role, id, 0,
					pd_port[port].rev, 0), 0, NULL);
}

static void cycle_through_state_machine(int port, uint32_t num, uint32_t time)
{
	int i;

	for (i = 0; i < num; i++) {
		task_wake(PD_PORT_TO_TASK_ID(port));
		task_wait_event(time);
	}
}

static int simulate_request_chunk(int port, enum pd_data_msg_type msg_type,
							int chunk_num, int len)
{
	uint16_t header = PD_HEADER(msg_type, pd_port[port].power_role,
					pd_port[port].data_role,
					pd_port[port].msg_rx_id,
					1, pd_port[port].rev, 1);
	uint32_t msg = PD_EXT_HEADER(chunk_num, 1, len);

	simulate_rx_msg(port, header, 1, (const uint32_t *)&msg);
	task_wait_event(30 * MSEC);

	if (!verify_goodcrc(port, pd_port[port].data_role,
						pd_port[port].msg_rx_id))
		return 0;

	return 1;
}

static int simulate_receive_ctrl_msg(int port, enum pd_ctrl_msg_type msg_type)
{
	uint16_t header = PD_HEADER(msg_type, pd_port[port].power_role,
			pd_port[port].data_role, pd_port[port].msg_rx_id,
			0, pd_port[port].rev, 0);

	simulate_rx_msg(port, header, 0, NULL);
	task_wait_event(30 * MSEC);

	if (!verify_goodcrc(port, pd_port[port].data_role,
						pd_port[port].msg_rx_id))
		return 0;

	return 1;
}

static int verify_data_reception(int port, uint16_t header, int len)
{
	int i;
	int cnt = (len + 3) & ~3;

	cycle_through_state_machine(port, 3, 10 * MSEC);

	if (pd_port[port].mock_pe_error >= 0)
		return 0;

	if (!pd_port[port].mock_pe_message_received)
		return 0;

	if (emsg[port].header != header)
		return 0;

	if (emsg[port].len != cnt)
		return 0;

	for (i = 0; i < cnt; i++) {
		if (i < len) {
			if (emsg[port].buf[i] !=
					*((unsigned char *)test_data + i))
				return 0;
		} else {
			if (emsg[port].buf[i] != 0)
				return 0;
		}
	}

	return 1;
}

static int verify_chunk_data_reception(int port, uint16_t header, int len)
{
	int i;
	uint8_t *td = (uint8_t *)test_data;

	if (pd_port[port].mock_got_soft_reset) {
		ccprintf("Got mock soft reset\n");
		return 0;
	}

	if (!pd_port[port].mock_pe_message_received) {
		ccprintf("No mock pe msg received\n");
		return 0;
	}

	if (pd_port[port].mock_pe_error >= 0) {
		ccprintf("Mock pe error (%d)\n", pd_port[port].mock_pe_error);
		return 0;
	}

	if (emsg[port].len != len) {
		ccprintf("emsg len (%d) != 0\n", emsg[port].len);
		return 0;
	}

	for (i = 0; i < len; i++) {
		if (emsg[port].buf[i] != td[i]) {
			ccprintf("emsg buf[%d] != td\n", i);
			return 0;
		}
	}

	return 1;
}

static int simulate_receive_data(int port, enum pd_data_msg_type msg_type,
									int len)
{
	int i;
	int nw = (len + 3) >> 2;
	uint8_t td[28];
	uint16_t header = PD_HEADER(msg_type, pd_port[port].power_role,
		pd_port[port].data_role, pd_port[port].msg_rx_id,
		nw, pd_port[port].rev, 0);

	pd_port[port].mock_pe_error = -1;
	pd_port[port].mock_pe_message_received = 0;
	emsg[port].header = 0;
	emsg[port].len = 0;
	memset(emsg[port].buf, 0, 260);

	for (i = 0; i < 28; i++) {
		if (i < len)
			td[i] = *((uint8_t *)test_data + i);
		else
			td[i] = 0;
	}

	simulate_rx_msg(port, header, nw, (uint32_t *)td);
	task_wait_event(30 * MSEC);

	if (!verify_goodcrc(port, pd_port[port].data_role,
						pd_port[port].msg_rx_id))
		return 0;

	inc_rx_id(port);

	return verify_data_reception(port, header, len);
}

static int simulate_receive_extended_data(int port,
					enum pd_data_msg_type msg_type, int len)
{
	int i;
	int j;
	int byte_len;
	int nw;
	int dsize;
	uint8_t td[28];
	int chunk_num = 0;
	int data_offset = 0;
	uint8_t *expected_data = (uint8_t *)test_data;
	uint16_t header;

	pd_port[port].mock_pe_error = -1;
	pd_port[port].mock_pe_message_received = 0;
	emsg[port].header = 0;
	emsg[port].len = 0;
	memset(emsg[port].buf, 0, 260);

	dsize = len;
	for (j = 0; j < 10; j++) {
		/* Let state machine settle before starting another round */
		cycle_through_state_machine(port, 10, MSEC);

		byte_len = len;
		if (byte_len > 26)
			byte_len = 26;

		len -= 26;

		memset(td, 0, 28);
		*(uint16_t *)td = PD_EXT_HEADER(chunk_num, 0, dsize);

		for (i = 0; i < byte_len; i++)
			td[i + 2] = *(expected_data + data_offset++);

		nw = (byte_len + 2 + 3) >> 2;
		header = PD_HEADER(msg_type, pd_port[port].power_role,
			pd_port[port].data_role, pd_port[port].msg_rx_id,
			nw, pd_port[port].rev, 1);

		if (pd_port[port].mock_pe_error >= 0) {
			ccprintf("Mock pe error (%d) iteration (%d)\n",
				pd_port[port].mock_pe_error, j);
			return 0;
		}

		if (pd_port[port].mock_pe_message_received) {
			ccprintf("Mock pe msg received iteration (%d)\n", j);
			return 0;
		}

		if (emsg[port].len != 0) {
			ccprintf("emsg len (%d) != 0 iteration (%d)\n",
				emsg[port].len, j);
			return 0;
		}

		simulate_rx_msg(port, header, nw, (uint32_t *)td);
		cycle_through_state_machine(port, 1, MSEC);

		if (!verify_goodcrc(port, pd_port[port].data_role,
						pd_port[port].msg_rx_id)) {
			ccprintf("Verify goodcrc bad iteration (%d)\n", j);
			return 0;
		}

		cycle_through_state_machine(port, 1, MSEC);
		inc_rx_id(port);

		/*
		 * If no more data, do expected to get a chunk request
		 */
		if (len <= 0)
			break;

		/*
		 * We need to ensure that the TX event has been set, which may
		 * require an extra cycle through the state machine
		 */
		if (!(PD_EVENT_TX & pending_pd_task_events(port)))
			cycle_through_state_machine(port, 1, MSEC);

		chunk_num++;

		/* Test Request next chunk packet */
		if (!pd_test_tx_msg_verify_sop(port)) {
			ccprintf("Verify sop bad iteration (%d)\n", j);
			return 0;
		}

		if (!pd_test_tx_msg_verify_short(port,
				PD_HEADER(msg_type,
				pd_port[port].power_role,
				pd_port[port].data_role,
				pd_port[port].msg_tx_id,
				1, pd_port[port].rev, 1))) {
			ccprintf("Verify msg short bad iteration (%d)\n", j);
			return 0;
		}

		if (!pd_test_tx_msg_verify_word(port,
				PD_EXT_HEADER(chunk_num, 1, 0))) {
			ccprintf("Verify msg word bad iteration (%d)\n", j);
			return 0;
		}

		if (!pd_test_tx_msg_verify_crc(port)) {
			ccprintf("Verify msg crc bad iteration (%d)\n", j);
			return 0;
		}

		if (!pd_test_tx_msg_verify_eop(port)) {
			ccprintf("Verify msg eop bad iteration (%d)\n", j);
			return 0;
		}

		cycle_through_state_machine(port, 1, MSEC);

		/* Request next chunk packet was good. Send GoodCRC */
		simulate_goodcrc(port, pd_port[port].power_role,
					pd_port[port].msg_tx_id);

		cycle_through_state_machine(port, 1, MSEC);

		inc_tx_id(port);
	}

	cycle_through_state_machine(port, 1, MSEC);

	return verify_chunk_data_reception(port, header, dsize);
}

static int verify_ctrl_msg_transmission(int port,
						enum pd_ctrl_msg_type msg_type)
{
	if (!pd_test_tx_msg_verify_sop(port))
		return 0;

	if (!pd_test_tx_msg_verify_short(port,
			PD_HEADER(msg_type, pd_port[port].power_role,
			pd_port[port].data_role, pd_port[port].msg_tx_id, 0,
			pd_port[port].rev, 0)))
		return 0;

	if (!pd_test_tx_msg_verify_crc(port))
		return 0;

	if (!pd_test_tx_msg_verify_eop(port))
		return 0;

	return 1;
}

static int simulate_send_ctrl_msg_request_from_pe(int port,
	enum tcpm_transmit_type type, enum pd_ctrl_msg_type msg_type)
{
	pd_port[port].mock_got_soft_reset = 0;
	pd_port[port].mock_pe_error = -1;
	pd_port[port].mock_pe_message_sent = 0;
	prl_send_ctrl_msg(port, type, msg_type);
	cycle_through_state_machine(port, 1, MSEC);

	/* Soft reset takes another iteration through the state machine */
	if (msg_type == PD_CTRL_SOFT_RESET)
		cycle_through_state_machine(port, 1, MSEC);

	return verify_ctrl_msg_transmission(port, msg_type);
}

static int verify_data_msg_transmission(int port,
				enum pd_data_msg_type msg_type, int len)
{
	int i;
	int num_words = (len + 3) >> 2;
	int data_obj_in_bytes;
	uint32_t td;

	if (!pd_test_tx_msg_verify_sop(port))
		return 0;

	if (!pd_test_tx_msg_verify_short(port,
			PD_HEADER(msg_type, pd_port[port].power_role,
			pd_port[port].data_role, pd_port[port].msg_tx_id,
			num_words, pd_port[port].rev, 0)))
		return 0;

	for (i = 0; i < num_words; i++) {
		td = test_data[i];
		data_obj_in_bytes = (i + 1) * 4;
		if (data_obj_in_bytes > len) {
			switch (data_obj_in_bytes - len) {
			case 1:
				td &= 0x00ffffff;
				break;
			case 2:
				td &= 0x0000ffff;
				break;
			case 3:
				td &= 0x000000ff;
				break;
			}
		}

		if (!pd_test_tx_msg_verify_word(port, td))
			return 0;
	}

	if (!pd_test_tx_msg_verify_crc(port))
		return 0;

	if (!pd_test_tx_msg_verify_eop(port))
		return 0;

	return 1;
}

static int simulate_send_data_msg_request_from_pe(int port,
	enum tcpm_transmit_type type, enum pd_ctrl_msg_type msg_type, int len)
{
	int i;
	uint8_t *buf = emsg[port].buf;
	uint8_t *td = (uint8_t *)test_data;

	pd_port[port].mock_got_soft_reset = 0;
	pd_port[port].mock_pe_error = -1;
	pd_port[port].mock_pe_message_sent = 0;

	for (i = 0; i < len; i++)
		buf[i] = td[i];

	emsg[port].len = len;

	prl_send_data_msg(port, type, msg_type);
	cycle_through_state_machine(port, 1, MSEC);

	return verify_data_msg_transmission(port, msg_type, len);
}

static int verify_extended_data_msg_transmission(int port,
			enum pd_data_msg_type msg_type, int len)
{
	int i;
	int j;
	int nw;
	int byte_len;
	int dsize;
	uint32_t td;
	uint8_t *expected_data = (uint8_t *)&test_data;
	int data_offset = 0;
	int chunk_number_to_send = 0;

	dsize = len;

	for (j = 0; j < 10; j++) {
		byte_len = len;
		if (byte_len > 26)
			byte_len = 26;

		nw = (byte_len + 2 + 3) >> 2;

		if (!pd_test_tx_msg_verify_sop(port)) {
			ccprintf("failed tx sop; iteration (%d)\n", j);
			return 0;
		}

		if (!pd_test_tx_msg_verify_short(port,
				PD_HEADER(msg_type, pd_port[port].power_role,
				pd_port[port].data_role,
				pd_port[port].msg_tx_id,
				nw, pd_port[port].rev, 1))) {
			ccprintf("failed tx short\n");
			return 0;
		}
		td = PD_EXT_HEADER(chunk_number_to_send, 0, dsize);
		td |= *(expected_data + data_offset++) << 16;
		td |= *(expected_data + data_offset++) << 24;

		if (byte_len == 1)
			td &= 0x00ffffff;

		if (!pd_test_tx_msg_verify_word(port, td)) {
			ccprintf("failed tx word\n");
			return 0;
		}

		byte_len -= 2;

		if (byte_len > 0) {
			nw = (byte_len + 3) >> 2;
			for (i = 0; i < nw; i++) {
				td = *(expected_data + data_offset++) << 0;
				td |= *(expected_data +	data_offset++) << 8;
				td |= *(expected_data +	data_offset++) << 16;
				td |= *(expected_data +	data_offset++) << 24;

				switch (byte_len) {
				case 3:
					td &= 0x00ffffff;
					break;
				case 2:
					td &= 0x0000ffff;
					break;
				case 1:
					td &= 0x000000ff;
					break;
				}

				if (!pd_test_tx_msg_verify_word(port, td))
					return 0;
				byte_len -= 4;
			}
		}

		if (!pd_test_tx_msg_verify_crc(port)) {
			ccprintf("failed tx crc\n");
			return 0;
		}

		if (!pd_test_tx_msg_verify_eop(port)) {
			ccprintf("failed tx eop\n");
			return 0;
		}

		cycle_through_state_machine(port, 1, MSEC);

		/* Send GoodCRC */
		simulate_goodcrc(port, pd_port[port].power_role,
						pd_port[port].msg_tx_id);
		cycle_through_state_machine(port, 1, MSEC);
		inc_tx_id(port);

		len -= 26;
		if (len <= 0)
			break;

		chunk_number_to_send++;
		/* Let state machine settle */
		cycle_through_state_machine(port, 10, MSEC);
		if (!simulate_request_chunk(port, msg_type,
				chunk_number_to_send, dsize)) {
			ccprintf("failed request chunk\n");
			return 0;
		}

		cycle_through_state_machine(port, 1, MSEC);
		inc_rx_id(port);
	}

	return 1;
}

static int simulate_send_extended_data_msg(int port,
		enum tcpm_transmit_type type, enum pd_ctrl_msg_type msg_type,
		int len)
{
	int i;
	uint8_t *buf = emsg[port].buf;
	uint8_t *td = (uint8_t *)test_data;

	memset(buf, 0, 260);
	emsg[port].len = len;

	/* don't overflow buffer */
	if (len > 260)
		len = 260;

	for (i = 0; i < len; i++)
		buf[i] = td[i];

	prl_send_ext_data_msg(port, type, msg_type);
	cycle_through_state_machine(port, 1, MSEC);

	return verify_extended_data_msg_transmission(port, msg_type,
							len);
}

static void enable_prl(int port, int en)
{
	tcpm_set_rx_enable(port, en);

	pd_port[port].pd_enable = en;
	pd_port[port].msg_tx_id = 0;
	pd_port[port].msg_rx_id = 0;

	/* Init PRL */
	cycle_through_state_machine(port, 10, MSEC);

	prl_set_rev(port, pd_port[port].rev);
}

enum pd_power_role tc_get_power_role(int port)
{
	return pd_port[port].power_role;
}

enum pd_data_role tc_get_data_role(int port)
{
	return pd_port[port].data_role;
}

enum pd_cable_plug tc_get_cable_plug(int port)
{
	return PD_PLUG_FROM_DFP_UFP;
}

void pe_report_error(int port, enum pe_error e)
{
	pd_port[port].mock_pe_error = e;
}

void pe_got_hard_reset(int port)
{
	pd_port[port].mock_pe_got_hard_reset = 1;
}

void pe_message_received(int port)
{
	pd_port[port].mock_pe_message_received = 1;
}

void pe_message_sent(int port)
{
	pd_port[port].mock_pe_message_sent = 1;
}

void pe_hard_reset_sent(int port)
{
	pd_port[port].mock_pe_hard_reset_sent = 1;
}

void pe_got_soft_reset(int port)
{
	pd_port[port].mock_got_soft_reset = 1;
}

static int test_prl_reset(void)
{
	int port = PORT0;

	enable_prl(port, 1);

	prl_reset(port);

	TEST_ASSERT(prl_tx_get_state(port) ==
				PRL_TX_WAIT_FOR_MESSAGE_REQUEST);
	TEST_ASSERT(rch_get_state(port) ==
				RCH_WAIT_FOR_MESSAGE_FROM_PROTOCOL_LAYER);
	TEST_ASSERT(tch_get_state(port) ==
				TCH_WAIT_FOR_MESSAGE_REQUEST_FROM_PE);
	TEST_ASSERT(prl_hr_get_state(port) ==
				PRL_HR_WAIT_FOR_REQUEST);
	enable_prl(port, 0);

	return EC_SUCCESS;
}

static int test_send_ctrl_msg(void)
{
	int i;
	int port = PORT0;

	enable_prl(port, 1);

	/*
	 * TEST: Control message transmission and tx_id increment
	 */
	for (i = 0; i < 10; i++) {
		task_wake(PD_PORT_TO_TASK_ID(port));
		task_wait_event(40 * MSEC);

		TEST_ASSERT(prl_tx_get_state(port) ==
					PRL_TX_WAIT_FOR_MESSAGE_REQUEST);

		TEST_ASSERT(simulate_send_ctrl_msg_request_from_pe(port,
						TCPC_TX_SOP, PD_CTRL_ACCEPT));

		cycle_through_state_machine(port, 1, MSEC);

		simulate_goodcrc(port, pd_port[port].power_role,
						pd_port[port].msg_tx_id);
		inc_tx_id(port);

		/* Let statemachine settle */
		cycle_through_state_machine(port, 10, MSEC);

		TEST_ASSERT(!pd_port[port].mock_got_soft_reset);
		TEST_ASSERT(pd_port[port].mock_pe_message_sent);
		TEST_ASSERT(pd_port[port].mock_pe_error < 0);
	}

	enable_prl(port, 0);

	return EC_SUCCESS;
}

static int test_send_ctrl_msg_with_retry_and_fail(void)
{
	int i;
	int port = PORT0;

	enable_prl(port, 1);

	/*
	 * TEST: Control message transmission fail with retry
	 */
	task_wake(PD_PORT_TO_TASK_ID(port));
	task_wait_event(MSEC);

	TEST_ASSERT(prl_tx_get_state(port) ==
			PRL_TX_WAIT_FOR_MESSAGE_REQUEST);

	TEST_ASSERT(simulate_send_ctrl_msg_request_from_pe(port,
					TCPC_TX_SOP, PD_CTRL_ACCEPT));

	cycle_through_state_machine(port, 1, MSEC);

	simulate_goodcrc(port, pd_port[port].power_role,
					pd_port[port].msg_tx_id);

	/* Do not increment tx_id so phy layer will not transmit message */

	/* Let statemachine settle */
	cycle_through_state_machine(port, 10, MSEC);

	TEST_ASSERT(!pd_port[port].mock_got_soft_reset);
	TEST_ASSERT(pd_port[port].mock_pe_message_sent);

	task_wake(PD_PORT_TO_TASK_ID(port));
	task_wait_event(MSEC);

	TEST_ASSERT(prl_tx_get_state(port) ==
					PRL_TX_WAIT_FOR_MESSAGE_REQUEST);

	pd_port[port].mock_pe_message_sent = 0;
	prl_send_ctrl_msg(port, TCPC_TX_SOP, PD_CTRL_ACCEPT);
	cycle_through_state_machine(port, 1, MSEC);

	for (i = 0; i < N_RETRY_COUNT + 1; i++) {
		/* Ensure that we have timed out */
		cycle_through_state_machine(port, 10, 100 * MSEC);

		TEST_ASSERT(!pd_port[port].mock_got_soft_reset);
		TEST_ASSERT(pd_port[port].mock_pe_message_sent == 0);
		if (i == N_RETRY_COUNT)
			TEST_ASSERT(pd_port[port].mock_pe_error ==
							ERR_TCH_XMIT);
		else
			TEST_ASSERT(pd_port[port].mock_pe_error < 0);
	}

	enable_prl(port, 0);

	return EC_SUCCESS;
}

static int test_send_ctrl_msg_with_retry_and_success(void)
{
	int i;
	int port = PORT0;

	enable_prl(port, 1);

	/*
	 * TEST: Control message transmission fail with retry
	 */

	TEST_ASSERT(prl_tx_get_state(port) ==
				PRL_TX_WAIT_FOR_MESSAGE_REQUEST);

	pd_port[port].mock_got_soft_reset = 0;
	pd_port[port].mock_pe_error = -1;
	pd_port[port].mock_pe_message_sent = 0;

	prl_send_ctrl_msg(port, TCPC_TX_SOP, PD_CTRL_ACCEPT);
	task_wait_event(40 * MSEC);

	task_wake(PD_PORT_TO_TASK_ID(port));
	task_wait_event(40 * MSEC);

	simulate_goodcrc(port, pd_port[port].power_role,
						pd_port[port].msg_tx_id);

	/* Do not increment tx_id. */

	cycle_through_state_machine(port, 3, 10 * MSEC);

	TEST_ASSERT(!pd_port[port].mock_got_soft_reset);
	TEST_ASSERT(pd_port[port].mock_pe_message_sent);

	task_wake(PD_PORT_TO_TASK_ID(port));
	task_wait_event(40 * MSEC);

	TEST_ASSERT(prl_tx_get_state(port) ==
				PRL_TX_WAIT_FOR_MESSAGE_REQUEST);

	pd_port[port].mock_pe_message_sent = 0;
	prl_send_ctrl_msg(port, TCPC_TX_SOP, PD_CTRL_ACCEPT);
	task_wait_event(30 * MSEC);

	task_wake(PD_PORT_TO_TASK_ID(port));
	task_wait_event(30 * MSEC);

	for (i = 0; i < N_RETRY_COUNT + 1; i++) {
		if (i == N_RETRY_COUNT)
			inc_tx_id(port);

		simulate_goodcrc(port, pd_port[port].power_role,
						pd_port[port].msg_tx_id);

		cycle_through_state_machine(port, 8, 10 * MSEC);

		task_wake(PD_PORT_TO_TASK_ID(port));
		task_wait_event(PD_T_TCPC_TX_TIMEOUT);

		TEST_ASSERT(!pd_port[port].mock_got_soft_reset);
		if (i == N_RETRY_COUNT)
			TEST_ASSERT(pd_port[port].mock_pe_message_sent);
		else
			TEST_ASSERT(pd_port[port].mock_pe_message_sent == 0);
		TEST_ASSERT(pd_port[port].mock_pe_error < 0);
	}

	enable_prl(port, 0);

	return EC_SUCCESS;
}

static int test_send_data_msg(void)
{
	int i;
	int port = PORT0;

	enable_prl(port, 1);

	/*
	 * TEST: Sending data  message with 1 to 28 bytes
	 */
	for (i = 1; i <= 28; i++) {
		cycle_through_state_machine(port, 1, MSEC);

		TEST_ASSERT(prl_tx_get_state(port) ==
					PRL_TX_WAIT_FOR_MESSAGE_REQUEST);

		TEST_ASSERT(simulate_send_data_msg_request_from_pe(port,
					TCPC_TX_SOP, PD_DATA_SOURCE_CAP, i));

		cycle_through_state_machine(port, 1, MSEC);

		simulate_goodcrc(port, pd_port[port].power_role,
						pd_port[port].msg_tx_id);
		inc_tx_id(port);

		cycle_through_state_machine(port, 10, MSEC);

		TEST_ASSERT(!pd_port[port].mock_got_soft_reset);
		TEST_ASSERT(pd_port[port].mock_pe_message_sent);
		TEST_ASSERT(pd_port[port].mock_pe_error < 0);
	}

	enable_prl(port, 0);

	return EC_SUCCESS;
}

static int test_send_data_msg_to_much_data(void)
{
	int port = PORT0;

	enable_prl(port, 1);

	/*
	 * TEST: Send data message with more than 28-bytes, should fail
	 */
	task_wake(PD_PORT_TO_TASK_ID(port));
	task_wait_event(40 * MSEC);

	TEST_ASSERT(prl_tx_get_state(port) ==
				PRL_TX_WAIT_FOR_MESSAGE_REQUEST);

	/* Try to send 29-bytes */
	TEST_ASSERT(!simulate_send_data_msg_request_from_pe(port,
					TCPC_TX_SOP, PD_DATA_SOURCE_CAP, 29));

	task_wake(PD_PORT_TO_TASK_ID(port));
	task_wait_event(30 * MSEC);

	cycle_through_state_machine(port, 10, MSEC);

	TEST_ASSERT(!pd_port[port].mock_got_soft_reset);
	TEST_ASSERT(!pd_port[port].mock_pe_message_sent);
	TEST_ASSERT(pd_port[port].mock_pe_error = ERR_TCH_XMIT);

	enable_prl(port, 0);

	return EC_SUCCESS;
}

static int test_send_extended_data_msg(void)
{
	int i;
	int port = PORT0;

	enable_prl(port, 1);

	/*
	 * TEST: Sending extended data message with 29 to 260 bytes
	 */

	pd_port[port].mock_got_soft_reset = 0;
	pd_port[port].mock_pe_error = -1;

	ccprintf("Iteration ");
	for (i = 29; i <= 260; i++) {
		ccprintf(".%d", i);
		pd_port[port].mock_pe_message_sent = 0;

		cycle_through_state_machine(port, 10, MSEC);

		TEST_ASSERT(prl_tx_get_state(port) ==
					PRL_TX_WAIT_FOR_MESSAGE_REQUEST);

		TEST_ASSERT(simulate_send_extended_data_msg(
			port, TCPC_TX_SOP, PD_EXT_MANUFACTURER_INFO, i));

		cycle_through_state_machine(port, 10, MSEC);

		TEST_ASSERT(!pd_port[port].mock_got_soft_reset);
		TEST_ASSERT(pd_port[port].mock_pe_message_sent);
		TEST_ASSERT(pd_port[port].mock_pe_error < 0);
	}
	enable_prl(port, 0);

	return EC_SUCCESS;
}

static int test_receive_soft_reset_msg(void)
{
	int port = PORT0;
	int expected_header = PD_HEADER(PD_CTRL_SOFT_RESET,
				pd_port[port].power_role,
				pd_port[port].data_role,
				pd_port[port].msg_rx_id,
				0, pd_port[port].rev, 0);

	enable_prl(port, 1);

	/*
	 * TEST: Receiving Soft Reset
	 */

	task_wake(PD_PORT_TO_TASK_ID(port));
	task_wait_event(40 * MSEC);

	TEST_ASSERT(rch_get_state(port) ==
			RCH_WAIT_FOR_MESSAGE_FROM_PROTOCOL_LAYER);

	pd_port[port].mock_got_soft_reset = 0;
	pd_port[port].mock_pe_error = -1;
	pd_port[port].mock_pe_message_received = 0;

	TEST_ASSERT(simulate_receive_ctrl_msg(port, PD_CTRL_SOFT_RESET));

	task_wake(PD_PORT_TO_TASK_ID(port));
	task_wait_event(30 * MSEC);

	cycle_through_state_machine(port, 10, MSEC);

	TEST_ASSERT(pd_port[port].mock_got_soft_reset);
	TEST_ASSERT(pd_port[port].mock_pe_error < 0);
	TEST_ASSERT(pd_port[port].mock_pe_message_received);
	TEST_ASSERT(expected_header == emsg[port].header);
	TEST_ASSERT(emsg[port].len == 0);

	enable_prl(port, 0);

	return EC_SUCCESS;
}

static int test_receive_control_msg(void)
{
	int port = PORT0;
	int expected_header = PD_HEADER(PD_CTRL_DR_SWAP,
				pd_port[port].power_role,
				pd_port[port].data_role,
				pd_port[port].msg_rx_id,
				0, pd_port[port].rev, 0);

	enable_prl(port, 1);

	/*
	 * TEST: Receiving a control message
	 */

	task_wake(PD_PORT_TO_TASK_ID(port));
	task_wait_event(40 * MSEC);

	TEST_ASSERT(rch_get_state(port) ==
			RCH_WAIT_FOR_MESSAGE_FROM_PROTOCOL_LAYER);

	pd_port[port].mock_got_soft_reset = 0;
	pd_port[port].mock_pe_error = -1;
	pd_port[port].mock_pe_message_received = 0;

	TEST_ASSERT(simulate_receive_ctrl_msg(port, PD_CTRL_DR_SWAP));

	task_wake(PD_PORT_TO_TASK_ID(port));
	task_wait_event(30 * MSEC);

	cycle_through_state_machine(port, 3, 10 * MSEC);

	TEST_ASSERT(!pd_port[port].mock_got_soft_reset);
	TEST_ASSERT(pd_port[port].mock_pe_error < 0);
	TEST_ASSERT(pd_port[port].mock_pe_message_received);
	TEST_ASSERT(expected_header == emsg[port].header);
	TEST_ASSERT(emsg[port].len == 0);

	enable_prl(port, 0);

	return EC_SUCCESS;
}

static int test_receive_data_msg(void)
{
	int port = PORT0;
	int i;

	enable_prl(port, 1);

	/*
	 * TEST: Receiving data message with 1 to 28 bytes
	 */

	for (i = 1; i <= 28; i++) {
		task_wake(PD_PORT_TO_TASK_ID(port));
		task_wait_event(40 * MSEC);

		TEST_ASSERT(rch_get_state(port) ==
				RCH_WAIT_FOR_MESSAGE_FROM_PROTOCOL_LAYER);
		TEST_ASSERT(simulate_receive_data(port,
						PD_DATA_BATTERY_STATUS, i));
	}

	enable_prl(port, 0);

	return EC_SUCCESS;
}

static int test_receive_extended_data_msg(void)
{
	int len;
	int port = PORT0;

	enable_prl(port, 1);

	/*
	 * TEST: Receiving extended data message with 29 to 260 bytes
	 */

	task_wake(PD_PORT_TO_TASK_ID(port));
	task_wait_event(40 * MSEC);

	TEST_ASSERT(rch_get_state(port) ==
			RCH_WAIT_FOR_MESSAGE_FROM_PROTOCOL_LAYER);

	for (len = 29; len <= 260; len++)
		TEST_ASSERT(simulate_receive_extended_data(port,
					PD_DATA_BATTERY_STATUS, len));

	enable_prl(port, 0);

	return EC_SUCCESS;
}

static int test_send_soft_reset_msg(void)
{
	int port = PORT0;

	enable_prl(port, 1);

	/*
	 * TEST: Send soft reset
	 */

	task_wake(PD_PORT_TO_TASK_ID(port));
	task_wait_event(40 * MSEC);

	TEST_ASSERT(prl_tx_get_state(port) ==
				PRL_TX_WAIT_FOR_MESSAGE_REQUEST);

	TEST_ASSERT(simulate_send_ctrl_msg_request_from_pe(port,
					TCPC_TX_SOP, PD_CTRL_SOFT_RESET));

	task_wake(PD_PORT_TO_TASK_ID(port));
	task_wait_event(30 * MSEC);

	simulate_goodcrc(port, pd_port[port].power_role,
						pd_port[port].msg_tx_id);
	inc_tx_id(port);

	TEST_ASSERT(prl_tx_get_state(port) ==
					PRL_TX_LAYER_RESET_FOR_TRANSMIT);

	cycle_through_state_machine(port, 3, 10 * MSEC);

	TEST_ASSERT(!pd_port[port].mock_got_soft_reset);
	TEST_ASSERT(pd_port[port].mock_pe_message_sent);
	TEST_ASSERT(pd_port[port].mock_pe_error < 0);

	enable_prl(port, 0);

	return EC_SUCCESS;
}

static int test_pe_execute_hard_reset_msg(void)
{
	int port = PORT0;

	enable_prl(port, 1);

	pd_port[port].mock_pe_hard_reset_sent = 0;

	/*
	 * TEST: Policy Engine initiated hard reset
	 */

	task_wake(PD_PORT_TO_TASK_ID(port));
	task_wait_event(40 * MSEC);

	TEST_ASSERT(prl_hr_get_state(port) == PRL_HR_WAIT_FOR_REQUEST);

	/* Simulate receiving hard reset from policy engine */
	prl_execute_hard_reset(port);

	TEST_ASSERT(prl_hr_get_state(port) == PRL_HR_RESET_LAYER);
	TEST_ASSERT(prl_tx_get_state(port) ==
					PRL_TX_WAIT_FOR_MESSAGE_REQUEST);

	cycle_through_state_machine(port, 1, 10 * MSEC);

	TEST_ASSERT(prl_hr_get_state(port) ==
				PRL_HR_WAIT_FOR_PHY_HARD_RESET_COMPLETE);

	cycle_through_state_machine(port, 2, PD_T_PS_HARD_RESET);
	TEST_ASSERT(pd_port[port].mock_pe_hard_reset_sent);

	TEST_ASSERT(prl_hr_get_state(port) ==
				PRL_HR_WAIT_FOR_PE_HARD_RESET_COMPLETE);

	/* Simulate policy engine indicating that it is done hard reset */
	prl_hard_reset_complete(port);

	cycle_through_state_machine(port, 1, 10 * MSEC);

	TEST_ASSERT(prl_hr_get_state(port) == PRL_HR_WAIT_FOR_REQUEST);

	enable_prl(port, 0);

	return EC_SUCCESS;
}

static int test_phy_execute_hard_reset_msg(void)
{
	int port = PORT0;

	enable_prl(port, 1);

	/*
	 * TEST: Port partner initiated hard reset
	 */

	pd_port[port].mock_pe_got_hard_reset = 0;

	task_wake(PD_PORT_TO_TASK_ID(port));
	task_wait_event(40 * MSEC);

	TEST_ASSERT(prl_hr_get_state(port) == PRL_HR_WAIT_FOR_REQUEST);

	/* Simulate receiving hard reset from port partner */
	pd_execute_hard_reset(port);

	TEST_ASSERT(prl_hr_get_state(port) == PRL_HR_RESET_LAYER);
	TEST_ASSERT(prl_tx_get_state(port) ==
					PRL_TX_WAIT_FOR_MESSAGE_REQUEST);

	cycle_through_state_machine(port, 1, 10 * MSEC);

	TEST_ASSERT(prl_hr_get_state(port) ==
				PRL_HR_WAIT_FOR_PE_HARD_RESET_COMPLETE);

	cycle_through_state_machine(port, 2, PD_T_PS_HARD_RESET);
	TEST_ASSERT(pd_port[port].mock_pe_got_hard_reset);

	TEST_ASSERT(prl_hr_get_state(port) ==
				PRL_HR_WAIT_FOR_PE_HARD_RESET_COMPLETE);

	/* Simulate policy engine indicating that it is done hard reset */
	prl_hard_reset_complete(port);

	cycle_through_state_machine(port, 1, 10 * MSEC);

	TEST_ASSERT(prl_hr_get_state(port) == PRL_HR_WAIT_FOR_REQUEST);

	enable_prl(port, 0);

	return EC_SUCCESS;
}

int pd_task(void *u)
{
	int port = PORT0;
	int evt;

	while (1) {
		evt = task_wait_event(-1);

		tcpc_run(port, evt);
		prl_run(port, evt, pd_port[port].pd_enable);
	}

	return EC_SUCCESS;
}

/* Reset the state machine between each test */
void before_test(void)
{
	pd_port[PORT0].mock_pe_message_sent = 0;
	pd_port[PORT0].mock_pe_error = -1;
	pd_port[PORT0].mock_pe_hard_reset_sent = 0;
	pd_port[PORT0].mock_pe_got_hard_reset = 0;
	pd_port[PORT0].mock_pe_message_received = 0;
	pd_port[PORT0].mock_got_soft_reset = 0;
	pd_port[PORT0].pd_enable = false;
	cycle_through_state_machine(PORT0, 10, MSEC);
	pd_port[PORT0].pd_enable = true;
	cycle_through_state_machine(PORT0, 10, MSEC);
}

void run_test(void)
{
	test_reset();

	/* Test PD 2.0 Protocol */
	init_port(PORT0, PD_REV20);
	RUN_TEST(test_prl_reset);
	RUN_TEST(test_send_ctrl_msg);
	RUN_TEST(test_send_ctrl_msg_with_retry_and_fail);
	RUN_TEST(test_send_ctrl_msg_with_retry_and_success);
	RUN_TEST(test_send_data_msg);
	RUN_TEST(test_send_data_msg_to_much_data);
	RUN_TEST(test_receive_control_msg);
	RUN_TEST(test_receive_data_msg);
	RUN_TEST(test_receive_soft_reset_msg);
	RUN_TEST(test_send_soft_reset_msg);
	RUN_TEST(test_pe_execute_hard_reset_msg);
	RUN_TEST(test_phy_execute_hard_reset_msg);

	/* TODO(shurst): More PD 2.0 Tests */

	/* Test PD 3.0 Protocol */
	init_port(PORT0, PD_REV30);
	RUN_TEST(test_prl_reset);
	RUN_TEST(test_send_ctrl_msg);
	RUN_TEST(test_send_ctrl_msg_with_retry_and_fail);
	RUN_TEST(test_send_ctrl_msg_with_retry_and_success);
	RUN_TEST(test_send_data_msg);
	RUN_TEST(test_send_data_msg_to_much_data);
	RUN_TEST(test_send_extended_data_msg);
	RUN_TEST(test_receive_control_msg);
	RUN_TEST(test_receive_data_msg);
	RUN_TEST(test_receive_extended_data_msg);
	RUN_TEST(test_receive_soft_reset_msg);
	RUN_TEST(test_send_soft_reset_msg);
	RUN_TEST(test_pe_execute_hard_reset_msg);
	RUN_TEST(test_phy_execute_hard_reset_msg);

	/* TODO(shurst): More PD 3.0 Tests */

	/* Do basic state machine sanity checks last. */
	RUN_TEST(test_prl_no_parent_cycles);
	RUN_TEST(test_prl_no_empty_state);
	RUN_TEST(test_prl_all_states_named);

	test_print_result();
}

