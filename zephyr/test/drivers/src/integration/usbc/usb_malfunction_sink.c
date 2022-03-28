/* Copyright 2022 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <stdint.h>
#include <zephyr/sys/byteorder.h>
#include <ztest.h>

#include "battery_smart.h"
#include "emul/emul_isl923x.h"
#include "emul/emul_smart_battery.h"
#include "emul/tcpc/emul_tcpci_partner_faulty_snk.h"
#include "emul/tcpc/emul_tcpci_partner_snk.h"
#include "tcpm/tcpci.h"
#include "test/drivers/test_state.h"
#include "test/drivers/utils.h"
#include "usb_pd.h"

struct usb_malfunction_sink_fixture {
	struct tcpci_partner_data sink;
	struct tcpci_faulty_snk_emul_data faulty_snk_ext;
	struct tcpci_snk_emul_data snk_ext;
	const struct emul *tcpci_emul;
	const struct emul *charger_emul;
	struct tcpci_faulty_snk_action actions[2];
};

static void
connect_sink_to_port(struct usb_malfunction_sink_fixture *fixture)
{
	/*
	 * TODO(b/221439302) Updating the TCPCI emulator registers, updating the
	 *   vbus, as well as alerting should all be a part of the connect
	 *   function.
	 */
	isl923x_emul_set_adc_vbus(fixture->charger_emul, 0);
	tcpci_emul_set_reg(fixture->tcpci_emul, TCPC_REG_POWER_STATUS,
			   TCPC_REG_POWER_STATUS_VBUS_DET);
	tcpci_emul_set_reg(fixture->tcpci_emul, TCPC_REG_EXT_STATUS,
			   TCPC_REG_EXT_STATUS_SAFE0V);
	tcpci_tcpc_alert(0);
	/*
	 * TODO(b/226567798) Wait for TCPC init and DRPToggle. It is required,
	 *   because tcpci_emul_reset_rule_before reset registers including
	 *   Looking4Connection bit in CC_STATUS register.
	 */
	k_sleep(K_SECONDS(1));
	zassume_ok(tcpci_partner_connect_to_tcpci(&fixture->sink,
						  fixture->tcpci_emul),
		   NULL);

	/* Wait for PD negotiation and current ramp.
	 * TODO(b/213906889): Check message timing and contents.
	 */
	k_sleep(K_SECONDS(10));
}

static inline void disconnect_sink_from_port(
	struct usb_malfunction_sink_fixture *fixture)
{
	zassume_ok(tcpci_emul_disconnect_partner(fixture->tcpci_emul), NULL);
	k_sleep(K_SECONDS(1));
}

static void *usb_malfunction_sink_setup(void)
{
	static struct usb_malfunction_sink_fixture test_fixture;

	/* Get references for the emulators */
	test_fixture.tcpci_emul =
		emul_get_binding(DT_LABEL(DT_NODELABEL(tcpci_emul)));
	test_fixture.charger_emul =
		emul_get_binding(DT_LABEL(DT_NODELABEL(isl923x_emul)));
	tcpci_emul_set_rev(test_fixture.tcpci_emul, TCPCI_EMUL_REV2_0_VER1_1);
	tcpc_config[0].flags = tcpc_config[0].flags |
			       TCPC_FLAGS_TCPCI_REV2_0;

	/* Initialized the sink to request 5V and 3A */
	tcpci_partner_init(&test_fixture.sink, PD_REV20);
	test_fixture.sink.extensions =
		tcpci_faulty_snk_emul_init(
			&test_fixture.faulty_snk_ext, &test_fixture.sink,
			tcpci_snk_emul_init(&test_fixture.snk_ext,
					    &test_fixture.sink, NULL));
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

	tcpci_faulty_snk_emul_clear_actions_list(&fixture->faulty_snk_ext);
	disconnect_sink_from_port(fixture);
	tcpci_partner_common_clear_logged_msgs(&fixture->sink);
}

ZTEST_SUITE(usb_malfunction_sink, drivers_predicate_post_main,
	    usb_malfunction_sink_setup,
	    usb_malfunction_sink_before,
	    usb_malfunction_sink_after, NULL);

