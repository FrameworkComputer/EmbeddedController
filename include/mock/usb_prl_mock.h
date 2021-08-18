/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
/* Mock for USB protocol layer */

#ifndef __MOCK_USB_PRL_MOCK_H
#define __MOCK_USB_PRL_MOCK_H

#include "common.h"
#include "usb_emsg.h"
#include "usb_pd_tcpm.h"

void mock_prl_reset(void);

int mock_prl_wait_for_tx_msg(int port,
			     enum tcpm_sop_type tx_type,
			     enum pd_ctrl_msg_type ctrl_msg,
			     enum pd_data_msg_type data_msg,
			     int timeout);

enum pd_ctrl_msg_type mock_prl_get_last_sent_ctrl_msg(int port);

enum pd_data_msg_type mock_prl_get_last_sent_data_msg(int port);


void mock_prl_clear_last_sent_msg(int port);

void mock_prl_message_sent(int port);

void mock_prl_message_received(int port);

void mock_prl_report_error(int port, enum pe_error e,
			   enum tcpm_sop_type tx_type);

#endif /* __MOCK_DP_ALT_MODE_MOCK_H */
