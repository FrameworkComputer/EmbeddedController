/* Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Mock USB Type-C PD */

#include "common.h"
#include "console.h"
#include "usb_pd.h"
#include "mock/usb_pd_mock.h"
#include "memory.h"

struct mock_pd_port_t mock_pd_port[CONFIG_USB_PD_PORT_MAX_COUNT];

enum pd_dual_role_states pd_get_dual_role(int port)
{
	return PD_DRP_TOGGLE_ON;
}

enum pd_data_role pd_get_data_role(int port)
{
	return mock_pd_port[port].data_role;
}
__overridable void tc_set_data_role(int port, enum pd_data_role role)
{
	mock_pd_port[port].data_role = role;
}

enum pd_power_role pd_get_power_role(int port)
{
	return mock_pd_port[port].power_role;
}
__overridable void tc_set_power_role(int port, enum pd_power_role role)
{
	mock_pd_port[port].power_role = role;
}

enum pd_cc_states pd_get_task_cc_state(int port)
{
	return PD_CC_NONE;
}

/* TODO remove when usbc_fake is cleaned up */
#if !defined(CONFIG_USB_DRP_ACC_TRYSRC) && \
	!defined(CONFIG_USB_CTVPD)
int pd_is_connected(int port)
{
	return 1;
}

bool pd_is_disconnected(int port)
{
	return false;
}
#endif /* !CONFIG_USB_DRP_ACC_TRYSRC && !CONFIG_USB_CTVPD */

const uint32_t * const pd_get_src_caps(int port)
{
	return NULL;
}

uint8_t pd_get_src_cap_cnt(int port)
{
	return 0;
}

void pd_set_src_caps(int port, int cnt, uint32_t *src_caps)
{
}

bool pd_get_partner_usb_comm_capable(int port)
{
	return true;
}

inline uint8_t board_get_usb_pd_port_count(void)
{
	return CONFIG_USB_PD_PORT_MAX_COUNT;
}
