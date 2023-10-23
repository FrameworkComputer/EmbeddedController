/* Copyright 2020 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Mock USB TC state machine */

#include "common.h"
#include "console.h"
#include "ec_commands.h"
#include "memory.h"
#include "mock/usb_tc_sm_mock.h"
#include "usb_tc_sm.h"

#ifndef CONFIG_COMMON_RUNTIME
#define cprints(format, args...)
#endif

#ifndef TEST_BUILD
#error "Mocks should only be in the test build."
#endif

struct mock_tc_port_t mock_tc_port[CONFIG_USB_PD_PORT_MAX_COUNT];

void mock_tc_port_reset(void)
{
	int port;

	for (port = 0; port < CONFIG_USB_PD_PORT_MAX_COUNT; ++port) {
		mock_tc_port[port].rev = PD_REV30;
		mock_tc_port[port].pd_enable = 0;
		mock_tc_port[port].msg_tx_id = 0;
		mock_tc_port[port].msg_rx_id = 0;
		mock_tc_port[port].sop = TCPCI_MSG_INVALID;
		mock_tc_port[port].lcl_rp = TYPEC_RP_RESERVED;
		mock_tc_port[port].attached_snk = 0;
		mock_tc_port[port].attached_src = 0;
		mock_tc_port[port].vconn_src = false;
		mock_tc_port[port].data_role = PD_ROLE_UFP;
		mock_tc_port[port].power_role = PD_ROLE_SINK;
	}
}

enum pd_cable_plug tc_get_cable_plug(int port)
{
	return PD_PLUG_FROM_DFP_UFP;
}

uint8_t tc_get_pd_enabled(int port)
{
	return mock_tc_port[port].pd_enable;
}

void typec_select_src_collision_rp(int port, enum tcpc_rp_value rp)
{
	mock_tc_port[port].lcl_rp = rp;
}

int tc_is_attached_src(int port)
{
	return mock_tc_port[port].attached_src;
}

int tc_is_attached_snk(int port)
{
	return mock_tc_port[port].attached_snk;
}

void tc_prs_snk_src_assert_rp(int port)
{
	mock_tc_port[port].attached_snk = 0;
	mock_tc_port[port].attached_src = 1;
}

void tc_prs_src_snk_assert_rd(int port)
{
	mock_tc_port[port].attached_snk = 1;
	mock_tc_port[port].attached_src = 0;
}

int tc_is_vconn_src(int port)
{
	return mock_tc_port[port].vconn_src;
}

void tc_hard_reset_request(int port)
{
	mock_tc_port_reset();
}

/* LCOV_EXCL_START These functions only serve to allow tests to link. */
void typec_select_src_current_limit_rp(int port, enum tcpc_rp_value rp)
{
}

int typec_update_cc(int port)
{
	return EC_SUCCESS;
}

int tc_check_vconn_swap(int port)
{
	return 0;
}

void tc_ctvpd_detected(int port)
{
}
void tc_partner_dr_data(int port, int en)
{
}

void tc_partner_dr_power(int port, int en)
{
}

void tc_partner_unconstrainedpower(int port, int en)
{
}

void tc_partner_usb_comm(int port, int en)
{
}

void tc_pd_connection(int port, int en)
{
}

void tc_pr_swap_complete(int port, bool success)
{
}

void tc_src_power_off(int port)
{
}

void tc_start_error_recovery(int port)
{
}

void tc_snk_power_off(int port)
{
}

void tc_request_power_swap(int port)
{
}

enum pd_dual_role_states pd_get_dual_role(int port)
{
	return PD_DRP_TOGGLE_ON;
}

enum pd_data_role pd_get_data_role(int port)
{
	return mock_tc_port[port].data_role;
}

enum pd_power_role pd_get_power_role(int port)
{
	return mock_tc_port[port].power_role;
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

bool pd_get_partner_usb_comm_capable(int port)
{
	return true;
}

bool pd_get_partner_dual_role_power(int port)
{
	return true;
}

bool pd_capable(int port)
{
	return true;
}

void pd_set_suspend(int port, int suspend)
{
}

void pd_set_error_recovery(int port)
{
}

enum tcpc_cc_polarity pd_get_polarity(int port)
{
	return POLARITY_CC1;
}

void pd_request_data_swap(int port)
{
}

void pd_request_vconn_swap_off(int port)
{
}

void pd_request_vconn_swap_on(int port)
{
}

bool pd_get_vconn_state(int port)
{
	return false;
}

bool pd_alt_mode_capable(int port)
{
	return false;
}
/* LCOV_EXCL_STOP */
