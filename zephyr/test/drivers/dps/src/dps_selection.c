/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "battery.h"
#include "charge_state.h"
#include "console.h"
#include "dps.h"
#include "ec_commands.h"
#include "ec_tasks.h"
#include "emul/emul_isl923x.h"
#include "emul/tcpc/emul_tcpci.h"
#include "emul/tcpc/emul_tcpci_partner_common.h"
#include "emul/tcpc/emul_tcpci_partner_drp.h"
#include "emul/tcpc/emul_tcpci_partner_src.h"
#include "host_command.h"
#include "tcpm/tcpci.h"
#include "test/drivers/stubs.h"
#include "test/drivers/test_state.h"
#include "test/drivers/utils.h"
#include "usb_pd.h"

#include <stdint.h>

#include <zephyr/drivers/gpio/gpio_emul.h>
#include <zephyr/fff.h>
#include <zephyr/kernel.h>
#include <zephyr/shell/shell_dummy.h>
#include <zephyr/ztest.h>

#define TEST_PORT 0

FAKE_VALUE_FUNC(int, get_batt_charge_power, int *);
FAKE_VALUE_FUNC(int, get_battery_target_voltage, int *);
FAKE_VALUE_FUNC(int, get_desired_input_power, int *, int *);

struct common_fixture {
	const struct emul *tcpci_emul;
	const struct emul *charger_emul;
	struct tcpci_partner_data partner;
	struct tcpci_snk_emul_data snk_ext;
	struct tcpci_src_emul_data src_ext;
	struct tcpci_drp_emul_data drp_ext;
};

struct dps_selection_fixture {
	struct common_fixture common;
};

/* FFF fakes for driver functions. These get assigned to members of the
 * charger_drv struct
 */
static int target_mv_custom_fake;
static int get_battery_target_voltage_custom_fake(int *target_mv)
{
	*target_mv = target_mv_custom_fake;

	return EC_SUCCESS;
}

static int vbus_custom_fake;
static int input_current_custom_fake;
static int get_desired_input_power_custom_fake(int *vbus, int *input_current)
{
	*vbus = vbus_custom_fake;
	*input_current = input_current_custom_fake;

	return vbus_custom_fake * input_current_custom_fake / 1000;
}

static void reset(void)
{
	/* Reset fakes */
	RESET_FAKE(get_batt_charge_power);
	RESET_FAKE(get_battery_target_voltage);
	RESET_FAKE(get_desired_input_power);
}

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

static void *common_setup(void)
{
	static struct dps_selection_fixture outer_fixture;
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

static void *dps_selection_setup(void)
{
	return common_setup();
}

static void common_before(struct common_fixture *fixture)
{
	/* Set chipset to ON, this will set TCPM to DRP */
	test_set_chipset_to_s0();

	/* TODO(b/214401892): Check why need to give time TCPM to spin */
	k_sleep(K_SECONDS(1));
}

static void dps_selection_before(void *data)
{
	struct dps_selection_fixture *outer = data;

	common_before(&outer->common);

	reset();
}

static void common_after(struct common_fixture *fixture)
{
	if (pd_is_connected(TEST_PORT)) {
		disconnect_partner_from_port(fixture->tcpci_emul,
					     fixture->charger_emul);
	}
}

static void dps_selection_after(void *data)
{
	struct dps_selection_fixture *outer = data;

	common_after(&outer->common);

	reset();
}

ZTEST_USER_F(dps_selection, test_dps_pdo_switch)
{
	struct common_fixture *common = &fixture->common;
	struct tcpci_src_emul_data *src_ext = &common->src_ext;
	uint32_t *partner_pdo = src_ext->pdo;

	/* Attach a partner with all of the Source Capability attributes that
	 * "pd <port> srccaps" checks for.
	 */
	partner_pdo[0] =
		PDO_FIXED(5000, 3000,
			  PDO_FIXED_DUAL_ROLE | PDO_FIXED_UNCONSTRAINED |
				  PDO_FIXED_COMM_CAP | PDO_FIXED_DATA_SWAP |
				  PDO_FIXED_FRS_CURR_MASK);
	partner_pdo[1] = PDO_FIXED(9000, 3000, PDO_FIXED_UNCONSTRAINED);
	partner_pdo[2] = PDO_FIXED(10000, 3000, PDO_FIXED_UNCONSTRAINED);
	partner_pdo[3] = PDO_FIXED(12000, 3000, PDO_FIXED_UNCONSTRAINED);
	partner_pdo[4] = PDO_FIXED(15000, 3000, PDO_FIXED_UNCONSTRAINED);
	partner_pdo[5] = PDO_FIXED(20000, 3000, PDO_FIXED_UNCONSTRAINED);
	connect_partner_to_port(common->tcpci_emul, common->charger_emul,
				&common->partner, &common->src_ext);

	get_battery_target_voltage_fake.custom_fake =
		get_battery_target_voltage_custom_fake;
	get_desired_input_power_fake.custom_fake =
		get_desired_input_power_custom_fake;

	/* This value is not used if not have board overridden.  */
	get_batt_charge_power_fake.return_val = 5566;

	/* Assume the charge targeting at 9V. */
	target_mv_custom_fake = 9000;

	k_sleep(K_SECONDS(1));
	/* Assumes the system sinks 15W. */
	vbus_custom_fake = 20000;
	input_current_custom_fake = 750;

	/* DPS should request the PDO with the highest voltage at first. */
	zassert_equal(pd_get_requested_voltage(TEST_PORT), 20000, NULL);

	/* Wait for DPS to changing voltage */
	k_sleep(K_SECONDS(20));

	/* DPS should switch to 9V. */
	zassert_equal(pd_get_requested_voltage(TEST_PORT), 9000, NULL);

	/* Assumes the system sinks 27W/9V/3A. */
	vbus_custom_fake = 9000;
	input_current_custom_fake = 3000;
	k_sleep(K_SECONDS(20));
	/* PDO 10V/3A should be requested. */
	zassert_equal(pd_get_requested_voltage(TEST_PORT), 10000, NULL);

	/* Assumes the system sinks 30W/10V/3A. */
	vbus_custom_fake = 10000;
	k_sleep(K_SECONDS(20));
	/* PDO 12V/3A should be requested. */
	zassert_equal(pd_get_requested_voltage(TEST_PORT), 12000, NULL);

	/* Assumes the system sinks 36W/12V/3A. */
	vbus_custom_fake = 12000;
	k_sleep(K_SECONDS(20));
	/* PDO 15V/3A should be requested. */
	zassert_equal(pd_get_requested_voltage(TEST_PORT), 15000, NULL);
}

ZTEST_SUITE(dps_selection, drivers_predicate_post_main, dps_selection_setup,
	    dps_selection_before, dps_selection_after, NULL);
