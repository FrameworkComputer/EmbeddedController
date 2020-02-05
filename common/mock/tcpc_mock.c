/* Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
 /* Mock for the TCPC interface */

#include "common.h"
#include "console.h"
#include "usb_pd_tcpm.h"
#include "mock/tcpc_mock.h"
#include "memory.h"

/* Public API for controlling/inspecting this mock */
struct mock_tcpc_ctrl mock_tcpc;

void mock_tcpc_reset(void)
{
	memset(&mock_tcpc, 0, sizeof(mock_tcpc));
}

static int mock_init(int port)
{
	return EC_SUCCESS;
}

static int mock_release(int port)
{
	return EC_SUCCESS;
}

static int mock_get_cc(int port,
	enum tcpc_cc_voltage_status *cc1,
	enum tcpc_cc_voltage_status *cc2)
{
	*cc1 = mock_tcpc.cc1;
	*cc2 = mock_tcpc.cc2;
	return EC_SUCCESS;
}

static int mock_get_vbus_level(int port)
{
	return mock_tcpc.vbus_level;
}

static int mock_select_rp_value(int port, int rp)
{
	return EC_SUCCESS;
}

static int mock_set_cc(int port, int pull)
{
	return EC_SUCCESS;
}

static int mock_set_polarity(int port, int polarity)
{
	return EC_SUCCESS;
}

static int mock_set_vconn(int port, int enable)
{
	return EC_SUCCESS;
}

static int mock_set_msg_header(int port, int power_role, int data_role)
{
	++mock_tcpc.num_calls_to_set_header;

	mock_tcpc.power_role = power_role;
	mock_tcpc.data_role = data_role;

	if (!mock_tcpc.should_print_header_changes)
		return EC_SUCCESS;

	ccprints("Setting TCPC header to %s %s",
		power_role == PD_ROLE_SOURCE ? "SRC" : "SNK",
		data_role == PD_ROLE_UFP ? "UFP" : "DFP");

	return EC_SUCCESS;
}

static int mock_set_rx_enable(int port, int enable)
{
	return EC_SUCCESS;
}

static int mock_get_message_raw(int port, uint32_t *payload, int *head)
{
	return EC_SUCCESS;
}

static int mock_transmit(int port,
	enum tcpm_transmit_type type, uint16_t header, const uint32_t *data)
{
	return EC_SUCCESS;
}

void mock_tcpc_alert(int port)
{
}

void mock_tcpc_discharge_vbus(int port, int enable)
{
}

__maybe_unused static int mock_drp_toggle(int port)
{
	return EC_SUCCESS;
}

static int mock_get_chip_info(int port, int live,
	struct ec_response_pd_chip_info_v1 **info)
{
	return EC_SUCCESS;
}

__maybe_unused static int mock_set_snk_ctrl(int port, int enable)
{
	return EC_SUCCESS;
}

__maybe_unused static int mock_set_src_ctrl(int port, int enable)
{
	return EC_SUCCESS;
}

__maybe_unused static int mock_enter_low_power_mode(int port)
{
	return EC_SUCCESS;
}

void mock_set_frs_enable(int port, int enable)
{
}

const struct tcpm_drv mock_tcpc_driver = {
	.init  = &mock_init,
	.release  = &mock_release,
	.get_cc  = &mock_get_cc,
	.get_vbus_level  = &mock_get_vbus_level,
	.select_rp_value  = &mock_select_rp_value,
	.set_cc  = &mock_set_cc,
	.set_polarity  = &mock_set_polarity,
	.set_vconn  = &mock_set_vconn,
	.set_msg_header  = &mock_set_msg_header,
	.set_rx_enable  = &mock_set_rx_enable,
	.get_message_raw  = &mock_get_message_raw,
	.transmit  = &mock_transmit,
	.tcpc_alert  = &mock_tcpc_alert,
	.tcpc_discharge_vbus  = &mock_tcpc_discharge_vbus,
#ifdef CONFIG_USB_PD_DUAL_ROLE_AUTO_TOGGLE
	.drp_toggle  = &mock_drp_toggle,
#endif
	.get_chip_info  = &mock_get_chip_info,
#ifdef CONFIG_USBC_PPC
	.set_snk_ctrl  = &mock_set_snk_ctrl,
	.set_src_ctrl  = &mock_set_src_ctrl,
#endif
#ifdef CONFIG_USB_PD_TCPC_LOW_POWER
	.enter_low_power_mode  = &mock_enter_low_power_mode,
#endif
	.set_frs_enable  = &mock_set_frs_enable,
};
