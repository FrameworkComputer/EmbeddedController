/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <stdint.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/ztest.h>

#include "battery_smart.h"
#include "emul/emul_isl923x.h"
#include "emul/emul_smart_battery.h"
#include "emul/tcpc/emul_tcpci_partner_faulty_ext.h"
#include "emul/tcpc/emul_tcpci_partner_snk.h"
#include "tcpm/tcpci.h"
#include "test/drivers/test_state.h"
#include "test/drivers/utils.h"
#include "test/drivers/stubs.h"
#include "usb_pd.h"
#include "usb_tc_sm.h"
#include "timer.h"

/* USB-C port used to connect port partner in this testsuite */
#define TEST_PORT 0
BUILD_ASSERT(TEST_PORT == USBC_PORT_C0);

struct usb_malfunction_sink_fixture {
	struct tcpci_partner_data sink;
	struct tcpci_faulty_ext_data faulty_snk_ext;
	struct tcpci_snk_emul_data snk_ext;
	const struct emul *tcpci_emul;
	const struct emul *charger_emul;
	struct tcpci_faulty_ext_action actions[2];
	enum usbc_port port;
};

static void *usb_malfunction_sink_setup(void)
{
	static struct usb_malfunction_sink_fixture test_fixture;

	test_fixture.port = TEST_PORT;

	/* Get references for the emulators */
	test_fixture.tcpci_emul = EMUL_GET_USBC_BINDING(TEST_PORT, tcpc);
	test_fixture.charger_emul = EMUL_GET_USBC_BINDING(TEST_PORT, chg);

	/* Initialized the sink to request 5V and 3A */
	tcpci_partner_init(&test_fixture.sink, PD_REV20);
	test_fixture.sink.extensions = tcpci_faulty_ext_init(
		&test_fixture.faulty_snk_ext, &test_fixture.sink,
		tcpci_snk_emul_init(&test_fixture.snk_ext, &test_fixture.sink,
				    NULL));
	test_fixture.snk_ext.pdo[1] =
		PDO_FIXED(5000, 3000, PDO_FIXED_UNCONSTRAINED);

	return &test_fixture;
}

static void usb_malfunction_sink_before(void *data)
{
	/* Set chipset to ON, this will set TCPM to DRP */
	test_set_chipset_to_s0();

	/* TODO(b/214401892): Check why need to give time TCPM to spin */
	k_sleep(K_SECONDS(1));
}

static void usb_malfunction_sink_after(void *data)
{
	struct usb_malfunction_sink_fixture *fixture = data;

	tcpci_faulty_ext_clear_actions_list(&fixture->faulty_snk_ext);
	disconnect_sink_from_port(fixture->tcpci_emul);
	tcpci_partner_common_clear_logged_msgs(&fixture->sink);
}

ZTEST_SUITE(usb_malfunction_sink, drivers_predicate_post_main,
	    usb_malfunction_sink_setup, usb_malfunction_sink_before,
	    usb_malfunction_sink_after, NULL);

ZTEST_F(usb_malfunction_sink, test_fail_source_cap_and_pd_disable)
{
	struct ec_response_typec_status typec_status;

	/*
	 * Fail on SourceCapabilities message to make TCPM change PD port state
	 * to disabled
	 */
	fixture->actions[0].action_mask = TCPCI_FAULTY_EXT_FAIL_SRC_CAP;
	fixture->actions[0].count = TCPCI_FAULTY_EXT_INFINITE_ACTION;
	tcpci_faulty_ext_append_action(&fixture->faulty_snk_ext,
				       &fixture->actions[0]);

	connect_sink_to_port(&fixture->sink, fixture->tcpci_emul,
			     fixture->charger_emul);

	typec_status = host_cmd_typec_status(0);

	/* Device is connected, but PD wasn't able to establish contract */
	zassert_true(typec_status.pd_enabled, NULL);
	zassert_true(typec_status.dev_connected, NULL);
	zassert_false(typec_status.sop_connected, NULL);
}

ZTEST_F(usb_malfunction_sink, test_fail_source_cap_and_pd_connect)
{
	struct ec_response_usb_pd_power_info info;
	struct ec_response_typec_status typec_status;

	/*
	 * Fail only few times on SourceCapabilities message to prevent entering
	 * PE_SRC_Disabled state by TCPM
	 */
	fixture->actions[0].action_mask = TCPCI_FAULTY_EXT_FAIL_SRC_CAP;
	fixture->actions[0].count = 3;
	tcpci_faulty_ext_append_action(&fixture->faulty_snk_ext,
				       &fixture->actions[0]);

	connect_sink_to_port(&fixture->sink, fixture->tcpci_emul,
			     fixture->charger_emul);

	typec_status = host_cmd_typec_status(0);

	zassert_true(typec_status.pd_enabled, NULL);
	zassert_true(typec_status.dev_connected, NULL);
	zassert_true(typec_status.sop_connected, NULL);

	info = host_cmd_power_info(0);

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
		info.meas.voltage_now, 5000, 500,
		"Charging voltage expected to be near 5000mV, but was %dmV",
		info.meas.voltage_now);
	zassert_equal(info.meas.current_max, 1500,
		      "Current max expected to be 1500mV, but was %dmV",
		      info.meas.current_max);
	zassert_equal(info.meas.current_lim, 0,
		      "VBUS max is set to 0mA, but PD is reporting %dmA",
		      info.meas.current_lim);
	zassert_equal(info.max_power, 0,
		      "Charging expected to be at %duW, but PD max is %duW", 0,
		      info.max_power);
}

