/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Test USB Protocol Layer module.
 */
#include "common.h"
#include "mock/tcpc_mock.h"
#include "mock/tcpm_mock.h"
#include "mock/usb_pd_mock.h"
#include "mock/usb_pe_sm_mock.h"
#include "mock/usb_tc_sm_mock.h"
#include "task.h"
#include "tcpci.h"
#include "tcpm.h"
#include "test_util.h"
#include "timer.h"
#include "usb_emsg.h"
#include "usb_pd.h"
#include "usb_pd_test_util.h"
#include "usb_pe_sm.h"
#include "usb_prl_sm.h"
#include "usb_sm_checks.h"
#include "usb_tc_sm.h"
#include "util.h"

#define PORT0 0

/* Install Mock TCPC and MUX drivers */
const struct tcpc_config_t tcpc_config[CONFIG_USB_PD_PORT_MAX_COUNT] = {
	{
		.drv = &mock_tcpc_driver,
	},
};

static void enable_prl(int port, int en)
{
	tcpm_set_rx_enable(port, en);

	mock_tc_port[port].pd_enable = en;

	task_wait_event(10*MSEC);

	prl_set_rev(port, TCPC_TX_SOP, mock_tc_port[port].rev);
}

static int test_receive_control_msg(void)
{
	int port = PORT0;
	uint16_t header = PD_HEADER(PD_CTRL_DR_SWAP,
		pd_get_power_role(port),
		pd_get_data_role(port),
		mock_tc_port[port].msg_rx_id,
		0, mock_tc_port[port].rev, 0);

	/* Set up the message to be received. */
	mock_tcpm_rx_msg(port, header, 0, NULL);

	/* Process the message. */
	task_wait_event(10*MSEC);

	/* Check results. */
	TEST_NE(mock_pe_port[port].mock_pe_message_received, 0, "%d");
	TEST_EQ(header, rx_emsg[port].header, "%d");
	TEST_EQ(rx_emsg[port].len, 0, "%d");

	TEST_LE(mock_pe_port[port].mock_pe_error, 0, "%d");
	TEST_EQ(mock_pe_port[port].mock_pe_message_discarded, 0, "%d");
	TEST_EQ(mock_pe_port[port].mock_got_soft_reset, 0, "%d");
	TEST_EQ(mock_pe_port[port].mock_pe_got_hard_reset, 0, "%d");
	TEST_EQ(mock_pe_port[port].mock_pe_hard_reset_sent, 0, "%d");

	return EC_SUCCESS;
}

static int test_send_control_msg(void)
{
	int port = PORT0;

	/* Set up the message to be sent. */
	prl_send_ctrl_msg(port, TCPC_TX_SOP, PD_CTRL_ACCEPT);
	task_wait_event(MSEC);
	/* Simulate the TX complete that the PD_INT handler would signal */
	pd_transmit_complete(port, TCPC_TX_COMPLETE_SUCCESS);

	task_wait_event(10*MSEC);

	/* Check results. */
	TEST_NE(mock_pe_port[port].mock_pe_message_sent, 0, "%d");
	TEST_LE(mock_pe_port[port].mock_pe_error, 0, "%d");
	TEST_EQ(mock_pe_port[port].mock_pe_message_discarded, 0, "%d");
	TEST_EQ(mock_pe_port[port].mock_got_soft_reset, 0, "%d");
	TEST_EQ(mock_pe_port[port].mock_pe_got_hard_reset, 0, "%d");
	TEST_EQ(mock_pe_port[port].mock_pe_hard_reset_sent, 0, "%d");

	return EC_SUCCESS;
}

static int test_discard_queued_tx_when_rx_happens(void)
{
	int port = PORT0;
	uint16_t header = PD_HEADER(PD_CTRL_DR_SWAP,
		pd_get_power_role(port),
		pd_get_data_role(port),
		mock_tc_port[port].msg_rx_id,
		0, mock_tc_port[port].rev, 0);
	uint8_t *buf = tx_emsg[port].buf;
	uint8_t len = 8;
	uint8_t i = 0;

	/* Set up the message to be sent. */
	for (i = 0 ; i < len ; i++)
		buf[i] = (uint8_t)i;

	tx_emsg[port].len = len;
	prl_send_data_msg(port, TCPC_TX_SOP, PD_DATA_SOURCE_CAP);

	/* Set up the message to be received. */
	mock_tcpm_rx_msg(port, header, 0, NULL);

	/* Process the message. */
	task_wait_event(10*MSEC);

	/* Check results. Source should have discarded its message queued up
	 * to TX, and should have received the message from the sink.
	 */
	TEST_NE(mock_pe_port[port].mock_pe_message_discarded, 0, "%d");
	TEST_NE(mock_pe_port[port].mock_pe_message_received, 0, "%d");
	TEST_EQ(mock_pe_port[port].mock_pe_message_sent, 0, "%d");

	TEST_LE(mock_pe_port[port].mock_pe_error, 0, "%d");
	TEST_EQ(mock_pe_port[port].mock_got_soft_reset, 0, "%d");
	TEST_EQ(mock_pe_port[port].mock_pe_got_hard_reset, 0, "%d");
	TEST_EQ(mock_pe_port[port].mock_pe_hard_reset_sent, 0, "%d");

	return EC_SUCCESS;
}

void before_test(void)
{
	mock_tc_port_reset();
	mock_tc_port[PORT0].rev = PD_REV30;
	mock_pd_port[PORT0].power_role = PD_ROLE_SOURCE;
	mock_pd_port[PORT0].data_role = PD_ROLE_DFP;

	mock_tcpm_reset();
	mock_pe_port_reset();

	prl_reset(PORT0);
	enable_prl(PORT0, 1);
}

void after_test(void)
{
	enable_prl(PORT0, 0);
}

void run_test(int argc, char **argv)
{
	RUN_TEST(test_receive_control_msg);
	RUN_TEST(test_send_control_msg);
	RUN_TEST(test_discard_queued_tx_when_rx_happens);
	/* TODO add tests here */


	/* Do basic state machine validity checks last. */
	RUN_TEST(test_prl_no_parent_cycles);
	RUN_TEST(test_prl_all_states_named);

	test_print_result();
}
