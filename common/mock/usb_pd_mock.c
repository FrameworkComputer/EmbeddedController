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

void mock_pd_reset(void)
{
	/* Reset all values to 0. */
	memset(mock_pd_port, 0, sizeof(mock_pd_port));
}

enum pd_dual_role_states pd_get_dual_role(int port)
{
	return PD_DRP_TOGGLE_ON;
}

enum pd_data_role pd_get_data_role(int port)
{
	return mock_pd_port[port].data_role;
}

enum pd_power_role pd_get_power_role(int port)
{
	return mock_pd_port[port].power_role;
}

enum pd_cc_states pd_get_task_cc_state(int port)
{
	return PD_CC_NONE;
}

int pd_is_connected(int port)
{
	return 1;
}

bool pd_is_disconnected(int port)
{
	return false;
}

__overridable const uint32_t * const pd_get_src_caps(int port)
{
	return NULL;
}

__overridable uint8_t pd_get_src_cap_cnt(int port)
{
	return 0;
}

__overridable void pd_set_src_caps(int port, int cnt, uint32_t *src_caps)
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

void pd_set_suspend(int port, int suspend)
{
}

enum tcpc_cc_polarity pd_get_polarity(int port)
{
	return POLARITY_CC1;
}

void pd_request_data_swap(int port)
{}

void pd_request_vconn_swap_off(int port)
{}

void pd_request_vconn_swap_on(int port)
{}

bool pd_alt_mode_capable(int port)
{
	return false;
}
