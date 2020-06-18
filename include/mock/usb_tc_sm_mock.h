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
	int pd_enable;
	enum tcpc_rp_value lcl_rp;
};

extern struct mock_tc_port_t mock_tc_port[CONFIG_USB_PD_PORT_MAX_COUNT];

#endif /* __MOCK_USB_TC_SM_MOCK_H */
