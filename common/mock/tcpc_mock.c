/* Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
/* Mock for the TCPC interface */

#include "common.h"
#include "console.h"
#include "memory.h"
#include "mock/tcpc_mock.h"
#include "test_util.h"
#include "tests/enum_strings.h"
#include "timer.h"
#include "usb_pd_tcpm.h"

#ifndef CONFIG_COMMON_RUNTIME
#define cprints(format, args...)
#endif

/* Public API for controlling/inspecting this mock */
struct mock_tcpc_ctrl mock_tcpc;

void mock_tcpc_reset(void)
{
	/* Reset all control values to 0. See also build assert below */
	memset(&mock_tcpc, 0, sizeof(mock_tcpc));

	/* Reset all last viewed variables to -1 to make them invalid  */
	memset(&mock_tcpc.last, 0xff, sizeof(mock_tcpc.last));
}
BUILD_ASSERT(TYPEC_CC_VOLT_OPEN == 0, "Ensure Open is 0-value for memset");

static int mock_init(int port)
{
	return EC_SUCCESS;
}

static int mock_release(int port)
{
	return EC_SUCCESS;
}

static int mock_get_cc(int port, enum tcpc_cc_voltage_status *cc1,
		       enum tcpc_cc_voltage_status *cc2)
{
	*cc1 = mock_tcpc.cc1;
	*cc2 = mock_tcpc.cc2;
	return EC_SUCCESS;
}

static bool mock_check_vbus_level(int port, enum vbus_level level)
{
	if (level == VBUS_PRESENT)
		return mock_tcpc.vbus_level;
	else if (level == VBUS_SAFE0V || level == VBUS_REMOVED)
		return !mock_tcpc.vbus_level;

	/*
	 * Unknown vbus_level was added, force a failure.
	 * Note that TCPC drivers and pd_check_vbus_level() implementations
	 * should be carefully checked on new level additions in case they
	 * need updated.
	 */
	ccprints("[TCPC] Unhandled Vbus check %d", level);
	TEST_ASSERT(0);
}

static int mock_select_rp_value(int port, int rp)
{
	mock_tcpc.last.rp = rp;

	if (!mock_tcpc.should_print_call)
		return EC_SUCCESS;

	ccprints("[TCPC] Setting TCPM-side Rp to %s", from_tcpc_rp_value(rp));

	return EC_SUCCESS;
}

static int mock_set_cc(int port, int pull)
{
	mock_tcpc.last.cc = pull;

	if (mock_tcpc.callbacks.set_cc)
		mock_tcpc.callbacks.set_cc(port, pull);

	if (!mock_tcpc.should_print_call)
		return EC_SUCCESS;

	ccprints("[TCPC] Setting TCPM-side CC to %s", from_tcpc_cc_pull(pull));

	return EC_SUCCESS;
}

static int mock_set_polarity(int port, enum tcpc_cc_polarity polarity)
{
	mock_tcpc.last.polarity = polarity;

	if (!mock_tcpc.should_print_call)
		return EC_SUCCESS;

	ccprints("[TCPC] Setting TCPM-side polarity to %s",
		 from_tcpc_cc_polarity(polarity));

	return EC_SUCCESS;
}

static int mock_set_vconn(int port, int enable)
{
	return EC_SUCCESS;
}

static int mock_set_msg_header(int port, int power_role, int data_role)
{
	++mock_tcpc.num_calls_to_set_header;

	mock_tcpc.last.power_role = power_role;
	mock_tcpc.last.data_role = data_role;

	if (!mock_tcpc.should_print_call)
		return EC_SUCCESS;

	ccprints("[TCPC] Setting TCPM-side header to %s %s",
		 from_pd_power_role(power_role),
		 from_pd_data_role(data_role));

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

static int mock_transmit(int port, enum tcpm_transmit_type type,
			 uint16_t header, const uint32_t *data)
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
	/* Only set the time the first time this is called. */
	if (mock_tcpc.first_call_to_enable_auto_toggle == 0)
		mock_tcpc.first_call_to_enable_auto_toggle = get_time().val;

	if (!mock_tcpc.should_print_call)
		return EC_SUCCESS;

	ccprints("[TCPC] Enabling Auto Toggle");

	return EC_SUCCESS;
}

static int mock_get_chip_info(int port, int live,
			      struct ec_response_pd_chip_info_v1 *info)
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

int mock_set_frs_enable(int port, int enable)
{
	return EC_SUCCESS;
}

const struct tcpm_drv mock_tcpc_driver = {
	.init = &mock_init,
	.release = &mock_release,
	.get_cc = &mock_get_cc,
	.check_vbus_level = &mock_check_vbus_level,
	.select_rp_value = &mock_select_rp_value,
	.set_cc = &mock_set_cc,
	.set_polarity = &mock_set_polarity,
	.set_vconn = &mock_set_vconn,
	.set_msg_header = &mock_set_msg_header,
	.set_rx_enable = &mock_set_rx_enable,
	.get_message_raw = &mock_get_message_raw,
	.transmit = &mock_transmit,
	.tcpc_alert = &mock_tcpc_alert,
	.tcpc_discharge_vbus = &mock_tcpc_discharge_vbus,
#ifdef CONFIG_USB_PD_DUAL_ROLE_AUTO_TOGGLE
	.drp_toggle = &mock_drp_toggle,
#endif
	.get_chip_info = &mock_get_chip_info,
#ifdef CONFIG_USBC_PPC
	.set_snk_ctrl = &mock_set_snk_ctrl,
	.set_src_ctrl = &mock_set_src_ctrl,
#endif
#ifdef CONFIG_USB_PD_TCPC_LOW_POWER
	.enter_low_power_mode = &mock_enter_low_power_mode,
#endif
#ifdef CONFIG_USB_PD_FRS_TCPC
	.set_frs_enable = &mock_set_frs_enable,
#endif
};
