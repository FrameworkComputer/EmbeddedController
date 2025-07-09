/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "emul/emul_isl923x.h"
#include "emul/emul_smart_battery.h"
#include "emul/tcpc/emul_tcpci_partner_snk.h"
#include "test/drivers/stubs.h"
#include "test/drivers/test_state.h"
#include "test/drivers/utils.h"
#include "usb_common.h"
#include "usb_pd.h"
#include "util.h"

#include <zephyr/ztest.h>

struct usb_pd_bist_shared_fixture {
	struct tcpci_partner_data sink_5v_500ma;
	struct tcpci_snk_emul_data snk_ext_500ma;
	struct tcpci_partner_data src;
	struct tcpci_src_emul_data src_ext;
	const struct emul *tcpci_emul; /* USBC_PORT_C0 in dts */
	const struct emul *tcpci_ps8xxx_emul; /* USBC_PORT_C1 in dts */
	const struct emul *charger_emul;
};

static void *usb_pd_bist_shared_setup(void)
{
	static struct usb_pd_bist_shared_fixture test_fixture;

	/* Get references for the emulators */
	test_fixture.tcpci_emul = EMUL_GET_USBC_BINDING(0, tcpc);
	test_fixture.charger_emul = EMUL_GET_USBC_BINDING(0, chg);
	test_fixture.tcpci_ps8xxx_emul = EMUL_GET_USBC_BINDING(1, tcpc);

	return &test_fixture;
}

static void usb_pd_bist_shared_before(void *data)
{
	struct usb_pd_bist_shared_fixture *test_fixture = data;

	/* Set chipset to ON, this will set TCPM to DRP */
	test_set_chipset_to_s0();

	/* TODO(b/214401892): Check why need to give time TCPM to spin */
	k_sleep(K_SECONDS(1));

	/* Initialized the sink to request 5V and 500mA */
	tcpci_partner_init(&test_fixture->sink_5v_500ma, PD_REV30);
	test_fixture->sink_5v_500ma.extensions =
		tcpci_snk_emul_init(&test_fixture->snk_ext_500ma,
				    &test_fixture->sink_5v_500ma, NULL);
	test_fixture->snk_ext_500ma.pdo[0] = PDO_FIXED(5000, 500, 0);

	/* Initialized the source */
	tcpci_partner_init(&test_fixture->src, PD_REV30);
	test_fixture->src.extensions = tcpci_src_emul_init(
		&test_fixture->src_ext, &test_fixture->src, NULL);

	/* Initially connect the 5V 500mA partner to C0 */
	connect_sink_to_port(&test_fixture->sink_5v_500ma,
			     test_fixture->tcpci_emul,
			     test_fixture->charger_emul);
}

static void usb_pd_bist_shared_after(void *data)
{
	struct usb_pd_bist_shared_fixture *test_fixture = data;

	/* Disocnnect C0 as sink, C1 as source */
	disconnect_sink_from_port(test_fixture->tcpci_emul);
	disconnect_source_from_port(test_fixture->tcpci_ps8xxx_emul,
				    test_fixture->charger_emul);
	host_cmd_typec_control_bist_share_mode(USBC_PORT_C0, 0);
}

ZTEST_SUITE(usb_pd_bist_shared, drivers_predicate_post_main,
	    usb_pd_bist_shared_setup, usb_pd_bist_shared_before,
	    usb_pd_bist_shared_after, NULL);

ZTEST_F(usb_pd_bist_shared, test_bist_shared_mode)
{
	uint32_t bist_data;
	uint32_t f5v_cap;

	/*
	 * Verify we were offered the 1.5A source cap because of our low current
	 * needs initially
	 */
	f5v_cap = fixture->snk_ext_500ma.last_5v_source_cap;
	/* Capability should be 5V fixed, 1.5 A */
	zassert_equal((f5v_cap & PDO_TYPE_MASK), PDO_TYPE_FIXED,
		      "PDO type wrong");
	zassert_equal(PDO_FIXED_VOLTAGE(f5v_cap), 5000, "PDO voltage wrong");
	zassert_equal(PDO_FIXED_CURRENT(f5v_cap), 1500,
		      "PDO initial current wrong");

	/* Start up BIST shared test mode */
	bist_data = BDO(BDO_MODE_SHARED_ENTER, 0);
	zassert_ok(tcpci_partner_send_data_msg(&fixture->sink_5v_500ma,
					       PD_DATA_BIST, &bist_data, 1, 0),
		   "Failed to send BIST enter message");

	/* The DUT has tBISTSharedTestMode (1 second) to offer us 3A now */
	k_sleep(K_SECONDS(1));

	f5v_cap = fixture->snk_ext_500ma.last_5v_source_cap;
	/* Capability should be 5V fixed, 3.0 A */
	zassert_equal((f5v_cap & PDO_TYPE_MASK), PDO_TYPE_FIXED,
		      "PDO type wrong");
	zassert_equal(PDO_FIXED_VOLTAGE(f5v_cap), 5000, "PDO voltage wrong");
	zassert_equal(PDO_FIXED_CURRENT(f5v_cap), 3000,
		      "PDO current didn't increase in BIST mode");

	/* Leave BIST shared test mode */
	bist_data = BDO(BDO_MODE_SHARED_EXIT, 0);
	zassert_ok(tcpci_partner_send_data_msg(&fixture->sink_5v_500ma,
					       PD_DATA_BIST, &bist_data, 1, 0),
		   "Failed to send BIST exit message");

	/*
	 * The DUT may now execute ErrorRecovery or simply send a new
	 * Source_Cap.  Either way, we should go back to 1.5 A
	 */
	k_sleep(K_SECONDS(5));

	f5v_cap = fixture->snk_ext_500ma.last_5v_source_cap;
	/* Capability should be 5V fixed, 1.5 A */
	zassert_equal((f5v_cap & PDO_TYPE_MASK), PDO_TYPE_FIXED,
		      "PDO type wrong");
	zassert_equal(PDO_FIXED_VOLTAGE(f5v_cap), 5000, "PDO voltage wrong");
	zassert_equal(PDO_FIXED_CURRENT(f5v_cap), 1500,
		      "PDO current didn't decrease after BIST exit");
}

