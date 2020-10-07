/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
/* Mock USB TC state machine*/

#ifndef __MOCK_USB_TC_SM_MOCK_H
#define __MOCK_USB_TC_SM_MOCK_H

#include "common.h"
#include "usb_tc_sm.h"

struct mock_tc_port_t {
	int rev;
	int pd_enable;
	int msg_tx_id;
	int msg_rx_id;
	enum tcpm_transmit_type sop;
	enum tcpc_rp_value lcl_rp;
	int attached_snk;
	int attached_src;
};

extern struct mock_tc_port_t mock_tc_port[CONFIG_USB_PD_PORT_MAX_COUNT];

void mock_tc_port_reset(void);

#endif /* __MOCK_USB_TC_SM_MOCK_H */
