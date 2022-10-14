/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <zephyr/fff.h>
#include <zephyr/ztest.h>

#include "charge_manager.h"
#include "chipset.h"
#include "ec_commands.h"
#include "usb_pd.h"
#include "usbc_ppc.h"

DECLARE_FAKE_VALUE_FUNC(int, chipset_in_state, int);
DEFINE_FAKE_VALUE_FUNC(int, chipset_in_state, int);
DECLARE_FAKE_VALUE_FUNC(int, ppc_vbus_source_enable, int, int);
DEFINE_FAKE_VALUE_FUNC(int, ppc_vbus_source_enable, int, int);
DECLARE_FAKE_VOID_FUNC(pd_set_vbus_discharge, int, int);
DEFINE_FAKE_VOID_FUNC(pd_set_vbus_discharge, int, int);
DECLARE_FAKE_VOID_FUNC(pd_send_host_event, int);
DEFINE_FAKE_VOID_FUNC(pd_send_host_event, int);
DECLARE_FAKE_VALUE_FUNC(bool, tcpm_get_src_ctrl, int);
DEFINE_FAKE_VALUE_FUNC(bool, tcpm_get_src_ctrl, int);
DECLARE_FAKE_VALUE_FUNC(int, ppc_vbus_sink_enable, int, int);
DEFINE_FAKE_VALUE_FUNC(int, ppc_vbus_sink_enable, int, int);

int chipset_in_state_mock(int state_mask)
{
	return state_mask == (CHIPSET_STATE_ANY_SUSPEND | CHIPSET_STATE_ON);
}

int ppc_vbus_source_enable_0_mock(int port, int enable)
{
	zassert_equal(0, pd_set_vbus_discharge_fake.call_count);
	zassert_equal(0, pd_send_host_event_fake.call_count);
	return (port == 0) && (enable == 0);
}

int ppc_vbus_source_enable_1_mock(int port, int enable)
{
	zassert_equal(1, enable, NULL);
	switch (port) {
	case 0:
		return EC_SUCCESS;
	case 3:
		return EC_ERROR_UNIMPLEMENTED;
	case 4:
		return EC_ERROR_INVAL;
	default:
		zassert_unreachable("Unknown port");
	}
	return -1;
}

void pd_set_vbus_discharge_enable_0_mock(int port, int enable)
{
	zassert_equal(0, port, NULL);
	zassert_equal(0, enable, NULL);
}

void pd_set_vbus_discharge_port_3_enable_0_mock(int port, int enable)
{
	zassert_equal(3, port, NULL);
	zassert_equal(0, enable, NULL);
}

void pd_set_vbus_discharge_port_4_enable_0_mock(int port, int enable)
{
	zassert_equal(4, port, NULL);
	zassert_equal(0, enable, NULL);
}

void pd_set_vbus_discharge_enable_1_mock(int port, int enable)
{
	zassert_equal(0, pd_send_host_event_fake.call_count);
	zassert_equal(0, port, NULL);
	zassert_equal(1, enable, NULL);
}

void pd_send_host_event_mock(int mask)
{
	zassert_equal(PD_EVENT_POWER_CHANGE, mask, NULL);
}

bool tcpm_get_src_ctrl_mock(int port)
{
	return port == 0;
}

int ppc_vbus_sink_enable_mock(int port, int enable)
{
	zassert_equal(0, enable, NULL);
	switch (port) {
	case 0:
	case 3:
	case 4:
		return EC_SUCCESS;
	case 1:
		return EC_ERROR_UNIMPLEMENTED;
	case 2:
		return EC_ERROR_INVAL;
	default:
		zassert_unreachable("Unknown port");
	}
	return -1;
}

static void usb_pd_policy_before(void *fixture)
{
	ARG_UNUSED(fixture);
	RESET_FAKE(chipset_in_state);
	RESET_FAKE(ppc_vbus_source_enable);
	RESET_FAKE(pd_set_vbus_discharge);
	RESET_FAKE(pd_send_host_event);
	RESET_FAKE(tcpm_get_src_ctrl);
	RESET_FAKE(ppc_vbus_sink_enable);
}

ZTEST_USER(usb_pd_policy, test_pd_check_vconn_swap)
{
	chipset_in_state_fake.custom_fake = chipset_in_state_mock;
	zassert_true(pd_check_vconn_swap(0), NULL, NULL);
	zassert_equal(1, chipset_in_state_fake.call_count);
}

ZTEST_USER(usb_pd_policy, test_pd_power_supply_reset)
{
	ppc_vbus_source_enable_fake.custom_fake = ppc_vbus_source_enable_0_mock;
	pd_set_vbus_discharge_fake.custom_fake =
		pd_set_vbus_discharge_enable_1_mock;
	pd_send_host_event_fake.custom_fake = pd_send_host_event_mock;
	pd_power_supply_reset(0);
	zassert_equal(1, ppc_vbus_source_enable_fake.call_count);
	zassert_equal(1, pd_set_vbus_discharge_fake.call_count);
	zassert_equal(1, pd_send_host_event_fake.call_count);
}

