/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "common.h"
#include "ec_tasks.h"
#include "emul/emul_isl923x.h"
#include "emul/tcpc/emul_tcpci_partner_drp.h"
#include "tcpm/tcpci.h"
#include "test/drivers/stubs.h"
#include "test/drivers/tcpci_test_common.h"
#include "test/drivers/test_state.h"
#include "test/drivers/utils.h"
#include "test/usb_pe.h"
#include "usb_pd.h"

#include <stdint.h>

#include <zephyr/ztest.h>

enum usb_tc_state {
	NOT_A_REAL_STATE,
};

enum usb_tc_state get_state_tc(const int port);

#define TEST_USB_PORT 0

#define TEST_ADDED_PDO PDO_FIXED(10000, 3000, PDO_FIXED_UNCONSTRAINED)

struct usb_hard_reset_source_fixture {
	struct tcpci_partner_data partner_emul;
	struct tcpci_snk_emul_data snk_ext;
	struct tcpci_src_emul_data src_ext;
	struct tcpci_drp_emul_data drp_ext;
	const struct emul *tcpci_emul;
	const struct emul *charger_emul;
	enum pd_power_role drp_partner_pd_role;
};

static void *usb_hard_reset_source_setup(void)
{
	static struct usb_hard_reset_source_fixture fixture;

	/* Get references for the emulators */
	fixture.tcpci_emul = EMUL_GET_USBC_BINDING(TEST_USB_PORT, tcpc);
	fixture.charger_emul = EMUL_GET_USBC_BINDING(TEST_USB_PORT, chg);
	fixture.drp_partner_pd_role = PD_ROLE_SOURCE;

	return &fixture;
}

static void
tcpci_drp_emul_connect_partner(struct tcpci_partner_data *partner_emul,
			       const struct emul *tcpci_emul,
			       const struct emul *charger_emul)
{
	isl923x_emul_set_adc_vbus(charger_emul, 0);
	zassert_ok(tcpci_emul_set_vbus_level(tcpci_emul, VBUS_SAFE0V));

	zassert_ok(tcpci_partner_connect_to_tcpci(partner_emul, tcpci_emul),
		   NULL);
	isl923x_emul_set_adc_vbus(charger_emul, 5000);
}

static void usb_hard_reset_before(void *data)
{
	struct usb_hard_reset_source_fixture *fixture = data;

	set_test_runner_tid();

	test_set_chipset_to_g3();
	k_sleep(K_SECONDS(1));

	/* Set chipset to ON, this will set TCPM to DRP */
	test_set_chipset_to_s0();

	/* TODO(b/214401892): Check why need to give time TCPM to spin */
	k_sleep(K_SECONDS(1));

	/* Initialized DRP */
	tcpci_partner_init(&fixture->partner_emul, PD_REV20);
	fixture->partner_emul.extensions = tcpci_drp_emul_init(
		&fixture->drp_ext, &fixture->partner_emul,
		fixture->drp_partner_pd_role,
		tcpci_src_emul_init(&fixture->src_ext, &fixture->partner_emul,
				    NULL),
		tcpci_snk_emul_init(&fixture->snk_ext, &fixture->partner_emul,
				    NULL));
	/* Add additional Sink PDO to partner to verify
	 * PE_DR_SNK_Get_Sink_Cap/PE_SRC_Get_Sink_Cap (these are shared PE
	 * states) state was reached
	 */
	fixture->snk_ext.pdo[1] = TEST_ADDED_PDO;

	/* Turn TCPCI rev 2 ON */
	tcpc_config[TEST_USB_PORT].flags |= TCPC_FLAGS_TCPCI_REV2_0;

	tcpci_drp_emul_connect_partner(&fixture->partner_emul,
				       fixture->tcpci_emul,
				       fixture->charger_emul);

	k_sleep(K_SECONDS(10));
}

static void usb_hard_reset_after(void *data)
{
	struct usb_hard_reset_source_fixture *fixture = data;

	isl923x_emul_set_adc_vbus(fixture->charger_emul, 0);
	zassert_ok(tcpci_emul_disconnect_partner(fixture->tcpci_emul));
	k_sleep(K_SECONDS(1));
}

/** ZTEST_SUITE to setup DRP partner_emul as SOURCE  */
ZTEST_SUITE(usb_hard_reset_source, drivers_predicate_post_main,
	    usb_hard_reset_source_setup, usb_hard_reset_before,
	    usb_hard_reset_after, NULL);

/**
 * @brief TestPurpose: Perform a normal hard reset.
 */
