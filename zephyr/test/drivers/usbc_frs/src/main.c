/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "ec_commands.h"
#include "ec_tasks.h"
#include "emul/emul_isl923x.h"
#include "emul/tcpc/emul_tcpci.h"
#include "emul/tcpc/emul_tcpci_partner_common.h"
#include "emul/tcpc/emul_tcpci_partner_drp.h"
#include "emul/tcpc/emul_tcpci_partner_snk.h"
#include "emul/tcpc/emul_tcpci_partner_src.h"
#include "host_command.h"
#include "tcpm/tcpci.h"
#include "test/drivers/stubs.h"
#include "test/drivers/test_state.h"
#include "test/drivers/utils.h"
#include "usb_pd_policy.h"
#include "usb_pd_vdo.h"

#include <stdint.h>

#include <zephyr/drivers/gpio/gpio_emul.h>
#include <zephyr/fff.h>
#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

FAKE_VALUE_FUNC(bool, port_frs_disable_until_source_on, int);

struct common_fixture {
	const struct emul *tcpci_emul;
	const struct emul *charger_emul;
	struct tcpci_partner_data partner;
	struct tcpci_src_emul_data src_ext;
	struct tcpci_snk_emul_data snk_ext;
	struct tcpci_drp_emul_data drp_ext;
};

struct usbc_frs_fixture {
	struct common_fixture common;
};

#define TEST_PORT 0
BUILD_ASSERT(TEST_PORT == USBC_PORT_C0);

static void disconnect_partner_from_port(const struct emul *tcpc_emul,
					 const struct emul *charger_emul)
{
	zassert_ok(tcpci_emul_disconnect_partner(tcpc_emul));
	isl923x_emul_set_adc_vbus(charger_emul, 0);
	k_sleep(K_SECONDS(1));
}

static void common_before(struct common_fixture *common)
{
	port_frs_disable_until_source_on_fake.return_val = false;

	/* Set chipset to ON, this will set TCPM to DRP */
	test_set_chipset_to_s0();

	/* TODO(b/214401892): Check why need to give time TCPM to spin */
	k_sleep(K_SECONDS(1));

	connect_sink_to_port(&common->partner, common->tcpci_emul,
			     common->charger_emul);
}

static void common_after(struct common_fixture *common)
{
	disconnect_partner_from_port(common->tcpci_emul, common->charger_emul);
}

static void *usbc_frs_setup(void)
{
	static struct usbc_frs_fixture fixture;
	struct common_fixture *common = &fixture.common;
	struct tcpci_partner_data *partner = &common->partner;
	struct tcpci_drp_emul_data *drp_ext = &common->drp_ext;

	tcpci_partner_init(partner, PD_REV30);
	partner->extensions = tcpci_drp_emul_init(
		drp_ext, partner, PD_ROLE_SOURCE,
		tcpci_src_emul_init(&common->src_ext, partner, NULL),
		tcpci_snk_emul_init(&common->snk_ext, partner, NULL));
	/* Configure the partner to support FRS as initial source. */
	common->snk_ext.pdo[0] |= PDO_FIXED_FRS_CURR_DFLT_USB_POWER;

	/* Get references for the emulators */
	common->tcpci_emul = EMUL_GET_USBC_BINDING(TEST_PORT, tcpc);
	common->charger_emul = EMUL_GET_USBC_BINDING(TEST_PORT, chg);

	return &fixture;
}

static void usbc_frs_before(void *data)
{
	struct usbc_frs_fixture *fixture = data;

	common_before(&fixture->common);
}

static void usbc_frs_after(void *data)
{
	struct usbc_frs_fixture *fixture = data;

	common_after(&fixture->common);
}

ZTEST_SUITE(usbc_frs, drivers_predicate_post_main, usbc_frs_setup,
	    usbc_frs_before, usbc_frs_after, NULL);

ZTEST_USER_F(usbc_frs, test_frs_enable)
{
	struct common_fixture *common = &fixture->common;
	uint16_t power_control;

	zassert_ok(tcpci_emul_get_reg(common->tcpci_emul, TCPC_REG_POWER_CTRL,
				      &power_control));
	zassert_equal(power_control & TCPC_REG_POWER_CTRL_FRS_ENABLE,
		      TCPC_REG_POWER_CTRL_FRS_ENABLE);
}

ZTEST_USER_F(usbc_frs, test_frs_got_signal_fail)
{
	struct common_fixture *common = &fixture->common;
	uint16_t power_control;

	/* FRS should be enabled */
	zassert_ok(tcpci_emul_get_reg(common->tcpci_emul, TCPC_REG_POWER_CTRL,
				      &power_control));
	zassert_equal(power_control & TCPC_REG_POWER_CTRL_FRS_ENABLE,
		      TCPC_REG_POWER_CTRL_FRS_ENABLE);

	/* inform TCPM of FRS Rx */
	pd_got_frs_signal(TEST_PORT);

	k_sleep(K_MSEC(100));

	/* FRS failed, FRS disabled */
	zassert_ok(tcpci_emul_get_reg(common->tcpci_emul, TCPC_REG_POWER_CTRL,
				      &power_control));
	zassert_equal(power_control & TCPC_REG_POWER_CTRL_FRS_ENABLE, 0);
}

ZTEST_USER_F(usbc_frs, test_frs_got_signal_frs_delay_disable_fail)
{
	struct common_fixture *common = &fixture->common;
	uint16_t power_control;

	/* FRS should be enabled */
	zassert_ok(tcpci_emul_get_reg(common->tcpci_emul, TCPC_REG_POWER_CTRL,
				      &power_control));
	zassert_equal(power_control & TCPC_REG_POWER_CTRL_FRS_ENABLE,
		      TCPC_REG_POWER_CTRL_FRS_ENABLE);

	/* enable delay disable */
	port_frs_disable_until_source_on_fake.return_val = true;

	/* inform TCPM of FRS Rx */
	pd_got_frs_signal(TEST_PORT);

	k_sleep(K_MSEC(100));

	/* FRS failed, FRS disabled */
	zassert_ok(tcpci_emul_get_reg(common->tcpci_emul, TCPC_REG_POWER_CTRL,
				      &power_control));
	zassert_equal(power_control & TCPC_REG_POWER_CTRL_FRS_ENABLE, 0);
}