ZTEST_F(usb_malfunction_sink, test_ignore_source_cap)
{
	struct tcpci_partner_log_msg *msg;
	uint16_t header;
	bool expect_hard_reset = false;
	int msg_cnt = 0;

	fixture->actions[0].action_mask = TCPCI_FAULTY_EXT_IGNORE_SRC_CAP;
	fixture->actions[0].count = TCPCI_FAULTY_EXT_INFINITE_ACTION;
	tcpci_faulty_ext_append_action(&fixture->faulty_snk_ext,
				       &fixture->actions[0]);

	tcpci_partner_common_enable_pd_logging(&fixture->sink, true);
	connect_sink_to_port(&fixture->sink, fixture->tcpci_emul,
			     fixture->charger_emul);
	tcpci_partner_common_enable_pd_logging(&fixture->sink, false);

	/*
	 * If test is failing, printing logged message may be useful to diagnose
	 * problem:
	 * tcpci_partner_common_print_logged_msgs(&fixture->sink);
	 */

	/* Check if SourceCapability message alternate with HardReset */
	SYS_SLIST_FOR_EACH_CONTAINER(&fixture->sink.msg_log, msg, node)
	{
		if (expect_hard_reset) {
			zassert_equal(msg->sop, TCPCI_MSG_TX_HARD_RESET,
				      "Expected message %d to be hard reset",
				      msg_cnt);
		} else {
			header = sys_get_le16(msg->buf);
			zassert_equal(
				msg->sop, TCPCI_MSG_SOP,
				"Expected message %d to be SOP message, not 0x%x",
				msg_cnt, msg->sop);
			zassert_not_equal(
				PD_HEADER_CNT(header), 0,
				"Expected message %d to has at least one data object",
				msg_cnt);
			zassert_equal(
				PD_HEADER_TYPE(header), PD_DATA_SOURCE_CAP,
				"Expected message %d to be SourceCapabilities, not 0x%x",
				msg_cnt, PD_HEADER_TYPE(header));
		}

		msg_cnt++;
		expect_hard_reset = !expect_hard_reset;
	}
}

ZTEST_F(usb_malfunction_sink, test_hard_reset_disconnect)
{
	struct ec_response_typec_status typec_status;
	int try_count;

	/*
	 * Test if disconnection during the power sequence doesn't have impact
	 * on next tries
	 */
	for (try_count = 1; try_count < 5; try_count++) {
		/* Connect port partner and check Vconn state */
		connect_sink_to_port(&fixture->sink, fixture->tcpci_emul,
				     fixture->charger_emul);
		typec_status = host_cmd_typec_status(fixture->port);
		zassert_equal(typec_status.vconn_role, PD_ROLE_VCONN_SRC,
			      "Vconn should be present after connection (%d)",
			      try_count);

		/* Send hard reset to trigger power sequence on source side */
		tcpci_partner_common_send_hard_reset(&fixture->sink);

		/*
		 * Wait for start of power sequence after hard reset and half
		 * the time of source recovery (first step of power sequence
		 * when vconn should be disabled)
		 */
		k_sleep(K_USEC(PD_T_PS_HARD_RESET + PD_T_SRC_RECOVER / 2));

		typec_status = host_cmd_typec_status(fixture->port);
		zassert_equal(typec_status.vconn_role, PD_ROLE_VCONN_OFF,
			      "Vconn should be disabled at power sequence (%d)",
			      try_count);

		/* Disconnect partner at the middle of power sequence */
		disconnect_sink_from_port(fixture->tcpci_emul);
	}
}

ZTEST_F(usb_malfunction_sink, test_ignore_source_cap_and_pd_disable)
{
	struct ec_response_typec_status typec_status;

	/*
	 * Ignore first SourceCapabilities message and discard others by sending
	 * different messages. This will lead to PD disable.
	 */
	fixture->actions[0].action_mask = TCPCI_FAULTY_EXT_IGNORE_SRC_CAP;
	fixture->actions[0].count = 1;
	tcpci_faulty_ext_append_action(&fixture->faulty_snk_ext,
				       &fixture->actions[0]);
	fixture->actions[1].action_mask = TCPCI_FAULTY_EXT_DISCARD_SRC_CAP;
	fixture->actions[1].count = TCPCI_FAULTY_EXT_INFINITE_ACTION;
	tcpci_faulty_ext_append_action(&fixture->faulty_snk_ext,
				       &fixture->actions[1]);

	connect_sink_to_port(&fixture->sink, fixture->tcpci_emul,
			     fixture->charger_emul);

	typec_status = host_cmd_typec_status(0);

	/* Device is connected, but PD wasn't able to establish contract */
	zassert_true(typec_status.pd_enabled, NULL);
	zassert_true(typec_status.dev_connected, NULL);
	zassert_false(typec_status.sop_connected, NULL);
}