ZTEST_F(usb_pd_bist_shared, test_bist_shared_no_snk_entry)
{
	uint32_t bist_data;
	uint32_t f5v_cap;

	/*
	 * Ensure we only enter BIST shared mode when acting as a source.  We
	 * must not enter shared mode from PE_SNK_Ready.
	 */

	/* Attach a new source */
	connect_source_to_port(&fixture->src, &fixture->src_ext, 1,
			       fixture->tcpci_ps8xxx_emul,
			       fixture->charger_emul);

	/* Have the source send the BIST Enter Mode */
	bist_data = BDO(BDO_MODE_SHARED_ENTER, 0);
	zassert_ok(tcpci_partner_send_data_msg(&fixture->src, PD_DATA_BIST,
					       &bist_data, 1, 0),
		   "Failed to send BIST enter message");

	/* Wait tBISTSharedTestMode (1 second) */
	k_sleep(K_SECONDS(1));

	/* Verify our low power sink on C0 still only has 1.5 A */
	f5v_cap = fixture->snk_ext_500ma.last_5v_source_cap;
	/* Capability should be 5V fixed, 1.5 A */
	zassert_equal((f5v_cap & PDO_TYPE_MASK), PDO_TYPE_FIXED,
		      "PDO type wrong");
	zassert_equal(PDO_FIXED_VOLTAGE(f5v_cap), 5000, "PDO voltage wrong");
	zassert_equal(PDO_FIXED_CURRENT(f5v_cap), 1500,
		      "PDO current incorrect after bad BIST entry");
}

ZTEST_F(usb_pd_bist_shared, test_bist_shared_exit_no_action)
{
	uint32_t bist_data;
	uint32_t f5v_cap;

	/*
	 * Verify that if we receive a BIST shared mode exit with no entry, we
	 * take no action on the port.
	 */
	tcpci_snk_emul_clear_last_5v_cap(&fixture->snk_ext_500ma);

	bist_data = BDO(BDO_MODE_SHARED_EXIT, 0);
	zassert_ok(tcpci_partner_send_data_msg(&fixture->sink_5v_500ma,
					       PD_DATA_BIST, &bist_data, 1, 0),
		   "Failed to send BIST exit message");

	/* Wait for the time it would take to settle out exit */
	k_sleep(K_SECONDS(5));

	/* Verify we didn't receive any new source caps due to the mode exit */
	f5v_cap = fixture->snk_ext_500ma.last_5v_source_cap;
	zassert_equal(f5v_cap, 0, "Received unexpected source cap");
}

ZTEST_F(usb_pd_bist_shared, test_control_bist_shared_mode)
{
	uint32_t f5v_cap;

	host_cmd_typec_control_bist_share_mode(USBC_PORT_C0, 1);
	zassert_ok(tcpci_partner_send_control_msg(&fixture->sink_5v_500ma,
						  PD_CTRL_GET_SOURCE_CAP, 0),
		   "Failed to send get src cap");
	/* wait tSenderResponse (26 ms) */
	k_sleep(K_MSEC(26));
	/*
	 * Verify we were offered the 3A source cap because of
	 * bist share mode be enabled.
	 */
	f5v_cap = fixture->snk_ext_500ma.last_5v_source_cap;
	/* Capability should be 5V fixed, 3 A */
	zassert_equal((f5v_cap & PDO_TYPE_MASK), PDO_TYPE_FIXED,
		      "PDO type wrong");
	zassert_equal(PDO_FIXED_VOLTAGE(f5v_cap), 5000, "PDO voltage wrong");
	zassert_equal(PDO_FIXED_CURRENT(f5v_cap), 3000,
		      "PDO initial current wrong");
	zassert_equal(typec_get_default_current_limit_rp(USBC_PORT_C0),
		      TYPEC_RP_3A0, "Default rp not 3A");

	host_cmd_typec_control_bist_share_mode(USBC_PORT_C0, 0);
}
