/* Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * Test USB Type-C module.
 */
#include "common.h"
#include "usb_tc_sm.h"
#include "usb_pd.h"

__overridable int pd_is_vbus_present(int port)
{
	return 0;
}

__overridable void pd_request_data_swap(int port)
{}

__overridable void pd_request_power_swap(int port)
{}

void pd_request_vconn_swap_off(int port)
{}

void pd_request_vconn_swap_on(int port)
{}


static enum pd_data_role data_role;
__overridable enum pd_data_role pd_get_data_role(int port)
{
	return data_role;
}
__overridable void tc_set_data_role(int port, enum pd_data_role role)
{
	data_role = role;
}

static enum pd_power_role power_role;
__overridable enum pd_power_role pd_get_power_role(int port)
{
	return power_role;
}
__overridable void tc_set_power_role(int port, enum pd_power_role role)
{
	power_role = role;
}

__overridable bool pd_get_partner_usb_comm_capable(int port)
{
	return true;
}

__overridable enum pd_cable_plug tc_get_cable_plug(int port)
{
	return PD_PLUG_FROM_DFP_UFP;
}

int tc_check_vconn_swap(int port)
{
	return 0;
}

void tc_ctvpd_detected(int port)
{}

void tc_disc_ident_complete(int port)
{}

static int attached_snk;
int tc_is_attached_snk(int port)
{
	return attached_snk;
}

static int attached_src;
int tc_is_attached_src(int port)
{
	return attached_src;
}

int tc_is_vconn_src(int port)
{
	return 0;
}

void tc_hard_reset(int port)
{}

void tc_partner_dr_data(int port, int en)
{}

void tc_partner_dr_power(int port, int en)
{}

void tc_partner_unconstrainedpower(int port, int en)
{}

void tc_partner_usb_comm(int port, int en)
{}

void tc_pd_connection(int port, int en)
{}

void tc_pr_swap_complete(int port)
{}

void tc_prs_snk_src_assert_rp(int port)
{
	attached_snk = 0;
	attached_src = 1;
}

void tc_prs_src_snk_assert_rd(int port)
{
	attached_snk = 1;
	attached_src = 0;
}

void tc_set_timeout(int port, uint64_t timeout)
{}

__overridable void tc_start_error_recovery(int port)
{}

__overridable void tc_snk_power_off(int port)
{}

int pd_dev_store_rw_hash(int port, uint16_t dev_id, uint32_t *rw_hash,
			 uint32_t ec_current_image)
{
	return 0;
}

enum pd_dual_role_states pd_get_dual_role(int port)
{
	return PD_DRP_TOGGLE_ON;
}

void pd_dev_get_rw_hash(int port, uint16_t *dev_id, uint8_t *rw_hash,
			uint32_t *current_image)
{
}

#ifndef CONFIG_USB_TYPEC_DRP_ACC_TRYSRC
bool pd_is_disconnected(int port)
{
	return false;
}
#endif /* CONFIG_USB_TYPEC_DRP_ACC_TRYSRC */
