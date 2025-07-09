/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "ec_commands.h"
#include "ec_tasks.h"
#include "emul/emul_isl923x.h"
#include "emul/tcpc/emul_ps8xxx.h"
#include "emul/tcpc/emul_tcpci.h"
#include "emul/tcpc/emul_tcpci_partner_common.h"
#include "emul/tcpc/emul_tcpci_partner_drp.h"
#include "emul/tcpc/emul_tcpci_partner_snk.h"
#include "host_command.h"
#include "tcpm/tcpci.h"
#include "test/drivers/stubs.h"
#include "test/drivers/test_state.h"
#include "test/drivers/utils.h"
#include "usb_tc_sm.h"

#include <stdint.h>

#include <zephyr/drivers/gpio/gpio_emul.h>
#include <zephyr/fff.h>
#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

#define TEST_PORT 0

FAKE_VALUE_FUNC(bool, port_discovery_vconn_swap_policy, int, bool);

/* This is the same as the real function. */
static bool port_discovery_vconn_swap_policy_custom(int port,
						    bool vconn_swap_flag)
{
	if (IS_ENABLED(CONFIG_USBC_VCONN) && vconn_swap_flag &&
	    !tc_is_vconn_src(port) && tc_check_vconn_swap(port))
		return true;

	/* Do not perform a VCONN swap */
	return false;
}

static bool port_discovery_vconn_swap_policy_nope(int port,
						  bool vconn_swap_flag)
{
	return false;
}

struct common_fixture {
	const struct emul *tcpci_emul;
	const struct emul *charger_emul;
	struct tcpci_partner_data partner;
	struct tcpci_snk_emul_data snk_ext;
	struct tcpci_src_emul_data src_ext;
	struct tcpci_drp_emul_data drp_ext;
};

struct usbc_vconn_swap_fixture {
	struct common_fixture common;
};

struct usbc_vconn_swap_late_fixture {
	struct common_fixture common;
};

struct usbc_vconn_swap_not_supported_fixture {
	struct common_fixture common;
};

struct usbc_vconn_swap_reject_fixture {
	struct common_fixture common;
};

static void connect_partner_to_port(const struct emul *tcpc_emul,
				    const struct emul *charger_emul,
				    struct tcpci_partner_data *partner_emul,
				    const struct tcpci_src_emul_data *src_ext)
{
	/*
	 * TODO(b/221439302): Updating the TCPCI emulator registers, updating
	 * the charger, and alerting should all be a part of the connect
	 * function.
	 */
	set_ac_enabled(true);
	zassert_ok(tcpci_partner_connect_to_tcpci(partner_emul, tcpc_emul),
		   NULL);

	isl923x_emul_set_adc_vbus(charger_emul,
				  PDO_FIXED_VOLTAGE(src_ext->pdo[0]));

	/* Wait for PD negotiation and current ramp. */
	k_sleep(K_SECONDS(10));
}

static void disconnect_partner_from_port(const struct emul *tcpc_emul,
					 const struct emul *charger_emul)
{
	zassert_ok(tcpci_emul_disconnect_partner(tcpc_emul), NULL);
	isl923x_emul_set_adc_vbus(charger_emul, 0);
	k_sleep(K_SECONDS(1));
}

/** Sets the partner as initial source, which should cause a VCONN swap to be
 * performed on connect.
 */
static void *common_setup(void)
{
	static struct usbc_vconn_swap_fixture outer_fixture;
	struct common_fixture *fixture = &outer_fixture.common;
	struct tcpci_partner_data *partner = &fixture->partner;
	struct tcpci_src_emul_data *src_ext = &fixture->src_ext;
	struct tcpci_snk_emul_data *snk_ext = &fixture->snk_ext;

	tcpci_partner_init(partner, PD_REV30);
	fixture->partner.extensions = tcpci_drp_emul_init(
		&fixture->drp_ext, partner, PD_ROLE_SOURCE,
		tcpci_src_emul_init(src_ext, partner, NULL),
		tcpci_snk_emul_init(snk_ext, partner, NULL));

	/* Get references for the emulators */
	fixture->tcpci_emul = EMUL_GET_USBC_BINDING(TEST_PORT, tcpc);
	fixture->charger_emul = EMUL_GET_USBC_BINDING(TEST_PORT, chg);

	return &outer_fixture;
}

static void *usbc_vconn_swap_setup(void)
{
	return common_setup();
}

static void *usbc_vconn_swap_not_supported_setup(void)
{
	struct usbc_vconn_swap_fixture *fixture = common_setup();

	/* The partner will respond to VCONN_Swap with Not_Supported. */
	tcpci_partner_set_vcs_response(&fixture->common.partner,
				       PD_CTRL_NOT_SUPPORTED);
	return fixture;
}

static void *usbc_vconn_swap_reject_setup(void)
{
	struct usbc_vconn_swap_fixture *fixture = common_setup();

	/* The partner will respond to VCONN_Swap with Reject. */
	tcpci_partner_set_vcs_response(&fixture->common.partner,
				       PD_CTRL_REJECT);
	return fixture;
}

static void common_before(struct common_fixture *fixture)
{
	/* Set chipset to ON, this will set TCPM to DRP */
	test_set_chipset_to_s0();

	/* TODO(b/214401892): Check why need to give time TCPM to spin */
	k_sleep(K_SECONDS(1));

	connect_partner_to_port(fixture->tcpci_emul, fixture->charger_emul,
				&fixture->partner, &fixture->src_ext);
	k_sleep(K_SECONDS(1));
}