ZTEST_F(usb_malfunction_sink, test_fail_source_cap_and_pd_disable)
{
	struct ec_response_typec_status typec_status;

	/*
	 * Fail on SourceCapabilities message to make TCPM change PD port state
	 * to disabled
	 */
	this->actions[0].action_mask = TCPCI_FAULTY_SNK_FAIL_SRC_CAP;
	this->actions[0].count = TCPCI_FAULTY_SNK_INFINITE_ACTION;
	tcpci_faulty_snk_emul_append_action(&this->faulty_snk_ext,
					    &this->actions[0]);

	connect_sink_to_port(this);

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
	this->actions[0].action_mask = TCPCI_FAULTY_SNK_FAIL_SRC_CAP;
	this->actions[0].count = 3;
	tcpci_faulty_snk_emul_append_action(&this->faulty_snk_ext,
					    &this->actions[0]);

	connect_sink_to_port(this);

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
		      "Charging expected to be at %duW, but PD max is %duW",
		      0, info.max_power);
}

ZTEST_F(usb_malfunction_sink, test_ignore_source_cap)
{
	struct tcpci_partner_log_msg *msg;
	uint16_t header;
	bool expect_hard_reset = false;
	int msg_cnt = 0;

	this->actions[0].action_mask = TCPCI_FAULTY_SNK_IGNORE_SRC_CAP;
	this->actions[0].count = TCPCI_FAULTY_SNK_INFINITE_ACTION;
	tcpci_faulty_snk_emul_append_action(&this->faulty_snk_ext,
					    &this->actions[0]);

	tcpci_partner_common_enable_pd_logging(&this->sink, true);
	connect_sink_to_port(this);
	tcpci_partner_common_enable_pd_logging(&this->sink, false);

	/*
	 * If test is failing, printing logged message may be useful to diagnose
	 * problem:
	 * tcpci_partner_common_print_logged_msgs(&this->sink);
	 */

	/* Check if SourceCapability message alternate with HardReset */
	SYS_SLIST_FOR_EACH_CONTAINER(&this->sink.msg_log, msg, node) {
		if (expect_hard_reset) {
			zassert_equal(msg->sop, TCPCI_MSG_TX_HARD_RESET,
				      "Expected message %d to be hard reset",
				      msg_cnt);
		} else {
			header = sys_get_le16(msg->buf);
			zassert_equal(msg->sop, TCPCI_MSG_SOP,
				      "Expected message %d to be SOP message, not 0x%x",
				      msg_cnt, msg->sop);
			zassert_not_equal(PD_HEADER_CNT(header), 0,
					  "Expected message %d to has at least one data object",
					  msg_cnt);
			zassert_equal(PD_HEADER_TYPE(header),
				      PD_DATA_SOURCE_CAP,
				      "Expected message %d to be SourceCapabilities, not 0x%x",
				      msg_cnt, PD_HEADER_TYPE(header));
		}

		msg_cnt++;
		expect_hard_reset = !expect_hard_reset;
	}
}

ZTEST_F(usb_malfunction_sink, test_ignore_source_cap_and_pd_disable)
{
	struct ec_response_typec_status typec_status;

	/*
	 * Ignore first SourceCapabilities message and discard others by sending
	 * different messages. This will lead to PD disable.
	 */
	this->actions[0].action_mask = TCPCI_FAULTY_SNK_IGNORE_SRC_CAP;
	this->actions[0].count = 1;
	tcpci_faulty_snk_emul_append_action(&this->faulty_snk_ext,
					    &this->actions[0]);
	this->actions[1].action_mask = TCPCI_FAULTY_SNK_DISCARD_SRC_CAP;
	this->actions[1].count = TCPCI_FAULTY_SNK_INFINITE_ACTION;
	tcpci_faulty_snk_emul_append_action(&this->faulty_snk_ext,
					    &this->actions[1]);

	connect_sink_to_port(this);

	typec_status = host_cmd_typec_status(0);

	/* Device is connected, but PD wasn't able to establish contract */
	zassert_true(typec_status.pd_enabled, NULL);
	zassert_true(typec_status.dev_connected, NULL);
	zassert_false(typec_status.sop_connected, NULL);
}
