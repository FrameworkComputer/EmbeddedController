/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "battery_smart.h"
#include "emul/emul_isl923x.h"
#include "emul/emul_smart_battery.h"
#include "emul/tcpc/emul_tcpci_partner_src.h"
#include "test/drivers/stubs.h"
#include "test/drivers/test_state.h"
#include "test/drivers/utils.h"
#include "usb_pd.h"

#include <zephyr/sys/byteorder.h>
#include <zephyr/ztest.h>

#define BATTERY_NODE DT_NODELABEL(battery)

#define TEST_PORT USBC_PORT_C0

struct usb_charging_policy_fixture {
	struct tcpci_partner_data partner;
	struct tcpci_src_emul_data src_ext;
	const struct emul *tcpci_emul;
	const struct emul *charger_emul;
};

static void *usb_charging_policy_setup(void)
{
	static struct usb_charging_policy_fixture fixture;
	struct tcpci_partner_data *partner = &fixture.partner;
	struct tcpci_src_emul_data *src_ext = &fixture.src_ext;

	tcpci_partner_init(partner, PD_REV30);
	partner->extensions = tcpci_src_emul_init(src_ext, partner, NULL);

	/* Get references for the emulators */
	fixture.tcpci_emul = EMUL_DT_GET(DT_NODELABEL(tcpci_emul));
	fixture.charger_emul = EMUL_DT_GET(DT_NODELABEL(isl923x_emul));

	/* Set chipset on so the "AP" is on to give us commands */
	test_set_chipset_to_s0();

	return &fixture;
}

static void usb_charging_policy_after(void *data)
{
	struct usb_charging_policy_fixture *fix = data;

	disconnect_source_from_port(fix->tcpci_emul, fix->charger_emul);
	tcpci_partner_common_clear_logged_msgs(&fix->partner);
}

ZTEST_SUITE(usb_charging_policy, drivers_predicate_post_main,
	    usb_charging_policy_setup, NULL, usb_charging_policy_after, NULL);

ZTEST_F(usb_charging_policy, test_charge_from_pure_source)
{
	/* Report ourselves as a high-powered partner with unlimited power */
	fixture->partner.extensions =
		tcpci_src_emul_init(&fixture->src_ext, &fixture->partner, NULL);
	fixture->src_ext.pdo[0] =
		PDO_FIXED(5000, 3000, PDO_FIXED_UNCONSTRAINED);
	fixture->src_ext.pdo[1] = PDO_FIXED(9000, 3000, 0);
	fixture->src_ext.pdo[2] = PDO_FIXED(12000, 3000, 0);
	fixture->src_ext.pdo[3] = PDO_FIXED(15000, 3000, 0);

	/* Connect our port partner */
	connect_source_to_port(&fixture->partner, &fixture->src_ext, 0,
			       fixture->tcpci_emul, fixture->charger_emul);

	struct ec_response_usb_pd_power_info info =
		host_cmd_power_info(TEST_PORT);

	zassert_equal(info.role, USB_PD_PORT_POWER_SINK);
	zassert_equal(info.type, USB_CHG_TYPE_PD);
	zassert_equal(info.meas.voltage_max, 15000);
	zassert_equal(info.meas.current_max, 3000);
	zassert_equal(info.max_power, 15000 * 3000);
}

ZTEST_F(usb_charging_policy, test_charge_from_drp_source)
{
	/*
	 * Report ourselves as a DRP and forget to set UP flag, as some buggy
	 * partners do
	 */
	fixture->partner.extensions =
		tcpci_src_emul_init(&fixture->src_ext, &fixture->partner, NULL);
	fixture->src_ext.pdo[0] = PDO_FIXED(5000, 3000, PDO_FIXED_DUAL_ROLE);
	fixture->src_ext.pdo[1] = PDO_FIXED(9000, 3000, 0);
	fixture->src_ext.pdo[2] = PDO_FIXED(12000, 3000, 0);
	fixture->src_ext.pdo[3] = PDO_FIXED(15000, 3000, 0);

	/* Connect our port partner */
	connect_source_to_port(&fixture->partner, &fixture->src_ext, 0,
			       fixture->tcpci_emul, fixture->charger_emul);

	struct ec_response_usb_pd_power_info info =
		host_cmd_power_info(TEST_PORT);

	zassert_equal(info.role, USB_PD_PORT_POWER_SINK);
	zassert_equal(info.type, USB_CHG_TYPE_PD);
	zassert_equal(info.meas.voltage_max, 15000);
	zassert_equal(info.meas.current_max, 3000);
	zassert_equal(info.max_power, 15000 * 3000);
}

ZTEST_F(usb_charging_policy, test_no_charge_from_low_drp)
{
	/*
	 * Report ourselves as a low-power DRP partner
	 */
	fixture->partner.extensions =
		tcpci_src_emul_init(&fixture->src_ext, &fixture->partner, NULL);
	fixture->src_ext.pdo[0] = PDO_FIXED(5000, 3000, PDO_FIXED_DUAL_ROLE);

	/* Connect our port partner */
	connect_source_to_port(&fixture->partner, &fixture->src_ext, 0,
			       fixture->tcpci_emul, fixture->charger_emul);

	struct ec_response_usb_pd_power_info info =
		host_cmd_power_info(TEST_PORT);

	zassert_equal(info.role, USB_PD_PORT_POWER_SINK_NOT_CHARGING);
}

ZTEST_F(usb_charging_policy, test_dut_gets_src_caps)
{
	fixture->partner.extensions =
		tcpci_src_emul_init(&fixture->src_ext, &fixture->partner, NULL);

	/* Connect our port partner */
	connect_source_to_port(&fixture->partner, &fixture->src_ext, 0,
			       fixture->tcpci_emul, fixture->charger_emul);

	/* DPM requests that the DUT gathers source caps */
	tcpci_partner_common_enable_pd_logging(&fixture->partner, true);
	pd_dpm_request(TEST_PORT, DPM_REQUEST_SOURCE_CAP);
	k_sleep(K_SECONDS(1));

	tcpci_partner_common_enable_pd_logging(&fixture->partner, false);

	struct tcpci_partner_log_msg *msg;
	bool get_src_cap_seen = false;

	SYS_SLIST_FOR_EACH_CONTAINER(&fixture->partner.msg_log, msg, node)
	{
		uint16_t header = sys_get_le16(msg->buf);

		/* Ignore messages from the test */
		if (msg->sender == TCPCI_PARTNER_SENDER_PARTNER)
			continue;

		if ((PD_HEADER_CNT(header) == 0) &&
		    (PD_HEADER_TYPE(header) == PD_CTRL_GET_SOURCE_CAP) &&
		    (PD_HEADER_EXT(header) == 0)) {
			get_src_cap_seen = true;
			break;
		}
	}
	zassert_true(get_src_cap_seen);
}
