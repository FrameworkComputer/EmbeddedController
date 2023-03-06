/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "charge_manager.h"
#include "chipset.h"
#include "ec_commands.h"
#include "usb_charge.h"
#include "usb_dp_alt_mode.h"
#include "usb_pd.h"
#include "usbc_ppc.h"

#include <zephyr/drivers/gpio/gpio_emul.h>
#include <zephyr/fff.h>
#include <zephyr/ztest.h>

FAKE_VALUE_FUNC(int, chipset_in_state, int);
FAKE_VALUE_FUNC(int, ppc_vbus_source_enable, int, int);
FAKE_VOID_FUNC(pd_set_vbus_discharge, int, int);
FAKE_VOID_FUNC(pd_send_host_event, int);
FAKE_VALUE_FUNC(int, ppc_vbus_sink_enable, int, int);
FAKE_VALUE_FUNC(int, ppc_is_sourcing_vbus, int);

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

int ppc_is_sourcing_vbus_mock(int port)
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
	RESET_FAKE(ppc_is_sourcing_vbus);
	RESET_FAKE(ppc_vbus_sink_enable);
}

ZTEST_USER(usb_pd_policy, test_pd_check_vconn_swap)
{
	const struct gpio_dt_spec *const en_z1_rails =
		GPIO_DT_FROM_NODELABEL(gpio_en_z1_rails);

	/* AP 5V rail is off. */
	zassert_false(gpio_pin_get_dt(en_z1_rails));
	zassert_false(pd_check_vconn_swap(0));
	zassert_false(pd_check_vconn_swap(1));

	/*
	 * Case with the rail on is untestable because emulated GPIOs don't
	 * allow getting the current value of output pins.
	 */
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
	ppc_is_sourcing_vbus_fake.custom_fake = ppc_is_sourcing_vbus_mock;
	zassert_true(board_vbus_source_enabled(0), NULL, NULL);
	zassert_equal(1, ppc_is_sourcing_vbus_fake.call_count);
}

ZTEST_USER(usb_pd_policy, test_board_is_sourcing_vbus)
{
	ppc_is_sourcing_vbus_fake.custom_fake = ppc_is_sourcing_vbus_mock;
	zassert_true(board_is_sourcing_vbus(0), NULL, NULL);
	zassert_equal(1, ppc_is_sourcing_vbus_fake.call_count);
}

ZTEST_USER(usb_pd_policy, test_uhbr13_5_not_allowed)
{
	zassert_false(board_is_dp_uhbr13_5_allowed(0), NULL, NULL);
}
ZTEST_SUITE(usb_pd_policy, NULL, NULL, usb_pd_policy_before, NULL, NULL);
