/* Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Mock Protocol Layer module.
 */
#include <string.h>
#include "common.h"
#include "usb_emsg.h"
#include "usb_pe_sm.h"
#include "usb_prl_sm.h"
#include "mock/usb_prl_mock.h"
#include "task.h"
#include "test_util.h"
#include "timer.h"
#include "usb_pd_tcpm.h"

#ifndef TEST_BUILD
#error "Mocks should only be in the test build."
#endif

/* Defaults should all be 0 values. */
struct extended_msg rx_emsg[CONFIG_USB_PD_PORT_MAX_COUNT];
struct extended_msg tx_emsg[CONFIG_USB_PD_PORT_MAX_COUNT];

struct mock_prl_port_t {
	enum pd_ctrl_msg_type last_ctrl_msg;
	enum pd_data_msg_type last_data_msg;
	enum tcpci_msg_type last_tx_type;
	bool message_sent;
	bool message_received;
	enum pe_error error;
	enum tcpci_msg_type error_tx_type;
};

struct mock_prl_port_t mock_prl_port[CONFIG_USB_PD_PORT_MAX_COUNT];

void mock_prl_reset(void)
{
	int port;

	/* Reset all values to 0. */
	memset(rx_emsg, 0, sizeof(rx_emsg));
	memset(tx_emsg, 0, sizeof(tx_emsg));

	memset(mock_prl_port, 0, sizeof(mock_prl_port));

	for (port = 0 ; port < CONFIG_USB_PD_PORT_MAX_COUNT ; ++port) {
		mock_prl_port[port].last_tx_type = TCPCI_MSG_INVALID;
		mock_prl_port[port].error_tx_type = TCPCI_MSG_INVALID;
	}
}

void prl_end_ams(int port)
{}

void prl_execute_hard_reset(int port)
{
	mock_prl_port[port].last_ctrl_msg = 0;
	mock_prl_port[port].last_data_msg = 0;
	mock_prl_port[port].last_tx_type = TCPCI_MSG_TX_HARD_RESET;
}

enum pd_rev_type prl_get_rev(int port, enum tcpci_msg_type partner)
{
	return PD_REV30;
}

void prl_hard_reset_complete(int port)
{}

int prl_is_running(int port)
{
	return 1;
}

__overridable bool prl_is_busy(int port)
{
	return false;
}

void prl_reset_soft(int port)
{}

void prl_send_ctrl_msg(int port, enum tcpci_msg_type type,
	enum pd_ctrl_msg_type msg)
{
	mock_prl_port[port].last_ctrl_msg = msg;
	mock_prl_port[port].last_data_msg = 0;
	mock_prl_port[port].last_tx_type = type;
}

void prl_send_data_msg(int port, enum tcpci_msg_type type,
	enum pd_data_msg_type msg)
{
	mock_prl_port[port].last_data_msg = msg;
	mock_prl_port[port].last_ctrl_msg = 0;
	mock_prl_port[port].last_tx_type = type;
}

void prl_send_ext_data_msg(int port, enum tcpci_msg_type type,
	enum pd_ext_msg_type msg)
{}

void prl_set_rev(int port, enum tcpci_msg_type partner,
	enum pd_rev_type rev)
{}


int mock_prl_wait_for_tx_msg(int port,
			     enum tcpci_msg_type tx_type,
			     enum pd_ctrl_msg_type ctrl_msg,
			     enum pd_data_msg_type data_msg,
			     int timeout)
{
	uint64_t end_time = get_time().val + timeout;

	while (get_time().val < end_time) {
		if (mock_prl_port[port].last_tx_type != TCPCI_MSG_INVALID) {
			TEST_EQ(mock_prl_port[port].last_tx_type,
				tx_type, "%d");
			TEST_EQ(mock_prl_port[port].last_ctrl_msg,
				ctrl_msg, "%d");
			TEST_EQ(mock_prl_port[port].last_data_msg,
				data_msg, "%d");
			mock_prl_clear_last_sent_msg(port);
			return EC_SUCCESS;
		}
		task_wait_event(5 * MSEC);
	}
	/* A message of the expected type should have been sent by end_time. */
	TEST_ASSERT(0);
	return EC_ERROR_UNKNOWN;
}

enum pd_ctrl_msg_type mock_prl_get_last_sent_ctrl_msg(int port)
{
	enum pd_ctrl_msg_type last = mock_prl_port[port].last_ctrl_msg;

	mock_prl_clear_last_sent_msg(port);
	return last;
}

enum pd_data_msg_type mock_prl_get_last_sent_data_msg(int port)
{
	enum pd_data_msg_type last = mock_prl_port[port].last_data_msg;

	mock_prl_clear_last_sent_msg(port);
	return last;
}

void mock_prl_clear_last_sent_msg(int port)
{
	mock_prl_port[port].last_data_msg = 0;
	mock_prl_port[port].last_ctrl_msg = 0;
	mock_prl_port[port].last_tx_type = TCPCI_MSG_INVALID;
}

timestamp_t prl_get_tcpc_tx_success_ts(int port)
{
	return get_time();
}
void mock_prl_message_sent(int port)
{
	mock_prl_port[port].message_sent = 1;
}

void mock_prl_message_received(int port)
{
	mock_prl_port[port].message_received = 1;
}

void mock_prl_report_error(int port, enum pe_error e,
			   enum tcpci_msg_type tx_type)
{
	mock_prl_port[port].error = e;
	mock_prl_port[port].error_tx_type = tx_type;
}

void prl_run(int port, int evt, int en)
{
	if (mock_prl_port[port].message_sent) {
		ccprints("message_sent");
		pe_message_sent(port);
		mock_prl_port[port].message_sent = 0;
	}
	if (mock_prl_port[port].message_received) {
		ccprints("message_received");
		pe_message_received(port);
		mock_prl_port[port].message_received = 0;
	}
	if (mock_prl_port[port].error_tx_type != TCPCI_MSG_INVALID) {
		ccprints("pe_error %d", mock_prl_port[port].error);
		pe_report_error(port,
				mock_prl_port[port].error,
				mock_prl_port[port].error_tx_type);
		mock_prl_port[port].error = 0;
		mock_prl_port[port].error_tx_type = TCPCI_MSG_INVALID;
	}
}