static void usbc_vconn_swap_before(void *data)
{
	struct usbc_vconn_swap_fixture *outer = data;

	RESET_FAKE(port_discovery_vconn_swap_policy);
	port_discovery_vconn_swap_policy_fake.custom_fake =
		port_discovery_vconn_swap_policy_custom;

	common_before(&outer->common);
}

static void usbc_vconn_swap_late_before(void *data)
{
	struct usbc_vconn_swap_fixture *outer = data;

	static bool (*custom_fakes[])(
		int, bool) = { port_discovery_vconn_swap_policy_nope,
			       port_discovery_vconn_swap_policy_custom };

	RESET_FAKE(port_discovery_vconn_swap_policy);
	SET_CUSTOM_FAKE_SEQ(port_discovery_vconn_swap_policy, custom_fakes, 2);

	common_before(&outer->common);
}

static void usbc_vconn_swap_not_supported_before(void *data)
{
	struct usbc_vconn_swap_not_supported_fixture *outer = data;

	RESET_FAKE(port_discovery_vconn_swap_policy);
	port_discovery_vconn_swap_policy_fake.custom_fake =
		port_discovery_vconn_swap_policy_custom;

	common_before(&outer->common);
}

static void usbc_vconn_swap_reject_before(void *data)
{
	struct usbc_vconn_swap_reject_fixture *outer = data;

	RESET_FAKE(port_discovery_vconn_swap_policy);
	port_discovery_vconn_swap_policy_fake.custom_fake =
		port_discovery_vconn_swap_policy_custom;

	common_before(&outer->common);
}

static void common_after(struct common_fixture *fixture)
{
	disconnect_partner_from_port(fixture->tcpci_emul,
				     fixture->charger_emul);
	RESET_FAKE(port_discovery_vconn_swap_policy);
	port_discovery_vconn_swap_policy_fake.custom_fake =
		port_discovery_vconn_swap_policy_custom;
}

static void usbc_vconn_swap_after(void *data)
{
	struct usbc_vconn_swap_fixture *outer = data;

	common_after(&outer->common);
}

static void usbc_vconn_swap_not_supported_after(void *data)
{
	struct usbc_vconn_swap_not_supported_fixture *outer = data;

	common_after(&outer->common);
}

static void usbc_vconn_swap_reject_after(void *data)
{
	struct usbc_vconn_swap_reject_fixture *outer = data;

	common_after(&outer->common);
}

ZTEST_F(usbc_vconn_swap, test_vconn_swap_before_discovery)
{
	struct ec_response_typec_status status =
		host_cmd_typec_status(TEST_PORT);

	zassert_equal(status.vconn_role, PD_ROLE_VCONN_SRC,
		      "TCPM did not initiate VCONN Swap after attach");
}

ZTEST_F(usbc_vconn_swap, test_vconn_swap_via_host_command)
{
	struct ec_response_typec_status status =
		host_cmd_typec_status(TEST_PORT);

	zassert_equal(status.vconn_role, PD_ROLE_VCONN_SRC,
		      "TCPM did not initiate VCONN Swap after attach");

	host_cmd_usb_pd_control(TEST_PORT, USB_PD_CTRL_SWAP_VCONN);
	k_sleep(K_SECONDS(1));

	status = host_cmd_typec_status(TEST_PORT);
	zassert_equal(status.vconn_role, PD_ROLE_VCONN_OFF,
		      "TCPM did not initiate VCONN Swap after host command");
}

ZTEST_SUITE(usbc_vconn_swap, drivers_predicate_post_main, usbc_vconn_swap_setup,
	    usbc_vconn_swap_before, usbc_vconn_swap_after, NULL);

ZTEST_F(usbc_vconn_swap_not_supported, test_vconn_swap_force_vconn)
{
	struct ec_response_typec_status status =
		host_cmd_typec_status(TEST_PORT);

	zassert_equal(status.vconn_role, PD_ROLE_VCONN_SRC,
		      "TCPM did not initiate VCONN Swap after attach");
}

ZTEST_SUITE(usbc_vconn_swap_not_supported, drivers_predicate_post_main,
	    usbc_vconn_swap_not_supported_setup,
	    usbc_vconn_swap_not_supported_before,
	    usbc_vconn_swap_not_supported_after, NULL);

/** Tests if the board's port_discovery_vconn_swap_policy doesn't allow a
 *  vconn immediately, that it will still work eventually. See fake
 *  setup in usbc_vconn_swap_late_before().
 */
ZTEST_F(usbc_vconn_swap_late, test_vconn_swap_before_discovery)
{
	struct ec_response_typec_status status;

	status = host_cmd_typec_status(TEST_PORT);

	zassert_equal(status.vconn_role, PD_ROLE_VCONN_SRC,
		      "TCPM did not initiate VCONN Swap after attach");
}

ZTEST_SUITE(usbc_vconn_swap_late, drivers_predicate_post_main,
	    usbc_vconn_swap_setup, usbc_vconn_swap_late_before,
	    usbc_vconn_swap_after, NULL);

ZTEST_F(usbc_vconn_swap_reject, test_vconn_swap_force_vconn)
{
	struct ec_response_typec_status status =
		host_cmd_typec_status(TEST_PORT);

	zassert_equal(status.vconn_role, PD_ROLE_VCONN_OFF);
}

ZTEST_SUITE(usbc_vconn_swap_reject, drivers_predicate_post_main,
	    usbc_vconn_swap_reject_setup, usbc_vconn_swap_reject_before,
	    usbc_vconn_swap_reject_after, NULL);
