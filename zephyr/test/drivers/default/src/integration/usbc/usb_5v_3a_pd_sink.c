/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "battery_smart.h"
#include "emul/emul_isl923x.h"
#include "emul/emul_smart_battery.h"
#include "emul/tcpc/emul_tcpci_partner_snk.h"
#include "tcpm/tcpci.h"
#include "test/drivers/test_state.h"
#include "test/drivers/utils.h"
#include "timer.h"
#include "usb_pd.h"

#include <stdint.h>

#include <zephyr/ztest.h>

struct usb_attach_5v_3a_pd_sink_fixture {
	struct tcpci_partner_data sink_5v_3a;
	struct tcpci_snk_emul_data snk_ext;
	const struct emul *tcpci_emul;
	const struct emul *charger_emul;
};

/* Chromebooks only charge PD partners at 5v */
#define TEST_SRC_PORT_VBUS_MV 5000
#define TEST_SRC_PORT_TARGET_MA 3000

#define TEST_INITIAL_SINK_CAP \
	PDO_FIXED(TEST_SRC_PORT_VBUS_MV, TEST_SRC_PORT_TARGET_MA, 0)
/* Only used to verify sink capabilities being received by SRC port */
#define TEST_ADDITIONAL_SINK_CAP PDO_FIXED(TEST_SRC_PORT_VBUS_MV, 5000, 0)

static void *usb_attach_5v_3a_pd_sink_setup(void)
{
	static struct usb_attach_5v_3a_pd_sink_fixture test_fixture;

	/* Get references for the emulators */
	test_fixture.tcpci_emul = EMUL_GET_USBC_BINDING(0, tcpc);
	test_fixture.charger_emul = EMUL_GET_USBC_BINDING(0, chg);

	return &test_fixture;
}

static void usb_attach_5v_3a_pd_sink_before(void *data)
{
	struct usb_attach_5v_3a_pd_sink_fixture *test_fixture = data;

	/* Set chipset to ON, this will set TCPM to DRP */
	test_set_chipset_to_s0();

	/* TODO(b/214401892): Check why need to give time TCPM to spin */
	k_sleep(K_SECONDS(1));

	/* Initialized the sink to request 5V and 3A */
	tcpci_partner_init(&test_fixture->sink_5v_3a, PD_REV20);
	test_fixture->sink_5v_3a.extensions = tcpci_snk_emul_init(
		&test_fixture->snk_ext, &test_fixture->sink_5v_3a, NULL);
	test_fixture->snk_ext.pdo[0] = TEST_INITIAL_SINK_CAP;
	test_fixture->snk_ext.pdo[1] = TEST_ADDITIONAL_SINK_CAP;
	connect_sink_to_port(&test_fixture->sink_5v_3a,
			     test_fixture->tcpci_emul,
			     test_fixture->charger_emul);
}

static void usb_attach_5v_3a_pd_sink_after(void *data)
{
	struct usb_attach_5v_3a_pd_sink_fixture *test_fixture = data;

	disconnect_sink_from_port(test_fixture->tcpci_emul);
}

ZTEST_SUITE(usb_attach_5v_3a_pd_sink, drivers_predicate_post_main,
	    usb_attach_5v_3a_pd_sink_setup, usb_attach_5v_3a_pd_sink_before,
	    usb_attach_5v_3a_pd_sink_after, NULL);

ZTEST_F(usb_attach_5v_3a_pd_sink, test_partner_pd_completed)
{
	zassert_true(fixture->snk_ext.pd_completed);
}

ZTEST(usb_attach_5v_3a_pd_sink, test_battery_is_discharging)
{
	const struct emul *emul = EMUL_DT_GET(DT_NODELABEL(battery));
	uint16_t battery_status;

	zassert_ok(sbat_emul_get_word_val(emul, SB_BATTERY_STATUS,
					  &battery_status));
	zassert_equal(battery_status & STATUS_DISCHARGING, STATUS_DISCHARGING,
		      "Battery is not discharging: %d", battery_status);
}

ZTEST(usb_attach_5v_3a_pd_sink, test_typec_status)
{
	struct ec_response_typec_status status = host_cmd_typec_status(0);

	zassert_true(status.pd_enabled, "PD is disabled");
	zassert_true(status.dev_connected, "Device disconnected");
	zassert_true(status.sop_connected, "Charger is not SOP capable");
	zassert_equal(status.sink_cap_count, 2,
		      "Expected 2 sink PDOs, but got %d",
		      status.sink_cap_count);
	zassert_equal(status.power_role, PD_ROLE_SOURCE,
		      "Expected power role to be %d, but got %d",
		      PD_ROLE_SOURCE, status.power_role);
}

ZTEST(usb_attach_5v_3a_pd_sink, test_power_info)
{
	struct ec_response_usb_pd_power_info info = host_cmd_power_info(0);

	zassert_equal(info.role, USB_PD_PORT_POWER_SOURCE,
		      "Expected role to be %d, but got %d",
		      USB_PD_PORT_POWER_SOURCE, info.role);
	zassert_equal(info.type, USB_CHG_TYPE_NONE,
		      "Expected type to be %d, but got %d", USB_CHG_TYPE_NONE,
		      info.type);
	zassert_equal(info.meas.voltage_max, 0,
		      "Expected charge voltage max of 0mV, but got %dmV",
		      info.meas.voltage_max);
	zassert_within(
		info.meas.voltage_now, TEST_SRC_PORT_VBUS_MV, 500,
		"Charging voltage expected to be near 5000mV, but was %dmV",
		info.meas.voltage_now);
	zassert_equal(info.meas.current_max, TEST_SRC_PORT_TARGET_MA,
		      "Current max expected to be 1500mV, but was %dmV",
		      info.meas.current_max);
	zassert_equal(info.meas.current_lim, 0,
		      "VBUS max is set to 0mA, but PD is reporting %dmA",
		      info.meas.current_lim);
	zassert_equal(info.max_power, 0,
		      "Charging expected to be at %duW, but PD max is %duW", 0,
		      info.max_power);
}

