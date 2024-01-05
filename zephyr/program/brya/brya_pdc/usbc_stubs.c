/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * Stubs required to build without TCPMv2
 */
#include "ec_commands.h"
#include "usb_pd.h"
#include "usb_pd_tbt.h"

__override uint8_t board_get_usb_pd_port_count(void)
{
	return CONFIG_USB_PD_PORT_MAX_COUNT;
}

enum tcpc_cc_polarity pd_get_polarity(int port)
{
	return POLARITY_CC1;
}

enum pd_data_role pd_get_data_role(int port)
{
	return EC_PD_DATA_ROLE_UFP;
}

int pd_is_connected(int port)
{
	return 0;
}

#ifdef CONFIG_PLATFORM_EC_USB_PD_DP_MODE
__override uint8_t get_dp_pin_mode(int port)
{
	return MODE_DP_PIN_D;
}
#endif

#ifdef CONFIG_PLATFORM_EC_USB_PD_TBT_COMPAT_MODE
enum tbt_compat_cable_speed get_tbt_cable_speed(int port)
{
	return TBT_SS_U31_GEN1;
}

enum tbt_compat_rounded_support get_tbt_rounded_support(int port)
{
	return TBT_GEN3_NON_ROUNDED;
}
#endif

void pd_request_data_swap(int port)
{
}

enum pd_power_role pd_get_power_role(int port)
{
	return PD_ROLE_SINK;
}

uint8_t pd_get_task_state(int port)
{
	return 0;
}

int pd_comm_is_enabled(int port)
{
	return 1;
}

bool pd_get_vconn_state(int port)
{
	return true;
}

bool pd_get_partner_dual_role_power(int port)
{
	return false;
}

bool pd_get_partner_data_swap_capable(int port)
{
	return false;
}

bool pd_get_partner_usb_comm_capable(int port)
{
	return false;
}

bool pd_get_partner_unconstr_power(int port)
{
	return false;
}

const char *pd_get_task_state_name(int port)
{
	return "";
}

enum pd_cc_states pd_get_task_cc_state(int port)
{
	return PD_CC_UFP_ATTACHED;
}

bool pd_capable(int port)
{
	return true;
}

void pd_set_new_power_request(int port)
{
}

__override bool board_is_usb_pd_port_present(int port)
{
	if (port == 0)
		return true;
	return false;
}

void pd_request_power_swap(int port)
{
}

int board_set_active_charge_port(int charge_port)
{
	return EC_SUCCESS;
}
