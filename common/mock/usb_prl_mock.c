/* Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Fake Protocol Layer module.
 */
#include <string.h>
#include "common.h"
#include "usb_emsg.h"
#include "usb_pe_sm.h"
#include "usb_prl_sm.h"
#include "mock/usb_prl_mock.h"

/* Defaults should all be 0 values. */
struct extended_msg rx_emsg[CONFIG_USB_PD_PORT_MAX_COUNT];
struct extended_msg tx_emsg[CONFIG_USB_PD_PORT_MAX_COUNT];

struct mock_prl_port_t {
	enum pd_ctrl_msg_type last_ctrl_msg;
	enum pd_data_msg_type last_data_msg_type;
	bool message_sent;
	bool message_received;
	int pe_error;
};

struct mock_prl_port_t mock_prl_port[CONFIG_USB_PD_PORT_MAX_COUNT];

void mock_prl_reset(void)
{
	int port;

	/* Reset all values to 0. */
	memset(rx_emsg, 0, sizeof(rx_emsg));
	memset(tx_emsg, 0, sizeof(tx_emsg));

	memset(mock_prl_port, 0, sizeof(mock_prl_port));

	for (port = 0 ; port < CONFIG_USB_PD_PORT_MAX_COUNT ; ++port)
		mock_prl_port[port].pe_error = -1;
}

void prl_end_ams(int port)
{}

void prl_execute_hard_reset(int port)
{}

enum pd_rev_type prl_get_rev(int port, enum tcpm_transmit_type partner)
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

void prl_reset(int port)
{}

void prl_reset_soft(int port)
{}

void prl_send_ctrl_msg(int port, enum tcpm_transmit_type type,
	enum pd_ctrl_msg_type msg)
{
	mock_prl_port[port].last_ctrl_msg = msg;
}

void prl_send_data_msg(int port, enum tcpm_transmit_type type,
	enum pd_data_msg_type msg)
{
	mock_prl_port[port].last_data_msg_type = msg;
}

void prl_send_ext_data_msg(int port, enum tcpm_transmit_type type,
	enum pd_ext_msg_type msg)
{}

void prl_set_rev(int port, enum tcpm_transmit_type partner,
	enum pd_rev_type rev)
{}


enum pd_ctrl_msg_type fake_prl_get_last_sent_ctrl_msg(int port)
{
	enum pd_ctrl_msg_type last = mock_prl_port[port].last_ctrl_msg;

	fake_prl_clear_last_sent_ctrl_msg(port);
	return last;
}

void fake_prl_clear_last_sent_ctrl_msg(int port)
{
	mock_prl_port[port].last_ctrl_msg = 0;
}

enum pd_data_msg_type fake_prl_get_last_sent_data_msg_type(int port)
{
	enum pd_data_msg_type last = mock_prl_port[port].last_data_msg_type;

	fake_prl_clear_last_sent_data_msg(port);
	return last;
}

void fake_prl_clear_last_sent_data_msg(int port)
{
	mock_prl_port[port].last_data_msg_type = 0;
}

void fake_prl_message_sent(int port)
{
	mock_prl_port[port].message_sent = 1;
}

void fake_prl_message_received(int port)
{
	mock_prl_port[port].message_received = 1;
}

void fake_prl_report_error(int port, enum pe_error e)
{
	mock_prl_port[port].pe_error = e;
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
	if (mock_prl_port[port].pe_error >= 0) {
		ccprints("pe_error %d", mock_prl_port[port].pe_error);
		pe_report_error(port,
				mock_prl_port[port].pe_error,
				TCPC_TX_SOP);
		mock_prl_port[port].pe_error = -1;
	}
}