ZTEST_F(usb_attach_5v_3a_pd_sink, test_disconnect_battery_discharging)
{
	const struct emul *emul = EMUL_DT_GET(DT_NODELABEL(battery));
	uint16_t battery_status;

	disconnect_sink_from_port(fixture->tcpci_emul);
	zassert_ok(sbat_emul_get_word_val(emul, SB_BATTERY_STATUS,
					  &battery_status));
	zassert_equal(battery_status & STATUS_DISCHARGING, STATUS_DISCHARGING,
		      "Battery is not discharging: %d", battery_status);
}

ZTEST_F(usb_attach_5v_3a_pd_sink, test_disconnect_charge_state)
{
	struct ec_response_charge_state state;

	disconnect_sink_from_port(fixture->tcpci_emul);
	state = host_cmd_charge_state(0);

	zassert_false(state.get_state.ac, "AC_OK not triggered");
	zassert_equal(state.get_state.chg_current, 0,
		      "Max charge current expected 0mA, but was %dmA",
		      state.get_state.chg_current);
	zassert_equal(state.get_state.chg_input_current,
		      CONFIG_PLATFORM_EC_CHARGER_DEFAULT_CURRENT_LIMIT,
		      "Charge input current limit expected %dmA, but was %dmA",
		      CONFIG_PLATFORM_EC_CHARGER_DEFAULT_CURRENT_LIMIT,
		      state.get_state.chg_input_current);
}

ZTEST_F(usb_attach_5v_3a_pd_sink, test_disconnect_typec_status)
{
	struct ec_response_typec_status typec_status;

	disconnect_sink_from_port(fixture->tcpci_emul);
	typec_status = host_cmd_typec_status(0);

	zassert_false(typec_status.pd_enabled);
	zassert_false(typec_status.dev_connected);
	zassert_false(typec_status.sop_connected);
	zassert_equal(typec_status.source_cap_count, 0,
		      "Expected 0 source caps, but got %d",
		      typec_status.source_cap_count);
	zassert_equal(typec_status.power_role, USB_CHG_TYPE_NONE,
		      "Expected power role to be %d, but got %d",
		      USB_CHG_TYPE_NONE, typec_status.power_role);
}

ZTEST_F(usb_attach_5v_3a_pd_sink, test_disconnect_power_info)
{
	struct ec_response_usb_pd_power_info power_info;

	disconnect_sink_from_port(fixture->tcpci_emul);
	power_info = host_cmd_power_info(0);

	zassert_equal(power_info.role, USB_PD_PORT_POWER_DISCONNECTED,
		      "Expected power role to be %d, but got %d",
		      USB_PD_PORT_POWER_DISCONNECTED, power_info.role);
	zassert_equal(power_info.type, USB_CHG_TYPE_NONE,
		      "Expected charger type to be %d, but got %s",
		      USB_CHG_TYPE_NONE, power_info.type);
	zassert_equal(power_info.max_power, 0,
		      "Expected the maximum power to be 0uW, but got %duW",
		      power_info.max_power);
	zassert_equal(power_info.meas.voltage_max, 0,
		      "Expected maximum voltage of 0mV, but got %dmV",
		      power_info.meas.voltage_max);
	zassert_within(power_info.meas.voltage_now, 5, 5,
		       "Expected present voltage near 0mV, but got %dmV",
		       power_info.meas.voltage_now);
	zassert_equal(power_info.meas.current_max, 0,
		      "Expected maximum current of 0mA, but got %dmA",
		      power_info.meas.current_max);
	zassert_true(power_info.meas.current_lim >= 0,
		     "Expected the PD current limit to be >= 0, but got %dmA",
		     power_info.meas.current_lim);
}

/**
 * @brief TestPurpose: Verify GotoMin message.
 *
 * @details
 *  - TCPM is configured initially as Source
 *  - Initiate Goto_Min request
 *  - Verify emulated sink PD negotiation is completed
 *
 * Expected Results
 *  - Sink completes Goto Min PD negotiation
 */
ZTEST_F(usb_attach_5v_3a_pd_sink, test_goto_min)
{
	pd_dpm_request(0, DPM_REQUEST_GOTO_MIN);
	k_sleep(K_SECONDS(1));

	zassert_true(fixture->snk_ext.pd_completed);
}

/**
 * @brief TestPurpose: Verify Ping message.
 *
 * @details
 *  - TCPM is configured initially as Source
 *  - Initiate Ping request
 *  - Verify emulated sink received ping message
 *
 * Expected Results
 *  - Sink received ping message
 */
ZTEST_F(usb_attach_5v_3a_pd_sink, test_ping_msg)
{
	tcpci_snk_emul_clear_ping_received(&fixture->snk_ext);

	pd_dpm_request(0, DPM_REQUEST_SEND_PING);
	k_sleep(K_USEC(PD_T_SOURCE_ACTIVITY));

	zassert_true(fixture->snk_ext.ping_received);
}