ZTEST_F(usb_hard_reset_source, test_normal)
{
	int state;

	/* Partner sends the Hard Reset. */
	tcpci_partner_common_send_hard_reset(&fixture->partner_emul);
	/* Partner waits ~30 ms - 650 ms (PD_T_SAFE_0V) */
	k_sleep(K_MSEC(30));
	/* Partner drops to vSafe0V */
	isl923x_emul_set_adc_vbus(fixture->charger_emul, 0);
	zassert_ok(tcpci_emul_set_vbus_level(fixture->tcpci_emul, VBUS_SAFE0V));

	/* Partner waits ~660 ms - 1000 ms (PD_T_SRC_RECOVER_MAX)
	 * + 0 ms - 275 ms (PD_T_SRC_TURN_ON)
	 */
	k_sleep(K_MSEC(660));

	/* Partner sets VBUS present */
	isl923x_emul_set_adc_vbus(fixture->charger_emul, 5000);
	zassert_ok(
		tcpci_emul_set_vbus_level(fixture->tcpci_emul, VBUS_PRESENT));

	/* Wait for everything to settle, and check state. */
	k_sleep(K_SECONDS(2));
	state = get_state_tc(TEST_USB_PORT);
	zassert_equal(state, /* TC_ATTACHED_SNK */ 7, "Got %d", state);
	state = get_state_pe(TEST_USB_PORT);
	zassert_equal(state, PE_SNK_READY, "Got %d", state);
}

/**
 * @brief TestPurpose: Perform a hard reset where the VBUS doesn't reach
 *        vSave0V on time.
 */
ZTEST_F(usb_hard_reset_source, test_vsafe0v_late)
{
	int state;

	/* Partner sends the Hard Reset. */
	tcpci_partner_common_send_hard_reset(&fixture->partner_emul);
	/* Partner more than ~30 ms - 650 ms (PD_T_SAFE_0V) */
	k_sleep(K_MSEC(660));
	/* Partner drops to vSafe0V */
	isl923x_emul_set_adc_vbus(fixture->charger_emul, 0);
	zassert_ok(tcpci_emul_set_vbus_level(fixture->tcpci_emul, VBUS_SAFE0V));

	/* Partner waits ~660 ms - 1000 ms (PD_T_SRC_RECOVER_MAX)
	 * + 0 ms - 275 ms (PD_T_SRC_TURN_ON)
	 */
	k_sleep(K_MSEC(660));

	/* Partner sets VBUS present */
	isl923x_emul_set_adc_vbus(fixture->charger_emul, 5000);
	zassert_ok(
		tcpci_emul_set_vbus_level(fixture->tcpci_emul, VBUS_PRESENT));

	/* Wait for everything to settle, and check state. */
	k_sleep(K_SECONDS(5));
	state = get_state_tc(TEST_USB_PORT);
	zassert_equal(state, /* TC_ATTACHED_SNK */ 7, "Got %d", state);
	state = get_state_pe(TEST_USB_PORT);
	zassert_equal(state, PE_SNK_READY, "Got %d", state);
}

/**
 * @brief TestPurpose: Perform a hard reset where the VBUS present is late.
 */
ZTEST_F(usb_hard_reset_source, test_vbus_present_late)
{
	int state;

	/* Partner sends the Hard Reset. */
	tcpci_partner_common_send_hard_reset(&fixture->partner_emul);
	/* Partner waits ~30 ms - 650 ms (PD_T_SAFE_0V) */
	k_sleep(K_MSEC(30));
	/* Partner drops to vSafe0V */
	isl923x_emul_set_adc_vbus(fixture->charger_emul, 0);
	zassert_ok(tcpci_emul_set_vbus_level(fixture->tcpci_emul, VBUS_SAFE0V));

	/* Partner waits more than ~660 ms - 1000 ms (PD_T_SRC_RECOVER_MAX)
	 * + 0 ms - 275 ms (PD_T_SRC_TURN_ON)
	 */
	k_sleep(K_MSEC(1300));

	/* Partner sets VBUS present */
	isl923x_emul_set_adc_vbus(fixture->charger_emul, 5000);
	zassert_ok(
		tcpci_emul_set_vbus_level(fixture->tcpci_emul, VBUS_PRESENT));

	/* Wait for everything to settle, and check state. */
	k_sleep(K_SECONDS(5));
	state = get_state_tc(TEST_USB_PORT);
	zassert_equal(state, /* TC_ATTACHED_SNK */ 7, "Got %d", state);
	state = get_state_pe(TEST_USB_PORT);
	zassert_equal(state, PE_SNK_READY, "Got %d", state);
}
