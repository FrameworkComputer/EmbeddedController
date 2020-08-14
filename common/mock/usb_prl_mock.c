/* Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Fake Protocol Layer module.
 */
#include <string.h>
#include "common.h"
#include "usb_emsg.h"
#include "usb_prl_sm.h"
#include "mock/usb_prl_mock.h"

/* Defaults should all be 0 values. */
struct extended_msg rx_emsg[CONFIG_USB_PD_PORT_MAX_COUNT];
struct extended_msg tx_emsg[CONFIG_USB_PD_PORT_MAX_COUNT];

void mock_prl_reset(void)
{
	/* Reset all values to 0. */
	memset(rx_emsg, 0, sizeof(rx_emsg));
	memset(tx_emsg, 0, sizeof(tx_emsg));
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

static enum pd_ctrl_msg_type last_ctrl_msg[CONFIG_USB_PD_PORT_MAX_COUNT];
static enum pd_data_msg_type last_data_msg_type[CONFIG_USB_PD_PORT_MAX_COUNT];

void prl_send_ctrl_msg(int port, enum tcpm_transmit_type type,
	enum pd_ctrl_msg_type msg)
{
	last_ctrl_msg[port] = msg;
}

void prl_send_data_msg(int port, enum tcpm_transmit_type type,
	enum pd_data_msg_type msg)
{
	last_data_msg_type[port] = msg;
}

void prl_send_ext_data_msg(int port, enum tcpm_transmit_type type,
	enum pd_ext_msg_type msg)
{}

void prl_set_rev(int port, enum tcpm_transmit_type partner,
	enum pd_rev_type rev)
{}


enum pd_ctrl_msg_type fake_prl_get_last_sent_ctrl_msg(int port)
{
	return last_ctrl_msg[port];
}

void fake_prl_clear_last_sent_ctrl_msg(int port)
{
	last_ctrl_msg[port] = 0;
}

enum pd_data_msg_type fake_prl_get_last_sent_data_msg_type(int port)
{
	return last_data_msg_type[port];
}

void fake_prl_clear_last_sent_data_msg(int port)
{
	last_data_msg_type[port] = 0;
}