ZTEST_USER(usb_pd_policy, test_pd_set_power_supply_ready_case_0)
{
	ppc_vbus_sink_enable_fake.custom_fake = ppc_vbus_sink_enable_mock;
	pd_set_vbus_discharge_fake.custom_fake =
		pd_set_vbus_discharge_enable_0_mock;
	ppc_vbus_source_enable_fake.custom_fake = ppc_vbus_source_enable_1_mock;
	pd_send_host_event_fake.custom_fake = pd_send_host_event_mock;
	zassert_equal(EC_SUCCESS, pd_set_power_supply_ready(0), NULL);
	zassert_equal(1, ppc_vbus_sink_enable_fake.call_count);
	zassert_equal(1, pd_set_vbus_discharge_fake.call_count);
	zassert_equal(1, ppc_vbus_source_enable_fake.call_count);
	zassert_equal(1, pd_send_host_event_fake.call_count);
}

ZTEST_USER(usb_pd_policy, test_pd_set_power_supply_ready_case_1)
{
	ppc_vbus_sink_enable_fake.custom_fake = ppc_vbus_sink_enable_mock;
	pd_set_vbus_discharge_fake.custom_fake =
		pd_set_vbus_discharge_enable_0_mock;
	ppc_vbus_source_enable_fake.custom_fake = ppc_vbus_source_enable_1_mock;
	pd_send_host_event_fake.custom_fake = pd_send_host_event_mock;
	zassert_equal(EC_ERROR_UNIMPLEMENTED, pd_set_power_supply_ready(1),
		      NULL);
	zassert_equal(1, ppc_vbus_sink_enable_fake.call_count);
	zassert_equal(0, pd_set_vbus_discharge_fake.call_count);
	zassert_equal(0, ppc_vbus_source_enable_fake.call_count);
	zassert_equal(0, pd_send_host_event_fake.call_count);
}

ZTEST_USER(usb_pd_policy, test_pd_set_power_supply_ready_case_2)
{
	ppc_vbus_sink_enable_fake.custom_fake = ppc_vbus_sink_enable_mock;
	pd_set_vbus_discharge_fake.custom_fake =
		pd_set_vbus_discharge_enable_0_mock;
	ppc_vbus_source_enable_fake.custom_fake = ppc_vbus_source_enable_1_mock;
	pd_send_host_event_fake.custom_fake = pd_send_host_event_mock;
	zassert_equal(EC_ERROR_INVAL, pd_set_power_supply_ready(2), NULL);
	zassert_equal(1, ppc_vbus_sink_enable_fake.call_count);
	zassert_equal(0, pd_set_vbus_discharge_fake.call_count);
	zassert_equal(0, ppc_vbus_source_enable_fake.call_count);
	zassert_equal(0, pd_send_host_event_fake.call_count);
}

ZTEST_USER(usb_pd_policy, test_pd_set_power_supply_ready_case_3)
{
	ppc_vbus_sink_enable_fake.custom_fake = ppc_vbus_sink_enable_mock;
	pd_set_vbus_discharge_fake.custom_fake =
		pd_set_vbus_discharge_port_3_enable_0_mock;
	ppc_vbus_source_enable_fake.custom_fake = ppc_vbus_source_enable_1_mock;
	pd_send_host_event_fake.custom_fake = pd_send_host_event_mock;
	zassert_equal(EC_ERROR_UNIMPLEMENTED, pd_set_power_supply_ready(3),
		      NULL);
	zassert_equal(1, ppc_vbus_sink_enable_fake.call_count);
	zassert_equal(1, pd_set_vbus_discharge_fake.call_count);
	zassert_equal(1, ppc_vbus_source_enable_fake.call_count);
	zassert_equal(0, pd_send_host_event_fake.call_count);
}

ZTEST_USER(usb_pd_policy, test_pd_set_power_supply_ready_case_4)
{
	ppc_vbus_sink_enable_fake.custom_fake = ppc_vbus_sink_enable_mock;
	pd_set_vbus_discharge_fake.custom_fake =
		pd_set_vbus_discharge_port_4_enable_0_mock;
	ppc_vbus_source_enable_fake.custom_fake = ppc_vbus_source_enable_1_mock;
	pd_send_host_event_fake.custom_fake = pd_send_host_event_mock;
	zassert_equal(EC_ERROR_INVAL, pd_set_power_supply_ready(4), NULL);
	zassert_equal(1, ppc_vbus_sink_enable_fake.call_count);
	zassert_equal(1, pd_set_vbus_discharge_fake.call_count);
	zassert_equal(1, ppc_vbus_source_enable_fake.call_count);
	zassert_equal(0, pd_send_host_event_fake.call_count);
}

ZTEST_USER(usb_pd_policy, test_board_vbus_source_enabled)
{
	tcpm_get_src_ctrl_fake.custom_fake = tcpm_get_src_ctrl_mock;
	zassert_true(board_vbus_source_enabled(0), NULL, NULL);
	zassert_equal(1, tcpm_get_src_ctrl_fake.call_count);
}

ZTEST_USER(usb_pd_policy, test_board_is_sourcing_vbus)
{
	tcpm_get_src_ctrl_fake.custom_fake = tcpm_get_src_ctrl_mock;
	zassert_true(board_is_sourcing_vbus(0), NULL, NULL);
	zassert_equal(1, tcpm_get_src_ctrl_fake.call_count);
}

ZTEST_SUITE(usb_pd_policy, NULL, NULL, usb_pd_policy_before, NULL, NULL);
