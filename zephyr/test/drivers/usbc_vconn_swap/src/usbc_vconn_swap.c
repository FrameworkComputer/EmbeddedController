/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <stdint.h>
#include <zephyr/kernel.h>
#include <zephyr/ztest.h>
#include <zephyr/drivers/gpio/gpio_emul.h>

#include "ec_commands.h"
#include "ec_tasks.h"
#include "emul/emul_isl923x.h"
#include "emul/tcpc/emul_ps8xxx.h"
#include "emul/tcpc/emul_tcpci.h"
#include "emul/tcpc/emul_tcpci_partner_common.h"
#include "emul/tcpc/emul_tcpci_partner_drp.h"
#include "emul/tcpc/emul_tcpci_partner_snk.h"
#include "host_command.h"
#include "test/drivers/stubs.h"
#include "tcpm/tcpci.h"
#include "test/drivers/utils.h"
#include "test/drivers/test_state.h"

#define TEST_PORT 0

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
	zassume_ok(tcpci_partner_connect_to_tcpci(partner_emul, tcpc_emul),
		   NULL);

	isl923x_emul_set_adc_vbus(charger_emul,
				  PDO_FIXED_GET_VOLT(src_ext->pdo[0]));

	/* Wait for PD negotiation and current ramp. */
	k_sleep(K_SECONDS(10));
}

static void disconnect_partner_from_port(const struct emul *tcpc_emul,
					 const struct emul *charger_emul)
{
	zassume_ok(tcpci_emul_disconnect_partner(tcpc_emul), NULL);
	isl923x_emul_set_adc_vbus(charger_emul, 0);
	k_sleep(K_SECONDS(1));
}

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

	common_before(&outer->common);
}

static void common_after(struct common_fixture *fixture)
{
	disconnect_partner_from_port(fixture->tcpci_emul,
				     fixture->charger_emul);
}

static void usbc_vconn_swap_after(void *data)
{
	struct usbc_vconn_swap_fixture *outer = data;

	common_after(&outer->common);
}

ZTEST_SUITE(usbc_vconn_swap, drivers_predicate_post_main, usbc_vconn_swap_setup,
	    usbc_vconn_swap_before, usbc_vconn_swap_after, NULL);
