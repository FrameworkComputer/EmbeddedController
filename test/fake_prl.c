/* Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Fake Protocol Layer module.
 */
#include "common.h"
#include "usb_emsg.h"
#include "usb_prl_sm.h"

struct extended_msg emsg[CONFIG_USB_PD_PORT_MAX_COUNT];

void prl_end_ams(int port)
{}

void prl_execute_hard_reset(int port)
{}

enum pd_rev_type prl_get_rev(int port)
{
	return PD_REV30;
}

void prl_hard_reset_complete(int port)
{}

int prl_is_running(int port)
{
	return 0;
}

void prl_reset(int port)
{}

static enum pd_ctrl_msg_type last_ctrl_msg[CONFIG_USB_PD_PORT_MAX_COUNT];
void prl_send_ctrl_msg(int port, enum tcpm_transmit_type type,
	enum pd_ctrl_msg_type msg)
{
	last_ctrl_msg[port] = msg;
}

void prl_send_data_msg(int port, enum tcpm_transmit_type type,
	enum pd_data_msg_type msg)
{}

void prl_send_ext_data_msg(int port, enum tcpm_transmit_type type,
	enum pd_ext_msg_type msg)
{}

void prl_set_rev(int port, enum pd_rev_type rev)
{}

void prl_start_ams(int port)
{}


enum pd_ctrl_msg_type fake_prl_get_last_sent_ctrl_msg(int port)
{
	return last_ctrl_msg[port];
}
