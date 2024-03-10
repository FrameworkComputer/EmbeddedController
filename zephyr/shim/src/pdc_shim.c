/* Copyright 2024 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <zephyr/sys/atomic.h>
#include <zephyr/toolchain/common.h>

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
	/* TODO:b/326475515 */
}

__override uint8_t board_get_usb_pd_port_count(void)
{
	return CONFIG_USB_PD_PORT_MAX_COUNT;
}

__override int board_set_active_charge_port(int charge_port)
{
	return pdc_power_mgmt_set_active_charge_port(charge_port);
}

const uint32_t *const pd_get_src_caps(int port)
{
	return pdc_power_mgmt_get_src_caps(port);
}

uint8_t pd_get_src_cap_cnt(int port)
{
	return pdc_power_mgmt_get_src_cap_cnt(port);
}

const uint32_t *const pd_get_snk_caps(int port)
{
	return pdc_power_mgmt_get_snk_caps(port);
}

uint8_t pd_get_snk_cap_cnt(int port)
{
	return pdc_power_mgmt_get_snk_cap_cnt(port);
}

uint32_t pd_get_events(int port)
{
	/*
	 * atomic_t (or perhaps eventually atomic_t[] with enough flags) is the
	 * natural Zephyr data type for a bitfield of events. uint32_t is the
	 * legacy ECOS type. Ensure that they are compatible.
	 */
	BUILD_ASSERT(sizeof(uint32_t) >= sizeof(atomic_t));

	return pdc_power_mgmt_get_events(port);
}

void pd_clear_events(int port, uint32_t clear_mask)
{
	pdc_power_mgmt_clear_event(port, clear_mask);
}

struct rmdo pd_get_partner_rmdo(int port)
{
	return pdc_power_mgmt_get_partner_rmdo(port);
}

enum pd_discovery_state pd_get_identity_discovery(int port,
						  enum tcpci_msg_type type)
{
	return pdc_power_mgmt_get_identity_discovery(port, type);
}

int pd_get_rev(int port, enum tcpci_msg_type type)
{
	return pdc_power_mgmt_get_rev(port, type);
}

uint16_t pd_get_identity_vid(int port)
{
	return pdc_power_mgmt_get_identity_vid(port);
}

uint16_t pd_get_identity_pid(int port)
{
	return pdc_power_mgmt_get_identity_pid(port);
}

uint8_t pd_get_product_type(int port)
{
	return pdc_power_mgmt_get_product_type(port);
}
