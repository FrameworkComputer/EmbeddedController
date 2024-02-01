/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <usbc/pdc_power_mgmt.h>

enum tcpc_cc_polarity pd_get_polarity(int port)
{
	return pdc_power_mgmt_pd_get_polarity(port);
}

enum pd_data_role pd_get_data_role(int port)
{
	return pdc_power_mgmt_pd_get_data_role(port);
}

int pd_is_connected(int port)
{
	return pdc_power_mgmt_is_connected(port);
}

void pd_request_data_swap(int port)
{
	pdc_power_mgmt_request_data_swap(port);
}

void pd_request_power_swap(int port)
{
	pdc_power_mgmt_request_power_swap(port);
}

enum pd_power_role pd_get_power_role(int port)
{
	return pdc_power_mgmt_get_power_role(port);
}

uint8_t pd_get_task_state(int port)
{
	return pdc_power_mgmt_get_task_state(port);
}

int pd_comm_is_enabled(int port)
{
	return pdc_power_mgmt_comm_is_enabled(port);
}

bool pd_get_vconn_state(int port)
{
	return pdc_power_mgmt_get_vconn_state(port);
}

bool pd_get_partner_dual_role_power(int port)
{
	return pdc_power_mgmt_get_partner_dual_role_power(port);
}

bool pd_get_partner_data_swap_capable(int port)
{
	return pdc_power_mgmt_get_partner_data_swap_capable(port);
}

bool pd_get_partner_usb_comm_capable(int port)
{
	return pdc_power_mgmt_get_partner_usb_comm_capable(port);
}

bool pd_get_partner_unconstr_power(int port)
{
	return pdc_power_mgmt_get_partner_unconstr_power(port);
}

const char *pd_get_task_state_name(int port)
{
	return pdc_power_mgmt_get_task_state_name(port);
}

enum pd_cc_states pd_get_task_cc_state(int port)
{
	return pdc_power_mgmt_get_task_cc_state(port);
}

bool pd_capable(int port)
{
	return pdc_power_mgmt_pd_capable(port);
}

void pd_set_dual_role(int port, enum pd_dual_role_states state)
{
	pdc_power_mgmt_set_dual_role(port, state);
}

void pd_set_new_power_request(int port)
{
}

__override uint8_t board_get_usb_pd_port_count(void)
{
	return CONFIG_USB_PD_PORT_MAX_COUNT;
}

__override int board_set_active_charge_port(int charge_port)
{
	return EC_SUCCESS;
}

const uint32_t *const pd_get_src_caps(int port)
{
	return pdc_power_mgmt_get_src_caps(port);
}

uint8_t pd_get_src_cap_cnt(int port)
{
	return pdc_power_mgmt_get_src_cap_cnt(port);
}
