/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
/* Mock for USB protocol layer */

#ifndef __MOCK_USB_PRL_MOCK_H
#define __MOCK_USB_PRL_MOCK_H

#include "common.h"
#include "usb_emsg.h"

void mock_prl_reset(void);

enum pd_ctrl_msg_type fake_prl_get_last_sent_ctrl_msg(int port);

void fake_prl_clear_last_sent_ctrl_msg(int port);

enum pd_data_msg_type fake_prl_get_last_sent_data_msg_type(int port);

void fake_prl_clear_last_sent_data_msg(int port);

void fake_prl_message_sent(int port);

void fake_prl_message_received(int port);

void fake_prl_report_error(int port, enum pe_error e);

#endif /* __MOCK_DP_ALT_MODE_MOCK_H */
